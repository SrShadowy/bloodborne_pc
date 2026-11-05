// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the 3-file shader archive ("Hunter's Grimoire") -- the paths its files live at and the
// helpers the rest of the module shares. The archive itself is inspected in
// bbport_shader_pack_inspect.cpp, built in bbport_shader_pack_pack.cpp, expanded in
// bbport_shader_pack_unpack.cpp and seeded from a shipped copy in bbport_shader_pack_restore.cpp.
#include "bbport_shader_pack_internal.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "common/elf_info.h"
#include "common/path_util.h"

namespace BbShaderPack::detail {

ArchivePaths PathsIn(const std::filesystem::path& cache_dir) {
    return ArchivePaths{cache_dir / ARCHIVE_INDEX_NAME, cache_dir / ARCHIVE_SHADERS_NAME,
                        cache_dir / ARCHIVE_METADATA_NAME};
}

std::string GameSerialOrDefault() {
    const auto serial = Common::ElfInfo::Instance().GameSerial();
    return serial.empty() ? std::string("CUSA03173") : std::string(serial);
}

std::filesystem::path ResolveCacheDir(const std::filesystem::path& dir) {
    if (!dir.empty()) {
        return dir;
    }
    return Common::FS::GetUserPath(Common::FS::PathType::CacheDir) / GameSerialOrDefault();
}

std::vector<std::filesystem::path> MasterCacheCandidates(const std::string& serial) {
    std::vector<std::filesystem::path> candidates;
    if (const char* port_dir = std::getenv("BB_PORT_DIR")) {
        candidates.push_back(std::filesystem::path(port_dir) / "user/cache" / serial);
    }
    if (const char* app_dir = std::getenv("APPDIR")) {
        candidates.push_back(std::filesystem::path(app_dir) / "user/cache" / serial);
    }
    candidates.push_back(std::filesystem::path("user/cache") / serial);
    candidates.push_back(std::filesystem::path("../user/cache") / serial);
    candidates.push_back(std::filesystem::path("../../user/cache") / serial);
    candidates.push_back(std::filesystem::path("dist/user/cache") / serial);
    candidates.push_back(std::filesystem::path("../dist/user/cache") / serial);
    return candidates;
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& out) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    const auto size = file.tellg();
    if (size <= 0) {
        return false;
    }
    out.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(out.data()), size);
    return file.good();
}

} // namespace BbShaderPack::detail

namespace BbShaderPack {

void MigrateArchiveNames(const std::filesystem::path& cache_dir_param) {
    const auto cache_dir = detail::ResolveCacheDir(cache_dir_param);
    std::error_code ec;
    const auto legacy = cache_dir / ARCHIVE_METADATA_NAME_LEGACY;
    const auto current = cache_dir / ARCHIVE_METADATA_NAME;
    if (std::filesystem::exists(legacy, ec) && !std::filesystem::exists(current, ec)) {
        std::filesystem::rename(legacy, current, ec);
        if (!ec) {
            std::printf("[Shaders] BbShaderPack: migrated %s to %s\n", ARCHIVE_METADATA_NAME_LEGACY,
                        ARCHIVE_METADATA_NAME);
        }
    }
}

} // namespace BbShaderPack

bool extra_shader_pack_restore_master() {
    return BbShaderPack::RestoreMasterCacheIfEmpty();
}
