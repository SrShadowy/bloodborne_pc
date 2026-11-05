// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: building the 3-file shader archive out of the loose shadPS4 files (.spv, .meta, .key).
// The shaders and their metadata go into the .pack and .bmeta files, the index that ties them
// together is written last, and all three are renamed into place together. Reading an archive back
// is in bbport_shader_pack_unpack.cpp.
#include "bbport_shader_pack_internal.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>

namespace BbShaderPack {
namespace {

/// The loose pipeline keys are named "g_0x<16 hex>.key" (graphics) or "c_0x<16 hex>.key"
/// (compute); both carry the "X_0x" prefix, so the hash starts two characters in.
constexpr const char* kComputeKeyPrefix = "c_0x";
constexpr const char* kGraphicsKeyPrefix = "g_0x";
constexpr std::size_t kKeyPrefixLength = 2;

/// The .meta files on disk, keyed by the program hash in their name.
std::map<std::uint64_t, std::filesystem::path> CollectMetaFiles(
    const std::filesystem::path& cache_dir) {
    std::map<std::uint64_t, std::filesystem::path> meta_files;
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
        if (entry.path().extension() != ".meta") {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        const std::uint64_t hash = std::strtoull(stem.c_str(), nullptr, 16);
        if (hash != 0) {
            meta_files[hash] = entry.path();
        }
    }
    return meta_files;
}

/// Appends the .spv files to the pack (and their .meta files, when present) and returns their index
/// entries. `meta_files` is what CollectMetaFiles() found.
std::vector<ArchiveShaderEntry> PackShaders(
    const std::filesystem::path& cache_dir,
    const std::map<std::uint64_t, std::filesystem::path>& meta_files, std::ofstream& out_spv,
    std::ofstream& out_meta, std::uint64_t& spv_offset, std::uint64_t& meta_offset) {
    std::vector<ArchiveShaderEntry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
        if (entry.path().extension() != ".spv") {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        std::uint64_t pgm_hash = 0;
        std::uint32_t perm_idx = 0;
        if (std::sscanf(stem.c_str(), "%llx_%u", &pgm_hash, &perm_idx) < 1) {
            continue;
        }

        std::vector<std::uint8_t> spv_bytes;
        if (!detail::ReadFileBytes(entry.path(), spv_bytes)) {
            continue;
        }

        ArchiveShaderEntry shader{};
        shader.pgm_hash = pgm_hash;
        shader.perm_idx = perm_idx;
        shader.spv_offset = spv_offset;
        shader.spv_size = static_cast<std::uint32_t>(spv_bytes.size());

        out_spv.write(reinterpret_cast<const char*>(spv_bytes.data()), spv_bytes.size());
        spv_offset += spv_bytes.size();

        const auto meta = meta_files.find(pgm_hash);
        if (meta != meta_files.end()) {
            std::vector<std::uint8_t> meta_bytes;
            if (detail::ReadFileBytes(meta->second, meta_bytes)) {
                shader.meta_offset = meta_offset;
                shader.meta_size = static_cast<std::uint32_t>(meta_bytes.size());
                out_meta.write(reinterpret_cast<const char*>(meta_bytes.data()),
                               meta_bytes.size());
                meta_offset += meta_bytes.size();
            }
        }
        entries.push_back(shader);
    }
    return entries;
}

/// Appends the .key files and returns their index entries. The keys share the metadata file with
/// the shader metadata, so they continue from `meta_offset`.
std::vector<ArchivePipelineEntry> PackPipelineKeys(const std::filesystem::path& cache_dir,
                                                   std::ofstream& out_meta,
                                                   std::uint64_t& meta_offset) {
    std::vector<ArchivePipelineEntry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
        if (entry.path().extension() != ".key") {
            continue;
        }
        const std::string stem = entry.path().stem().string();
        std::uint64_t pipe_hash = 0;
        bool is_compute = false;
        if (stem.rfind(kComputeKeyPrefix, 0) == 0) {
            is_compute = true;
            pipe_hash = std::strtoull(stem.c_str() + kKeyPrefixLength, nullptr, 16);
        } else if (stem.rfind(kGraphicsKeyPrefix, 0) == 0) {
            pipe_hash = std::strtoull(stem.c_str() + kKeyPrefixLength, nullptr, 16);
        } else {
            pipe_hash = std::strtoull(stem.c_str(), nullptr, 16);
        }

        std::vector<std::uint8_t> key_bytes;
        if (!detail::ReadFileBytes(entry.path(), key_bytes)) {
            continue;
        }

        ArchivePipelineEntry pipeline{};
        pipeline.pipeline_hash = pipe_hash;
        pipeline.is_compute = is_compute ? 1 : 0;
        pipeline.key_offset = meta_offset;
        pipeline.key_size = static_cast<std::uint32_t>(key_bytes.size());

        out_meta.write(reinterpret_cast<const char*>(key_bytes.data()), key_bytes.size());
        meta_offset += key_bytes.size();
        entries.push_back(pipeline);
    }
    return entries;
}

/// Writes the index file: the header, then the shader entries, then the pipeline entries.
bool WriteIndex(const std::filesystem::path& path, const ArchiveIndexHeader& header,
                const std::vector<ArchiveShaderEntry>& shaders,
                const std::vector<ArchivePipelineEntry>& pipelines) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (const auto& shader : shaders) {
        file.write(reinterpret_cast<const char*>(&shader), sizeof(shader));
    }
    for (const auto& pipeline : pipelines) {
        file.write(reinterpret_cast<const char*>(&pipeline), sizeof(pipeline));
    }
    file.close();
    return file.good();
}

} // namespace

