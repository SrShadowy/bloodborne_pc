// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: reading and writing the driver's pipeline-cache blob. A blob is only accepted when its
// header matches this GPU (vendor, device, cache UUID), and it is written to a temporary file that
// is renamed over the real one, so a crash mid-write never leaves a corrupt cache behind. When the
// file is missing, the usual install layouts are searched for one that can be imported.
#include "bbport_vk_pipeline_cache_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "common/elf_info.h"
#include "common/path_util.h"

namespace BbVkPipelineCache {
namespace detail {

std::filesystem::path ResolveCacheDir(const std::filesystem::path& dir) {
    if (!dir.empty()) {
        return dir;
    }
    const auto serial = Common::ElfInfo::Instance().GameSerial();
    const std::string s = serial.empty() ? "CUSA03173" : std::string(serial);
    return Common::FS::GetUserPath(Common::FS::PathType::CacheDir) / s;
}

namespace {

/// The install layouts a cache from an earlier run may sit in, in the order they are tried.
std::vector<std::filesystem::path> ImportCandidates(const std::string& serial) {
    std::vector<std::filesystem::path> candidates;
    if (const char* port_dir = std::getenv("BB_PORT_DIR")) {
        candidates.push_back(std::filesystem::path(port_dir) / "user/cache" / serial /
                             DRIVER_CACHE_NAME);
    }
    if (const char* app_dir = std::getenv("APPDIR")) {
        candidates.push_back(std::filesystem::path(app_dir) / "user/cache" / serial /
                             DRIVER_CACHE_NAME);
    }
    candidates.push_back(std::filesystem::path("user/cache") / serial / DRIVER_CACHE_NAME);
    candidates.push_back(std::filesystem::path("../user/cache") / serial / DRIVER_CACHE_NAME);
    candidates.push_back(std::filesystem::path("../../user/cache") / serial / DRIVER_CACHE_NAME);
    candidates.push_back(std::filesystem::path("dist/user/cache") / serial / DRIVER_CACHE_NAME);
    candidates.push_back(std::filesystem::path("../dist/user/cache") / serial / DRIVER_CACHE_NAME);
    return candidates;
}

/// Copies the first usable candidate into `final_dir`; true when one was imported.
bool ImportFromCandidates(const std::string& serial, const std::filesystem::path& final_dir,
                          const std::filesystem::path& target) {
    for (const auto& candidate : ImportCandidates(serial)) {
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec) ||
            std::filesystem::file_size(candidate, ec) <= sizeof(PipelineCacheHeader)) {
            continue;
        }
        std::filesystem::create_directories(final_dir, ec);
        std::filesystem::copy_file(candidate, target, std::filesystem::copy_options::skip_existing,
                                   ec);
        if (!ec) {
            std::printf("[Shaders] VkPipelineCache: Imported persistent driver cache from %s\n",
                        candidate.string().c_str());
            return true;
        }
    }
    return false;
}

/// Resizes `out` to `size` bytes and reads the file from its start; false on any failure.
bool ReadWholeFile(std::ifstream& file, std::size_t size, std::vector<std::uint8_t>& out) {
    file.seekg(0, std::ios::beg);
    out.resize(size);
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    return file.good();
}

} // namespace
} // namespace detail

std::vector<std::uint8_t> LoadDriverCache(vk::PhysicalDevice physical_device,
                                          const std::filesystem::path& cache_dir) {
    const auto props = physical_device.getProperties();
    detail::g_status.vendor_id = props.vendorID;
    detail::g_status.device_id = props.deviceID;
    detail::g_status.device_name = props.deviceName.data();
    detail::g_status.loaded = false;
    detail::g_status.size_bytes = 0;

    // A cold run measures the shaders from scratch: loading a blob would spoil the comparison.
    if (const char* cold_env = std::getenv("BB_SHADER_COLD_RUN"); cold_env && cold_env[0] != '0') {
        detail::g_status.cold_forced = true;
        std::printf("[Shaders] VkPipelineCache: Cold run requested (BB_SHADER_COLD_RUN); skipping "
                    "persistent driver cache.\n");
        return {};
    }

    const auto final_dir = detail::ResolveCacheDir(cache_dir);
    const auto path = final_dir / DRIVER_CACHE_NAME;
    if (!std::filesystem::exists(path)) {
        const auto serial = Common::ElfInfo::Instance().GameSerial();
        detail::ImportFromCandidates(serial.empty() ? "CUSA03173" : std::string(serial), final_dir,
                                     path);
    }
    if (!std::filesystem::exists(path)) {
        return {};
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }
    const auto file_size = file.tellg();
    if (file_size < static_cast<std::streampos>(sizeof(detail::PipelineCacheHeader))) {
        return {};
    }

    file.seekg(0, std::ios::beg);
    detail::PipelineCacheHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good() || header.headerLength < sizeof(detail::PipelineCacheHeader) ||
        header.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) {
        return {};
    }
    if (header.vendorID != props.vendorID || header.deviceID != props.deviceID ||
        std::memcmp(header.pipelineCacheUUID, props.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
        std::printf("[Shaders] VkPipelineCache: Driver cache UUID mismatch; discarding old driver "
                    "cache.\n");
        return {};
    }

    std::vector<std::uint8_t> data;
    if (!detail::ReadWholeFile(file, static_cast<std::size_t>(file_size), data)) {
        return {};
    }

    detail::g_status.loaded = true;
    detail::g_status.size_bytes = data.size();
    std::printf("[Shaders] VkPipelineCache: Loaded persistent driver cache (%zu bytes) for device "
                "0x%04x:0x%04x (%s)\n",
                data.size(), props.vendorID, props.deviceID, props.deviceName.data());
    return data;
}

bool SaveDriverCache(vk::Device device, vk::PipelineCache cache,
                     const std::filesystem::path& cache_dir) {
    if (!cache || !device) {
        return false;
    }
    if (detail::g_status.cold_forced) {
        // A cold run compiles from scratch; saving its empty cache would clobber a real one.
        return false;
    }

    std::vector<std::uint8_t> data;
    {
        // Query in memory under the lock, then release it before the disk I/O.
        std::lock_guard lock{detail::g_mutex};
        std::size_t data_size = 0;
        if (device.getPipelineCacheData(cache, &data_size, nullptr) != vk::Result::eSuccess ||
            data_size <= sizeof(detail::PipelineCacheHeader)) {
            return false;
        }
        data.resize(data_size);
        if (device.getPipelineCacheData(cache, &data_size, data.data()) != vk::Result::eSuccess) {
            return false;
        }
    }

    const auto final_dir = detail::ResolveCacheDir(cache_dir);
    std::error_code ec;
    std::filesystem::create_directories(final_dir, ec);
    const auto tmp_path = final_dir / (std::string(DRIVER_CACHE_NAME) + ".tmp");
    const auto final_path = final_dir / DRIVER_CACHE_NAME;

    std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    file.close();
    if (!file.good()) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }

    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        return false;
    }
    std::printf("[Shaders] VkPipelineCache: Saved %zu bytes to %s\n", data.size(),
                final_path.string().c_str());
    return true;
}

} // namespace BbVkPipelineCache
