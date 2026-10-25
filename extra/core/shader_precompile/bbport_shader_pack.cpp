// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: 3-file optimized shader archive ("Hunter's Grimoire") implementation.
#include "bbport_shader_pack.h"
#include "common/elf_info.h"
#include "common/path_util.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string_view>

namespace BbShaderPack {

namespace {

bool ReadFileBytes(const std::filesystem::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    const auto sz = f.tellg();
    if (sz <= 0) return false;
    out.resize(static_cast<size_t>(sz));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(out.data()), sz);
    return f.good();
}

} // namespace

bool HasArchive(const std::filesystem::path& cache_dir) {
    const auto p_idx = cache_dir / ARCHIVE_INDEX_NAME;
    const auto p_spv = cache_dir / ARCHIVE_SHADERS_NAME;
    const auto p_meta = cache_dir / ARCHIVE_METADATA_NAME;
    return std::filesystem::exists(p_idx) && std::filesystem::exists(p_spv) &&
           std::filesystem::exists(p_meta) && std::filesystem::file_size(p_idx) >= sizeof(ArchiveIndexHeader);
}

ArchiveStats GetArchiveStats(const std::filesystem::path& cache_dir) {
    ArchiveStats stats{};
    if (!HasArchive(cache_dir)) return stats;

    const auto p_idx = cache_dir / ARCHIVE_INDEX_NAME;
    const auto p_spv = cache_dir / ARCHIVE_SHADERS_NAME;
    const auto p_meta = cache_dir / ARCHIVE_METADATA_NAME;

    stats.index_file_size = std::filesystem::file_size(p_idx);
    stats.shaders_file_size = std::filesystem::file_size(p_spv);
    stats.meta_file_size = std::filesystem::file_size(p_meta);

    std::ifstream f(p_idx, std::ios::binary);
    if (!f.is_open()) return stats;

    ArchiveIndexHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f.good() || hdr.magic != MAGIC_INDEX || hdr.version != ARCHIVE_VERSION) return stats;

    stats.valid = true;
    stats.shader_count = hdr.shader_count;
    stats.pipeline_count = hdr.pipeline_count;
    stats.title_id = std::string(hdr.title_id, strnlen(hdr.title_id, sizeof(hdr.title_id)));
    return stats;
}

bool VerifyArchive(const std::filesystem::path& cache_dir) {
    const auto stats = GetArchiveStats(cache_dir);
    if (!stats.valid) return false;

    std::ifstream f_idx(cache_dir / ARCHIVE_INDEX_NAME, std::ios::binary);
    if (!f_idx.is_open()) return false;

    ArchiveIndexHeader hdr{};
    f_idx.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));

    for (uint32_t i = 0; i < hdr.shader_count; ++i) {
        ArchiveShaderEntry e{};
        f_idx.read(reinterpret_cast<char*>(&e), sizeof(e));
        if (!f_idx.good()) return false;
        if (e.spv_offset + e.spv_size > stats.shaders_file_size) return false;
        if (e.meta_offset + e.meta_size > stats.meta_file_size) return false;
    }

    for (uint32_t i = 0; i < hdr.pipeline_count; ++i) {
        ArchivePipelineEntry e{};
        f_idx.read(reinterpret_cast<char*>(&e), sizeof(e));
        if (!f_idx.good()) return false;
        if (e.key_offset + e.key_size > stats.meta_file_size) return false;
    }
    return true;
}

