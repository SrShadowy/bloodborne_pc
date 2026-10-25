// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: runtime shader and pipeline discovery inventory (Phase 2).
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <span>

namespace BbShaderDiscovery {

struct DiscoveredShader {
    uint64_t pgm_hash = 0;
    uint32_t hw_stage = 0;
    uint32_t sw_stage = 0;
    uint32_t perm_idx = 0;
    uint32_t code_size_bytes = 0;
    uint32_t spv_size_words = 0;
    uint64_t compile_time_ns = 0;
    bool has_fetch_shader = false;
    bool motion_vectors = false;
    uint64_t hit_count = 0;
    std::string location;
};

struct DiscoveredPipeline {
    uint64_t pipeline_hash = 0;
    bool is_compute = false;
    std::vector<uint64_t> stage_hashes;
    uint32_t num_color_attachments = 0;
    uint8_t num_samples = 1;
    uint64_t compile_time_ns = 0;
    bool is_async = false;
    bool is_preloaded = false;
    uint64_t hit_count = 0;
    std::string location;
};

struct DiscoveryStats {
    uint32_t unique_shaders = 0;
    uint32_t unique_pipelines = 0;
    uint64_t shader_compilations = 0;
    uint64_t shader_hits = 0;
    uint64_t pipeline_compilations = 0;
    uint64_t pipeline_hits = 0;
    uint64_t total_shader_compile_ns = 0;
    uint64_t total_pipeline_compile_ns = 0;
};

struct CompileEvent {
    uint64_t timestamp_ms = 0;
    uint64_t hash = 0;
    uint32_t stage = 0;
    uint32_t perm_idx = 0;
    uint32_t size_bytes = 0;
    uint64_t duration_ns = 0;
    bool is_pipeline = false;
    bool is_compute = false;
    bool is_new = false;
    std::string location;
};

void RecordShaderCompile(uint64_t pgm_hash, uint32_t hw_stage, uint32_t sw_stage,
                         uint32_t perm_idx, uint32_t code_bytes, uint32_t spv_words,
                         uint64_t duration_ns, bool has_fetch, bool motion_vectors);

void RecordShaderHit(uint64_t pgm_hash, uint32_t perm_idx);

void RecordPipelineCompile(uint64_t pipeline_hash, bool is_compute,
                           std::span<const size_t> stage_hashes,
                           uint32_t num_color_attachments, uint8_t num_samples,
                           uint64_t duration_ns, bool is_async, bool is_preloaded);

void RecordPipelineHit(uint64_t pipeline_hash);

void DumpInventory(const std::string& directory = "");

DiscoveryStats GetStats();
std::vector<DiscoveredShader> GetShaders();
std::vector<DiscoveredPipeline> GetPipelines();
std::vector<CompileEvent> GetRecentEvents();
void ClearEvents();
uint32_t GetNewShaderCount();
uint32_t GetNewPipelineCount();

void Reset();
void ReportProgress(uint32_t current, uint32_t total);
void GetProgress(uint32_t& current, uint32_t& total);

void SetEnabled(bool enabled);
bool IsEnabled();

} // namespace BbShaderDiscovery

// Hook declarations for upstream C++ integration
void extra_shader_discovery_record_compile(uint64_t pgm_hash, uint32_t hw_stage, uint32_t sw_stage,
                                          uint32_t perm_idx, uint32_t code_bytes, uint32_t spv_words,
                                          uint64_t duration_ns, bool has_fetch, bool motion_vectors);

void extra_shader_discovery_record_shader_hit(uint64_t pgm_hash, uint32_t perm_idx);

void extra_shader_discovery_record_pipeline(uint64_t pipeline_hash, bool is_compute,
                                            std::span<const size_t> stage_hashes,
                                            uint32_t num_color_attachments, uint8_t num_samples,
                                            uint64_t duration_ns, bool is_async, bool is_preloaded);

void extra_shader_discovery_record_pipeline_hit(uint64_t pipeline_hash);

void extra_shader_discovery_dump_inventory();

void extra_shader_discovery_report_progress(uint32_t current, uint32_t total);
