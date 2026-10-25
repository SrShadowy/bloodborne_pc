// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: persistent VkPipelineCache driver serialization implementation.
#include "bbport_vk_pipeline_cache.h"
#include "common/elf_info.h"
#include "common/path_util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace BbVkPipelineCache {

namespace {

static DriverCacheStatus s_status{};

struct PipelineCacheHeader {
    uint32_t headerLength;
    uint32_t headerVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint8_t pipelineCacheUUID[VK_UUID_SIZE];
};

std::filesystem::path ResolveCacheDir(const std::filesystem::path& dir) {
    if (!dir.empty()) {
        return dir;
    }
    const auto serial = Common::ElfInfo::Instance().GameSerial();
    const std::string s = serial.empty() ? "CUSA03173" : std::string(serial);
    return Common::FS::GetUserPath(Common::FS::PathType::CacheDir) / s;
}

} // namespace

DriverCacheStatus GetDriverCacheStatus() {
    return s_status;
}

std::vector<uint8_t> LoadDriverCache(vk::PhysicalDevice physical_device, const std::filesystem::path& cache_dir) {
    const auto props = physical_device.getProperties();
    s_status.vendor_id = props.vendorID;
    s_status.device_id = props.deviceID;
    s_status.device_name = props.deviceName.data();
    s_status.loaded = false;
    s_status.size_bytes = 0;

    const char* cold_env = std::getenv("BB_SHADER_COLD_RUN");
    if (cold_env && cold_env[0] != '0') {
        s_status.cold_forced = true;
        std::printf("VkPipelineCache: Cold run requested (BB_SHADER_COLD_RUN); skipping persistent driver cache.\n");
        return {};
    }

    const auto final_dir = ResolveCacheDir(cache_dir);
    const auto p = final_dir / DRIVER_CACHE_NAME;
    if (!std::filesystem::exists(p)) {
        return {};
    }

    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        return {};
    }

    const auto file_size = f.tellg();
    if (file_size < static_cast<std::streampos>(sizeof(PipelineCacheHeader))) {
        return {};
    }

    f.seekg(0, std::ios::beg);
    PipelineCacheHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f.good() || hdr.headerLength < sizeof(PipelineCacheHeader) || hdr.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) {
        return {};
    }

    if (hdr.vendorID != props.vendorID || hdr.deviceID != props.deviceID ||
        std::memcmp(hdr.pipelineCacheUUID, props.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
        std::printf("VkPipelineCache: Driver cache UUID mismatch; discarding old driver cache.\n");
        return {};
    }

    std::vector<uint8_t> data(static_cast<size_t>(file_size));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(data.data()), file_size);
    if (!f.good()) {
        return {};
    }

    s_status.loaded = true;
    s_status.size_bytes = data.size();
    std::printf("VkPipelineCache: Loaded persistent driver cache (%zu bytes) for device 0x%04x:0x%04x (%s)\n",
                data.size(), props.vendorID, props.deviceID, props.deviceName.data());
    return data;
}

bool SaveDriverCache(vk::Device device, vk::PipelineCache cache, const std::filesystem::path& cache_dir) {
    if (!cache || !device) {
        return false;
    }

    size_t data_size = 0;
    const auto res1 = device.getPipelineCacheData(cache, &data_size, nullptr);
    if (res1 != vk::Result::eSuccess || data_size <= sizeof(PipelineCacheHeader)) {
        return false;
    }

    std::vector<uint8_t> data(data_size);
    const auto res2 = device.getPipelineCacheData(cache, &data_size, data.data());
    if (res2 != vk::Result::eSuccess) {
        return false;
    }

    const auto final_dir = ResolveCacheDir(cache_dir);
    const auto tmp_p = final_dir / (std::string(DRIVER_CACHE_NAME) + ".tmp");
    const auto final_p = final_dir / DRIVER_CACHE_NAME;

    std::ofstream f(tmp_p, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        return false;
    }
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    f.close();
    if (!f.good()) {
        std::filesystem::remove(tmp_p);
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp_p, final_p, ec);
    if (!ec) {
        std::printf("VkPipelineCache: Saved %zu bytes to %s\n", data.size(), final_p.string().c_str());
        return true;
    }
    return false;
}

} // namespace BbVkPipelineCache
