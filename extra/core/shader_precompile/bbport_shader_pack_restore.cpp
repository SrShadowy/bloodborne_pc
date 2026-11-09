// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: seeding an empty cache with the master assets shipped next to the executable (the shader
// archive, its loose keys and the driver cache). It runs at boot, before discovery, so a fresh
// install does not stutter through the first area. Unpacking the archive is in
// bbport_shader_pack_unpack.cpp.
#include "bbport_shader_pack_internal.h"

#include <cstdio>
#include <filesystem>
#include <vector>

namespace BbShaderPack {
namespace {

/// The file kinds a master cache ships.
bool IsCacheAsset(const std::filesystem::path& path) {
    const auto extension = path.extension();
    return extension == ".pack" || extension == ".idx" || extension == ".meta" ||
           extension == ".bmeta" || extension == ".key" || extension == ".spv" ||
           extension == ".bin" || extension == ".cache";
}

/// Copies the master assets from a candidate directory; returns how many files landed.
std::size_t MergeCandidate(const std::filesystem::path& candidate,
                           const std::filesystem::path& cache_dir) {
    std::error_code ec;
    std::size_t merged = 0;
    for (const auto& entry : std::filesystem::directory_iterator(candidate, ec)) {
        if (!IsCacheAsset(entry.path())) {
            continue;
        }
        // Masters ship the pre-rename grimoire name; land it under the current one.
        auto name = entry.path().filename();
        if (name == ARCHIVE_METADATA_NAME_LEGACY) {
            name = ARCHIVE_METADATA_NAME;
        }
        if (std::filesystem::copy_file(entry.path(), cache_dir / name,
                                       std::filesystem::copy_options::skip_existing, ec)) {
            ++merged;
        }
    }
    return merged;
}

/// True when the cache holds no loose .key file (discovery needs them, the archive alone does not
/// count).
bool HasNoLooseKeys(const std::filesystem::path& cache_dir) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir, ec)) {
        if (entry.path().extension() == ".key") {
            return false;
        }
    }
    return true;
}

} // namespace

bool RestoreMasterCacheIfEmpty(const std::filesystem::path& cache_dir_param) {
    const std::string serial = detail::GameSerialOrDefault();
    const auto cache_dir = detail::ResolveCacheDir(cache_dir_param);
    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    MigrateArchiveNames(cache_dir);

    std::size_t merged = 0;
    for (const auto& candidate : detail::MasterCacheCandidates(serial)) {
        if (!std::filesystem::exists(candidate, ec) ||
            std::filesystem::weakly_canonical(candidate, ec) ==
                std::filesystem::weakly_canonical(cache_dir, ec)) {
            continue;
        }
        merged = MergeCandidate(candidate, cache_dir);
        if (merged > 0) {
            std::printf("[Shaders] Merged %zu master cache assets to eliminate stuttering!\n",
                        merged);
            break;
        }
    }

    // The archive can be present while its loose keys were never unpacked.
    if (HasNoLooseKeys(cache_dir) && HasArchive(cache_dir)) {
        UnpackArchive(cache_dir);
    }

    return merged > 0;
}

} // namespace BbShaderPack
