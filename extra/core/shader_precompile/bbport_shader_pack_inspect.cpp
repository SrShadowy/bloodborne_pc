// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: answering questions about the 3-file shader archive -- does it exist, what does its index
// say, and do the offsets in it stay inside the two data files. Building the archive is in
// bbport_shader_pack_pack.cpp, expanding it in bbport_shader_pack_unpack.cpp.
#include "bbport_shader_pack_internal.h"

#include <cstring>
#include <fstream>

namespace BbShaderPack {

bool HasArchive(const std::filesystem::path& cache_dir) {
    const auto paths = detail::PathsIn(cache_dir);
    return std::filesystem::exists(paths.index) && std::filesystem::exists(paths.shaders) &&
           std::filesystem::exists(paths.metadata) &&
           std::filesystem::file_size(paths.index) >= sizeof(ArchiveIndexHeader);
}

ArchiveStats GetArchiveStats(const std::filesystem::path& cache_dir) {
    ArchiveStats stats{};
    if (!HasArchive(cache_dir)) {
        return stats;
    }

    const auto paths = detail::PathsIn(cache_dir);
    stats.index_file_size = std::filesystem::file_size(paths.index);
    stats.shaders_file_size = std::filesystem::file_size(paths.shaders);
    stats.meta_file_size = std::filesystem::file_size(paths.metadata);

    std::ifstream file(paths.index, std::ios::binary);
    if (!file.is_open()) {
        return stats;
    }

    ArchiveIndexHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file.good() || header.magic != MAGIC_INDEX || header.version != ARCHIVE_VERSION) {
        return stats;
    }

    stats.valid = true;
    stats.shader_count = header.shader_count;
    stats.pipeline_count = header.pipeline_count;
    stats.title_id =
        std::string(header.title_id, strnlen(header.title_id, sizeof(header.title_id)));
    return stats;
}

bool VerifyArchive(const std::filesystem::path& cache_dir) {
    const auto stats = GetArchiveStats(cache_dir);
    if (!stats.valid) {
        return false;
    }

    std::ifstream file(cache_dir / ARCHIVE_INDEX_NAME, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    ArchiveIndexHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    // Every entry must point inside the file that holds its bytes.
    for (std::uint32_t i = 0; i < header.shader_count; ++i) {
        ArchiveShaderEntry entry{};
        file.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        if (!file.good() || entry.spv_offset + entry.spv_size > stats.shaders_file_size ||
            entry.meta_offset + entry.meta_size > stats.meta_file_size) {
            return false;
        }
    }
    for (std::uint32_t i = 0; i < header.pipeline_count; ++i) {
        ArchivePipelineEntry entry{};
        file.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        if (!file.good() || entry.key_offset + entry.key_size > stats.meta_file_size) {
            return false;
        }
    }
    return true;
}

} // namespace BbShaderPack
