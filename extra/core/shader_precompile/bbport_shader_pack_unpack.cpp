// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: expanding the 3-file shader archive back into the loose files (.spv, .meta, .key) that
// BlobStore and the shader discovery expect. Files that already exist are left alone, so a cache
// that is only half unpacked finishes on the next boot. Building an archive is in
// bbport_shader_pack_pack.cpp.
#include "bbport_shader_pack_internal.h"

#include <cstdio>
#include <fstream>

namespace BbShaderPack {
namespace {

/// Extracts `size` bytes at `offset` into `dest`, unless the file is already there.
bool ExtractIfMissing(std::ifstream& source, std::uint64_t offset, std::uint32_t size,
                      const std::filesystem::path& dest) {
    if (std::filesystem::exists(dest)) {
        return false;
    }
    std::vector<std::uint8_t> buffer(size);
    source.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    source.read(reinterpret_cast<char*>(buffer.data()), size);
    if (!source.good()) {
        return false;
    }
    std::ofstream out(dest, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    return out.good();
}

/// The loose name of a shader's .spv: "0x<16 hex>_<perm>.spv".
std::string SpvName(const ArchiveShaderEntry& shader) {
    char name[64];
    std::snprintf(name, sizeof(name), "0x%016llx_%u.spv",
                  static_cast<unsigned long long>(shader.pgm_hash), shader.perm_idx);
    return name;
}

/// The loose name of a shader's metadata: "0x<16 hex>.meta".
std::string MetaName(const ArchiveShaderEntry& shader) {
    char name[64];
    std::snprintf(name, sizeof(name), "0x%016llx.meta",
                  static_cast<unsigned long long>(shader.pgm_hash));
    return name;
}

/// The loose name of a pipeline key: "g_0x<16 hex>.key" or "c_0x<16 hex>.key".
std::string KeyName(const ArchivePipelineEntry& pipeline) {
    char name[64];
    std::snprintf(name, sizeof(name), "%s0x%016llx.key", pipeline.is_compute ? "c_" : "g_",
                  static_cast<unsigned long long>(pipeline.pipeline_hash));
    return name;
}

} // namespace

bool UnpackArchive(const std::filesystem::path& cache_dir) {
    if (!HasArchive(cache_dir)) {
        return false;
    }

    const auto paths = detail::PathsIn(cache_dir);
    std::ifstream index(paths.index, std::ios::binary);
    std::ifstream shaders(paths.shaders, std::ios::binary);
    std::ifstream metadata(paths.metadata, std::ios::binary);
    if (!index.is_open() || !shaders.is_open() || !metadata.is_open()) {
        return false;
    }

    ArchiveIndexHeader header{};
    index.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!index.good() || header.magic != MAGIC_INDEX || header.version != ARCHIVE_VERSION) {
        return false;
    }

    // Both data files start with the same 16-byte magic header the packer wrote.
    std::uint32_t shader_magic[4]{};
    std::uint32_t meta_magic[4]{};
    shaders.read(reinterpret_cast<char*>(shader_magic), sizeof(shader_magic));
    metadata.read(reinterpret_cast<char*>(meta_magic), sizeof(meta_magic));
    if (shader_magic[0] != MAGIC_SHADERS ||
        (meta_magic[0] != MAGIC_META && meta_magic[0] != MAGIC_ECHOES)) {
        return false;
    }

    std::uint32_t unpacked_spv = 0;
    std::uint32_t unpacked_meta = 0;
    std::uint32_t unpacked_keys = 0;

    for (std::uint32_t i = 0; i < header.shader_count; ++i) {
        ArchiveShaderEntry shader{};
        index.read(reinterpret_cast<char*>(&shader), sizeof(shader));
        if (!index.good()) {
            break;
        }
        if (shader.spv_size > 0 &&
            ExtractIfMissing(shaders, shader.spv_offset, shader.spv_size,
                             cache_dir / SpvName(shader))) {
            ++unpacked_spv;
        }
        if (shader.meta_size > 0 &&
            ExtractIfMissing(metadata, shader.meta_offset, shader.meta_size,
                             cache_dir / MetaName(shader))) {
            ++unpacked_meta;
        }
    }

    for (std::uint32_t i = 0; i < header.pipeline_count; ++i) {
        ArchivePipelineEntry pipeline{};
        index.read(reinterpret_cast<char*>(&pipeline), sizeof(pipeline));
        if (!index.good()) {
            break;
        }
        if (pipeline.key_size > 0 &&
            ExtractIfMissing(metadata, pipeline.key_offset, pipeline.key_size,
                             cache_dir / KeyName(pipeline))) {
            ++unpacked_keys;
        }
    }

    std::printf("[Shaders] BbShaderPack: Unpacked %u SPV shaders, %u metadata, and %u pipeline keys "
                "from Hunter's Grimoire\n",
                unpacked_spv, unpacked_meta, unpacked_keys);
    return true;
}

} // namespace BbShaderPack
