// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_shader_discovery.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "common/elf_info.h"
#include "common/logging/log.h"
#include "common/path_util.h"

namespace BbShaderDiscovery {

namespace {

const char* HwStageToString(uint32_t stage) {
    switch (stage) {
    case 0: return "Fragment";
    case 1: return "Vertex";
    case 2: return "Geometry";
    case 3: return "Export";
    case 4: return "Hull";
    case 5: return "Local";
    case 6: return "Compute";
    default: return "Unknown";
    }
}

const char* SwStageToString(uint32_t stage) {
    switch (stage) {
    case 0: return "Fragment";
    case 1: return "TessellationControl";
    case 2: return "TessellationEval";
    case 3: return "Vertex";
    case 4: return "Geometry";
    case 5: return "Compute";
    default: return "Unknown";
    }
}

std::string GetTimestampIso() {
    const auto now = std::chrono::system_clock::now();
    const auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
    gmtime_r(&time_t_now, &tm_now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_now);
    return std::string(buf);
}

void WriteJsonAtomic(const std::filesystem::path& target_path, const std::string& json_content) {
    std::error_code ec;
    std::filesystem::create_directories(target_path.parent_path(), ec);

    const std::filesystem::path temp_path = target_path.string() + ".tmp";
    {
        std::ofstream ofs(temp_path, std::ios::out | std::ios::trunc);
        if (!ofs.is_open()) {
            LOG_ERROR(Render, "ShaderDiscovery: Failed to open {} for writing", temp_path.string());
            return;
        }
        ofs << json_content;
    }

    std::filesystem::rename(temp_path, target_path, ec);
    if (ec) {
        LOG_ERROR(Render, "ShaderDiscovery: Failed to rename {} to {}: {}",
                  temp_path.string(), target_path.string(), ec.message());
        std::filesystem::remove(temp_path, ec);
    } else {
        LOG_INFO(Render, "ShaderDiscovery: Successfully wrote inventory to {}", target_path.string());
    }
}

void SerializeShaders(std::stringstream& ss, const std::vector<DiscoveredShader>& shaders) {
    ss << "  \"shaders\": [\n";
    for (size_t i = 0; i < shaders.size(); ++i) {
        const auto& s = shaders[i];
        char hex_hash[32];
        std::snprintf(hex_hash, sizeof(hex_hash), "0x%016llx", static_cast<unsigned long long>(s.pgm_hash));
        ss << "    {\n";
        ss << "      \"pgm_hash\": \"" << hex_hash << "\",\n";
        ss << "      \"hw_stage\": \"" << HwStageToString(s.hw_stage) << "\",\n";
        ss << "      \"sw_stage\": \"" << SwStageToString(s.sw_stage) << "\",\n";
        ss << "      \"perm_idx\": " << s.perm_idx << ",\n";
        ss << "      \"code_size_bytes\": " << s.code_size_bytes << ",\n";
        ss << "      \"spv_size_words\": " << s.spv_size_words << ",\n";
        ss << "      \"compile_time_ms\": " << (s.compile_time_ns / 1000000.0) << ",\n";
        ss << "      \"has_fetch_shader\": " << (s.has_fetch_shader ? "true" : "false") << ",\n";
        ss << "      \"motion_vectors\": " << (s.motion_vectors ? "true" : "false") << ",\n";
        ss << "      \"location\": \"" << (s.location.empty() ? "Startup / Cache" : s.location) << "\",\n";
        ss << "      \"hit_count\": " << s.hit_count << "\n";
        ss << "    }" << (i + 1 < shaders.size() ? "," : "") << "\n";
    }
    ss << "  ],\n";
}

void SerializePipelines(std::stringstream& ss, const std::vector<DiscoveredPipeline>& pipelines) {
    ss << "  \"pipelines\": [\n";
    for (size_t i = 0; i < pipelines.size(); ++i) {
        const auto& p = pipelines[i];
        char hex_hash[32];
        std::snprintf(hex_hash, sizeof(hex_hash), "0x%016llx", static_cast<unsigned long long>(p.pipeline_hash));
        ss << "    {\n";
        ss << "      \"pipeline_hash\": \"" << hex_hash << "\",\n";
        ss << "      \"is_compute\": " << (p.is_compute ? "true" : "false") << ",\n";
        ss << "      \"stage_hashes\": [";
        for (size_t j = 0; j < p.stage_hashes.size(); ++j) {
            char stage_hex[32];
            std::snprintf(stage_hex, sizeof(stage_hex), "\"0x%016llx\"", static_cast<unsigned long long>(p.stage_hashes[j]));
            ss << stage_hex << (j + 1 < p.stage_hashes.size() ? ", " : "");
        }
        ss << "],\n";
        ss << "      \"num_color_attachments\": " << p.num_color_attachments << ",\n";
        ss << "      \"num_samples\": " << static_cast<uint32_t>(p.num_samples) << ",\n";
        ss << "      \"compile_time_ms\": " << (p.compile_time_ns / 1000000.0) << ",\n";
        ss << "      \"is_async\": " << (p.is_async ? "true" : "false") << ",\n";
        ss << "      \"is_preloaded\": " << (p.is_preloaded ? "true" : "false") << ",\n";
        ss << "      \"location\": \"" << (p.location.empty() ? "Startup / Cache" : p.location) << "\",\n";
        ss << "      \"hit_count\": " << p.hit_count << "\n";
        ss << "    }" << (i + 1 < pipelines.size() ? "," : "") << "\n";
    }
    ss << "  ]\n";
}

} // namespace

void DumpInventory(const std::string& custom_dir) {
    if (custom_dir.empty() && !std::getenv("BB_SHADER_DISCOVERY_DUMP") && !std::getenv("BB_SHADER_INVENTORY")) {
        return;
    }
    const auto stats = GetStats();
    if (stats.unique_shaders == 0 && stats.unique_pipelines == 0) {
        return;
    }

    const std::string serial{Common::ElfInfo::Instance().GameSerial()};
    std::filesystem::path out_dir;
    if (!custom_dir.empty()) {
        out_dir = custom_dir;
    } else {
        out_dir = Common::FS::GetUserPath(Common::FS::PathType::CacheDir) / serial;
    }

    const auto shaders = GetShaders();
    const auto pipelines = GetPipelines();

    std::stringstream ss;
    ss << "{\n";
    ss << "  \"schema_version\": 1,\n";
    ss << "  \"title_id\": \"" << serial << "\",\n";
    ss << "  \"timestamp\": \"" << GetTimestampIso() << "\",\n";
    ss << "  \"statistics\": {\n";
    ss << "    \"unique_shaders\": " << stats.unique_shaders << ",\n";
    ss << "    \"unique_pipelines\": " << stats.unique_pipelines << ",\n";
    ss << "    \"shader_compilations\": " << stats.shader_compilations << ",\n";
    ss << "    \"shader_hits\": " << stats.shader_hits << ",\n";
    ss << "    \"pipeline_compilations\": " << stats.pipeline_compilations << ",\n";
    ss << "    \"pipeline_hits\": " << stats.pipeline_hits << ",\n";
    ss << "    \"total_shader_compile_ms\": " << (stats.total_shader_compile_ns / 1000000.0) << ",\n";
    ss << "    \"total_pipeline_compile_ms\": " << (stats.total_pipeline_compile_ns / 1000000.0) << "\n";
    ss << "  },\n";

    SerializeShaders(ss, shaders);
    SerializePipelines(ss, pipelines);
    ss << "}\n";

    const std::string json_str = ss.str();
    WriteJsonAtomic(out_dir / "shader_inventory.json", json_str);

    const auto log_dir = Common::FS::GetUserPath(Common::FS::PathType::LogDir);
    if (std::filesystem::exists(log_dir)) {
        WriteJsonAtomic(log_dir / "shader_inventory.json", json_str);
    }
}

} // namespace BbShaderDiscovery
