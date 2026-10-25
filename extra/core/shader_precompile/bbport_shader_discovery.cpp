// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_shader_discovery.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <unordered_map>
#include "bbport_static_catalog.h"
#include "extra/contact/game_data/bbport_game_data.h"

namespace BbShaderDiscovery {

namespace {

std::string CurrentLocation() {
    auto snap = BbGameData::GetSnapshot();
    return (snap.in_game && !snap.map_name_en.empty()) ? snap.map_name_en : "Startup / Cache";
}

static std::atomic<bool> s_enabled{false};

struct State {
    std::mutex mutex;
    std::unordered_map<uint64_t, DiscoveredShader> shaders;
    std::unordered_map<uint64_t, DiscoveredPipeline> pipelines;
    std::deque<CompileEvent> events;
    uint32_t new_shaders_count = 0;
    uint32_t new_pipelines_count = 0;
    DiscoveryStats stats{};
    bool verbose_logging = false;
    bool initialized = false;

    void InitIfNeeded() {
        if (initialized) return;
        const char* env = std::getenv("BB_SHADER_DISCOVERY");
        if (env && env[0] != '0') {
            verbose_logging = true;
            s_enabled.store(true, std::memory_order_relaxed);
        }
        initialized = true;
        BbStaticCatalog::AutoLoad("dist/user/cache");
    }
};

State& GetState() {
    static State state;
    return state;
}

uint64_t MakeShaderKey(uint64_t pgm_hash, uint32_t perm_idx) {
    // Deterministic 64-bit combination of program hash and permutation index
    return pgm_hash ^ (static_cast<uint64_t>(perm_idx) + 0x9e3779b97f4a7c15ULL +
                       (pgm_hash << 6) + (pgm_hash >> 2));
}

} // namespace

void SetEnabled(bool enabled) { s_enabled.store(enabled, std::memory_order_relaxed); }
bool IsEnabled() { return s_enabled.load(std::memory_order_relaxed); }

void RecordShaderCompile(uint64_t pgm_hash, uint32_t hw_stage, uint32_t sw_stage,
                         uint32_t perm_idx, uint32_t code_bytes, uint32_t spv_words,
                         uint64_t duration_ns, bool has_fetch, bool motion_vectors) {
    if (!s_enabled.load(std::memory_order_relaxed)) return;
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    state.InitIfNeeded();

    const uint64_t key = MakeShaderKey(pgm_hash, perm_idx);
    auto [it, is_new] = state.shaders.try_emplace(key);
    if (is_new) {
        BbStaticCatalog::RecordRuntimeShader(pgm_hash);
        DiscoveredShader& shader = it->second;
        shader.pgm_hash = pgm_hash;
        shader.hw_stage = hw_stage;
        shader.sw_stage = sw_stage;
        shader.perm_idx = perm_idx;
        shader.code_size_bytes = code_bytes;
        shader.spv_size_words = spv_words;
        shader.compile_time_ns = duration_ns;
        shader.has_fetch_shader = has_fetch;
        shader.motion_vectors = motion_vectors;
        shader.hit_count = 1;
        shader.location = CurrentLocation();

        state.stats.unique_shaders++;
        state.stats.shader_compilations++;
        state.stats.total_shader_compile_ns += duration_ns;

        const auto now_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        CompileEvent evt{.timestamp_ms = now_ms, .hash = pgm_hash, .stage = hw_stage,
                         .perm_idx = perm_idx, .size_bytes = code_bytes > 0 ? code_bytes : (spv_words * 4),
                         .duration_ns = duration_ns, .is_pipeline = false, .is_compute = (hw_stage == 5),
                         .is_new = (duration_ns > 0), .location = shader.location};
        if (evt.is_new) state.new_shaders_count++;
        state.events.push_back(evt);
        if (state.events.size() > 250) state.events.pop_front();

        if (state.verbose_logging) {
            std::printf("ShaderDiscovery: Discovered shader 0x%016llx (stage %u, perm %u, %s, %.2f ms)\n",
                        static_cast<unsigned long long>(pgm_hash), hw_stage, perm_idx,
                        shader.location.c_str(), duration_ns / 1000000.0);
        }
    } else {
        it->second.hit_count++;
        state.stats.shader_hits++;
    }
}

void RecordShaderHit(uint64_t pgm_hash, uint32_t perm_idx) {
    if (!s_enabled.load(std::memory_order_relaxed)) return;
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    const uint64_t key = MakeShaderKey(pgm_hash, perm_idx);
    auto it = state.shaders.find(key);
    if (it != state.shaders.end()) it->second.hit_count++;
    state.stats.shader_hits++;
}

void RecordPipelineCompile(uint64_t pipeline_hash, bool is_compute,
                           std::span<const size_t> stage_hashes,
                           uint32_t num_color_attachments, uint8_t num_samples,
                           uint64_t duration_ns, bool is_async, bool is_preloaded) {
    if (!s_enabled.load(std::memory_order_relaxed)) return;
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    state.InitIfNeeded();

    auto [it, is_new] = state.pipelines.try_emplace(pipeline_hash);
    if (is_new) {
        DiscoveredPipeline& pipeline = it->second;
        pipeline.pipeline_hash = pipeline_hash;
        pipeline.is_compute = is_compute;
        pipeline.num_color_attachments = num_color_attachments;
        pipeline.num_samples = num_samples;
        pipeline.compile_time_ns = duration_ns;
        pipeline.is_async = is_async;
        pipeline.is_preloaded = is_preloaded;
        pipeline.hit_count = 1;
        pipeline.location = CurrentLocation();
        pipeline.stage_hashes.reserve(stage_hashes.size());
        for (size_t h : stage_hashes) {
            if (h != 0) pipeline.stage_hashes.push_back(static_cast<uint64_t>(h));
        }

        state.stats.unique_pipelines++;
        state.stats.pipeline_compilations++;
        state.stats.total_pipeline_compile_ns += duration_ns;

        const auto now_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        CompileEvent evt{.timestamp_ms = now_ms, .hash = pipeline_hash, .duration_ns = duration_ns,
                         .is_pipeline = true, .is_compute = is_compute, .is_new = !is_preloaded,
                         .location = pipeline.location};
        if (evt.is_new) state.new_pipelines_count++;
        state.events.push_back(evt);
        if (state.events.size() > 250) state.events.pop_front();

        if (state.verbose_logging && !is_preloaded) {
            std::printf("ShaderDiscovery: Discovered %s pipeline 0x%016llx (%s, %zu stages, %.2f ms%s)\n",
                        is_compute ? "compute" : "graphics", static_cast<unsigned long long>(pipeline_hash),
                        pipeline.location.c_str(), pipeline.stage_hashes.size(), duration_ns / 1000000.0,
                        is_async ? ", async" : "");
        }
    } else {
        it->second.hit_count++;
        state.stats.pipeline_hits++;
    }
}

void RecordPipelineHit(uint64_t pipeline_hash) {
    if (!s_enabled.load(std::memory_order_relaxed)) return;
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    auto it = state.pipelines.find(pipeline_hash);
    if (it != state.pipelines.end()) {
        it->second.hit_count++;
    }
    state.stats.pipeline_hits++;
}

DiscoveryStats GetStats() {
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    return state.stats;
}

std::vector<DiscoveredShader> GetShaders() {
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    std::vector<DiscoveredShader> result;
    result.reserve(state.shaders.size());
    for (const auto& [_, shader] : state.shaders) {
        result.push_back(shader);
    }
    return result;
}

std::vector<DiscoveredPipeline> GetPipelines() {
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    std::vector<DiscoveredPipeline> result;
    result.reserve(state.pipelines.size());
    for (const auto& [_, pipeline] : state.pipelines) {
        result.push_back(pipeline);
    }
    return result;
}

std::vector<CompileEvent> GetRecentEvents() {
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    return {state.events.begin(), state.events.end()};
}

void ClearEvents() {
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    state.events.clear();
}

uint32_t GetNewShaderCount() {
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    return state.new_shaders_count;
}

uint32_t GetNewPipelineCount() {
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    return state.new_pipelines_count;
}

void Reset() {
    auto& state = GetState();
    std::scoped_lock lock{state.mutex};
    state.shaders.clear();
    state.pipelines.clear();
    state.events.clear();
    state.new_shaders_count = 0;
    state.new_pipelines_count = 0;
    state.stats = {};
}

} // namespace BbShaderDiscovery