bool PackDirectory(const std::filesystem::path& cache_dir, const std::string& title_id) {
    if (!std::filesystem::exists(cache_dir)) {
        return false;
    }

    const auto tmp_idx = cache_dir / (std::string(ARCHIVE_INDEX_NAME) + ".tmp");
    const auto tmp_spv = cache_dir / (std::string(ARCHIVE_SHADERS_NAME) + ".tmp");
    const auto tmp_meta = cache_dir / (std::string(ARCHIVE_METADATA_NAME) + ".tmp");

    std::ofstream out_spv(tmp_spv, std::ios::binary | std::ios::trunc);
    std::ofstream out_meta(tmp_meta, std::ios::binary | std::ios::trunc);
    if (!out_spv.is_open() || !out_meta.is_open()) {
        return false;
    }

    // The 16-byte magic header each data file starts with.
    std::uint32_t spv_header[4] = {MAGIC_SHADERS, ARCHIVE_VERSION, 0, 0};
    std::uint32_t meta_header[4] = {MAGIC_ECHOES, ARCHIVE_VERSION, 0, 0};
    out_spv.write(reinterpret_cast<char*>(spv_header), sizeof(spv_header));
    out_meta.write(reinterpret_cast<char*>(meta_header), sizeof(meta_header));

    std::uint64_t spv_offset = sizeof(spv_header);
    std::uint64_t meta_offset = sizeof(meta_header);

    const auto meta_files = CollectMetaFiles(cache_dir);
    const auto shader_entries =
        PackShaders(cache_dir, meta_files, out_spv, out_meta, spv_offset, meta_offset);
    const auto pipeline_entries = PackPipelineKeys(cache_dir, out_meta, meta_offset);
    out_spv.close();
    out_meta.close();

    ArchiveIndexHeader header{};
    header.magic = MAGIC_INDEX;
    header.version = ARCHIVE_VERSION;
    std::strncpy(header.title_id, title_id.c_str(), sizeof(header.title_id) - 1);
    header.shader_count = static_cast<std::uint32_t>(shader_entries.size());
    header.pipeline_count = static_cast<std::uint32_t>(pipeline_entries.size());
    header.total_spv_bytes = spv_offset - sizeof(spv_header);
    header.total_meta_bytes = meta_offset - sizeof(meta_header);
    header.timestamp = static_cast<std::uint64_t>(std::time(nullptr));

    if (!WriteIndex(tmp_idx, header, shader_entries, pipeline_entries)) {
        return false;
    }

    // The three files replace the old ones together, so a crash never leaves a mixed archive.
    std::error_code ec;
    std::filesystem::rename(tmp_idx, cache_dir / ARCHIVE_INDEX_NAME, ec);
    std::filesystem::rename(tmp_spv, cache_dir / ARCHIVE_SHADERS_NAME, ec);
    std::filesystem::rename(tmp_meta, cache_dir / ARCHIVE_METADATA_NAME, ec);
    // The pre-rename archive name must not linger: BlobStore's *.meta glob would re-import it.
    std::filesystem::remove(cache_dir / ARCHIVE_METADATA_NAME_LEGACY, ec);
    return !ec;
}

} // namespace BbShaderPack
