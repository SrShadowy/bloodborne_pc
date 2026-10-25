// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: 3-file optimized shader archive ("Hunter's Grimoire").
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace BbShaderPack {

constexpr const char* ARCHIVE_INDEX_NAME    = "hunter_insight.idx";
constexpr const char* ARCHIVE_SHADERS_NAME  = "caryll_runes.pack";
constexpr const char* ARCHIVE_METADATA_NAME = "blood_echoes.meta";

constexpr uint32_t MAGIC_INDEX   = 0x47534e49; // "INSG"
constexpr uint32_t MAGIC_SHADERS = 0x454e5552; // "RUNE"
constexpr uint32_t MAGIC_META    = 0x48434542; // "BECH"
constexpr uint32_t MAGIC_ECHOES  = MAGIC_META;
constexpr uint32_t ARCHIVE_VERSION = 1;

struct ArchiveIndexHeader {
    uint32_t magic = MAGIC_INDEX;
    uint32_t version = ARCHIVE_VERSION;
    char title_id[16] = {0};
    uint32_t shader_count = 0;
    uint32_t pipeline_count = 0;
    uint64_t total_spv_bytes = 0;
    uint64_t total_meta_bytes = 0;
    uint64_t timestamp = 0;
    uint32_t flags = 0;
    uint32_t reserved[3] = {0};
};

struct ArchiveShaderEntry {
    uint64_t pgm_hash = 0;
    uint32_t stage = 0;
    uint32_t perm_idx = 0;
    uint64_t spv_offset = 0;
    uint32_t spv_size = 0;
    uint64_t meta_offset = 0;
    uint32_t meta_size = 0;
    uint32_t flags = 0;
};

struct ArchivePipelineEntry {
    uint64_t pipeline_hash = 0;
    uint32_t is_compute = 0;
    uint32_t stage_count = 0;
    uint64_t stage_hashes[6] = {0};
    uint64_t key_offset = 0;
    uint32_t key_size = 0;
};

struct ArchiveStats {
    bool valid = false;
    uint32_t shader_count = 0;
    uint32_t pipeline_count = 0;
    uint64_t index_file_size = 0;
    uint64_t shaders_file_size = 0;
    uint64_t meta_file_size = 0;
    std::string title_id;
};

// Check if the 3-file archive exists in the given cache directory
bool HasArchive(const std::filesystem::path& cache_dir);

// Get stats about the 3-file archive
ArchiveStats GetArchiveStats(const std::filesystem::path& cache_dir);

// Packs loose shadPS4 files (.spv, .meta, .key) in cache_dir into the 3 archive files
bool PackDirectory(const std::filesystem::path& cache_dir, const std::string& title_id = "CUSA03173");

// Verify integrity of the 3-file archive
bool VerifyArchive(const std::filesystem::path& cache_dir);

// Restores master shaders and pipeline keys if the cache directory is empty
bool RestoreMasterCacheIfEmpty(const std::filesystem::path& cache_dir = {});

} // namespace BbShaderPack