// Hook implementations
void extra_shader_discovery_record_compile(uint64_t pgm_hash, uint32_t hw_stage, uint32_t sw_stage,
                                          uint32_t perm_idx, uint32_t code_bytes, uint32_t spv_words,
                                          uint64_t duration_ns, bool has_fetch, bool motion_vectors) {
    BbShaderDiscovery::RecordShaderCompile(pgm_hash, hw_stage, sw_stage, perm_idx,
                                          code_bytes, spv_words, duration_ns, has_fetch, motion_vectors);
}

void extra_shader_discovery_record_shader_hit(uint64_t pgm_hash, uint32_t perm_idx) {
    BbShaderDiscovery::RecordShaderHit(pgm_hash, perm_idx);
}

void extra_shader_discovery_record_pipeline(uint64_t pipeline_hash, bool is_compute,
                                            std::span<const size_t> stage_hashes,
                                            uint32_t num_color_attachments, uint8_t num_samples,
                                            uint64_t duration_ns, bool is_async, bool is_preloaded) {
    BbShaderDiscovery::RecordPipelineCompile(pipeline_hash, is_compute, stage_hashes,
                                            num_color_attachments, num_samples, duration_ns, is_async, is_preloaded);
}

void extra_shader_discovery_record_pipeline_hit(uint64_t pipeline_hash) {
    BbShaderDiscovery::RecordPipelineHit(pipeline_hash);
}

void extra_shader_discovery_dump_inventory() {
    BbShaderDiscovery::DumpInventory();
}

static std::atomic<uint32_t> g_prog_cur{0}, g_prog_tot{0};

namespace BbShaderDiscovery {
void ReportProgress(uint32_t c, uint32_t t) { g_prog_cur = c; g_prog_tot = t; }
void GetProgress(uint32_t& c, uint32_t& t) { c = g_prog_cur; t = g_prog_tot; }
} // namespace BbShaderDiscovery

void extra_shader_discovery_report_progress(uint32_t c, uint32_t t) {
    BbShaderDiscovery::ReportProgress(c, t);
}