bool PackDirectory(const std::filesystem::path& cache_dir, const std::string& title_id) {
    if (!std::filesystem::exists(cache_dir)) return false;

    std::vector<ArchiveShaderEntry> shader_entries;
    std::vector<ArchivePipelineEntry> pipe_entries;

    const auto tmp_idx = cache_dir / (std::string(ARCHIVE_INDEX_NAME) + ".tmp");
    const auto tmp_spv = cache_dir / (std::string(ARCHIVE_SHADERS_NAME) + ".tmp");
    const auto tmp_meta = cache_dir / (std::string(ARCHIVE_METADATA_NAME) + ".tmp");

    std::ofstream out_spv(tmp_spv, std::ios::binary | std::ios::trunc);
    std::ofstream out_meta(tmp_meta, std::ios::binary | std::ios::trunc);
    if (!out_spv.is_open() || !out_meta.is_open()) return false;

    // 16-byte magic header for pack and meta files
    uint32_t spv_hdr[4] = {MAGIC_SHADERS, ARCHIVE_VERSION, 0, 0};
    uint32_t meta_hdr[4] = {MAGIC_ECHOES, ARCHIVE_VERSION, 0, 0};
    out_spv.write(reinterpret_cast<char*>(spv_hdr), sizeof(spv_hdr));
    out_meta.write(reinterpret_cast<char*>(meta_hdr), sizeof(meta_hdr));

    uint64_t cur_spv_offset = sizeof(spv_hdr);
    uint64_t cur_meta_offset = sizeof(meta_hdr);

    std::map<uint64_t, std::filesystem::path> meta_files;
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
        if (entry.path().extension() == ".meta") {
            const std::string stem = entry.path().stem().string();
            const uint64_t hash = std::strtoull(stem.c_str(), nullptr, 16);
            if (hash != 0) meta_files[hash] = entry.path();
        }
    }

    // 1. Pack Shaders (.spv and matching .meta)
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
        if (entry.path().extension() == ".spv") {
            const std::string stem = entry.path().stem().string();
            uint64_t pgm_hash = 0;
            uint32_t perm_idx = 0;
            if (std::sscanf(stem.c_str(), "%llx_%u", &pgm_hash, &perm_idx) < 1) continue;

            std::vector<uint8_t> spv_bytes;
            if (!ReadFileBytes(entry.path(), spv_bytes)) continue;

            ArchiveShaderEntry se{};
            se.pgm_hash = pgm_hash;
            se.perm_idx = perm_idx;
            se.spv_offset = cur_spv_offset;
            se.spv_size = static_cast<uint32_t>(spv_bytes.size());

            out_spv.write(reinterpret_cast<const char*>(spv_bytes.data()), spv_bytes.size());
            cur_spv_offset += spv_bytes.size();

            auto it = meta_files.find(pgm_hash);
            if (it != meta_files.end()) {
                std::vector<uint8_t> meta_bytes;
                if (ReadFileBytes(it->second, meta_bytes)) {
                    se.meta_offset = cur_meta_offset;
                    se.meta_size = static_cast<uint32_t>(meta_bytes.size());
                    out_meta.write(reinterpret_cast<const char*>(meta_bytes.data()), meta_bytes.size());
                    cur_meta_offset += meta_bytes.size();
                }
            }
            shader_entries.push_back(se);
        }
    }

    // 2. Pack Pipeline Keys (.key)
    for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
        if (entry.path().extension() == ".key") {
            const std::string stem = entry.path().stem().string();
            uint64_t pipe_hash = 0;
            bool is_compute = false;
            if (stem.rfind("c_0x", 0) == 0) {
                is_compute = true;
                pipe_hash = std::strtoull(stem.c_str() + 2, nullptr, 16);
            } else if (stem.rfind("g_0x", 0) == 0) {
                pipe_hash = std::strtoull(stem.c_str() + 2, nullptr, 16);
            } else {
                pipe_hash = std::strtoull(stem.c_str(), nullptr, 16);
            }

            std::vector<uint8_t> key_bytes;
            if (!ReadFileBytes(entry.path(), key_bytes)) continue;

            ArchivePipelineEntry pe{};
            pe.pipeline_hash = pipe_hash;
            pe.is_compute = is_compute ? 1 : 0;
            pe.key_offset = cur_meta_offset;
            pe.key_size = static_cast<uint32_t>(key_bytes.size());

            out_meta.write(reinterpret_cast<const char*>(key_bytes.data()), key_bytes.size());
            cur_meta_offset += key_bytes.size();
            pipe_entries.push_back(pe);
        }
    }
    out_spv.close();
    out_meta.close();

    // 3. Write Index File
    std::ofstream out_idx(tmp_idx, std::ios::binary | std::ios::trunc);
    if (!out_idx.is_open()) return false;

    ArchiveIndexHeader hdr{};
    hdr.magic = MAGIC_INDEX;
    hdr.version = ARCHIVE_VERSION;
    std::strncpy(hdr.title_id, title_id.c_str(), sizeof(hdr.title_id) - 1);
    hdr.shader_count = static_cast<uint32_t>(shader_entries.size());
    hdr.pipeline_count = static_cast<uint32_t>(pipe_entries.size());
    hdr.total_spv_bytes = cur_spv_offset - sizeof(spv_hdr);
    hdr.total_meta_bytes = cur_meta_offset - sizeof(meta_hdr);
    hdr.timestamp = static_cast<uint64_t>(std::time(nullptr));

    out_idx.write(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    for (const auto& se : shader_entries) {
        out_idx.write(reinterpret_cast<const char*>(&se), sizeof(se));
    }
    for (const auto& pe : pipe_entries) {
        out_idx.write(reinterpret_cast<const char*>(&pe), sizeof(pe));
    }
    out_idx.close();

    // Atomic rename
    std::error_code ec;
    std::filesystem::rename(tmp_idx, cache_dir / ARCHIVE_INDEX_NAME, ec);
    std::filesystem::rename(tmp_spv, cache_dir / ARCHIVE_SHADERS_NAME, ec);
    std::filesystem::rename(tmp_meta, cache_dir / ARCHIVE_METADATA_NAME, ec);
    return !ec;
}

bool RestoreMasterCacheIfEmpty(const std::filesystem::path& cache_dir_param) {
    std::filesystem::path cache_dir = cache_dir_param;
    if (cache_dir.empty()) {
        const auto serial = Common::ElfInfo::Instance().GameSerial();
        const std::string s = serial.empty() ? "CUSA03173" : std::string(serial);
        cache_dir = Common::FS::GetUserPath(Common::FS::PathType::CacheDir) / s;
    }
    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);

    const std::filesystem::path candidates[] = {
        "dist/user/cache/CUSA03173",
        "../dist/user/cache/CUSA03173",
    };
    size_t merged = 0;
    for (const auto& cand : candidates) {
        if (std::filesystem::exists(cand, ec) &&
            std::filesystem::weakly_canonical(cand, ec) != std::filesystem::weakly_canonical(cache_dir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(cand, ec)) {
                const auto ext = entry.path().extension();
                if (ext == ".key" || ext == ".meta" || ext == ".spv" || ext == ".bin") {
                    if (std::filesystem::copy_file(entry.path(), cache_dir / entry.path().filename(),
                                                   std::filesystem::copy_options::skip_existing, ec)) {
                        merged++;
                    }
                }
            }
            if (merged > 0) {
                std::printf("BBPort: Merged %zu master cache assets to eliminate stuttering!\n", merged);
                return true;
            }
        }
    }
    return false;
}

} // namespace BbShaderPack

bool extra_shader_pack_restore_master() {
    return BbShaderPack::RestoreMasterCacheIfEmpty();
}
