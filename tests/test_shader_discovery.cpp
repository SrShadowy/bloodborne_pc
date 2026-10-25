// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "extra/core/shader_precompile/bbport_shader_discovery.h"
#include "extra/core/shader_precompile/bbport_static_catalog.h"
#include "extra/core/shader_precompile/bbport_compile_queue.h"

int main() {
    using namespace BbShaderDiscovery;

    Reset();
    SetEnabled(true);
    auto initial_stats = GetStats();
    assert(initial_stats.unique_shaders == 0);
    assert(initial_stats.unique_pipelines == 0);

    // 1. Record Shader Compilations
    RecordShaderCompile(0x12345678ULL, 1 /* Vertex */, 3 /* Vertex */, 0 /* perm_idx */,
                        512 /* bytes */, 256 /* spv words */, 1500000ULL /* 1.5ms */,
                        true /* has_fetch */, false /* motion_vectors */);

    RecordShaderCompile(0x87654321ULL, 0 /* Fragment */, 0 /* Fragment */, 0,
                        1024, 512, 2500000ULL,
                        false, false);

    // Record a second permutation of the first shader
    RecordShaderCompile(0x12345678ULL, 1, 3, 1 /* perm 1 */,
                        540, 270, 1600000ULL,
                        true, false);

    auto stats = GetStats();
    assert(stats.unique_shaders == 3);
    assert(stats.shader_compilations == 3);
    assert(stats.shader_hits == 0);

    // 2. Record Shader Hits (deduplication & cache reuse)
    RecordShaderHit(0x12345678ULL, 0);
    RecordShaderHit(0x12345678ULL, 0);
    RecordShaderHit(0x87654321ULL, 0);

    stats = GetStats();
    assert(stats.unique_shaders == 3);
    assert(stats.shader_hits == 3);

    auto shaders = GetShaders();
    assert(shaders.size() == 3);
    for (const auto& s : shaders) {
        if (s.pgm_hash == 0x12345678ULL && s.perm_idx == 0) {
            assert(s.hit_count == 3); // 1 initial compile + 2 hits
            assert(s.has_fetch_shader == true);
        }
    }

    // 3. Record Pipeline Compilations
    const size_t vs_hash = 0x12345678ULL;
    const size_t fs_hash = 0x87654321ULL;
    const size_t stages[2] = {vs_hash, fs_hash};

    RecordPipelineCompile(0xAABBCCDDEEFF0011ULL, false /* is_compute */,
                          std::span<const size_t>{stages, 2},
                          1 /* num_color_attachments */, 1 /* num_samples */,
                          8000000ULL /* 8ms */, false /* is_async */, false /* is_preloaded */);

    // Record preloaded compute pipeline
    const size_t cs_hash = 0xCAFEBABEULL;
    RecordPipelineCompile(0xCAFEBABEULL, true /* is_compute */,
                          std::span<const size_t>{&cs_hash, 1},
                          0, 1, 0, false, true /* is_preloaded */);

    stats = GetStats();
    assert(stats.unique_pipelines == 2);
    assert(stats.pipeline_compilations == 2);
    assert(stats.pipeline_hits == 0);

    // 4. Record Pipeline Hits
    RecordPipelineHit(0xAABBCCDDEEFF0011ULL);
    RecordPipelineHit(0xAABBCCDDEEFF0011ULL);
    RecordPipelineHit(0xCAFEBABEULL);

    stats = GetStats();
    assert(stats.unique_pipelines == 2);
    assert(stats.pipeline_hits == 3);

    auto pipelines = GetPipelines();
    assert(pipelines.size() == 2);
    for (const auto& p : pipelines) {
        if (p.pipeline_hash == 0xAABBCCDDEEFF0011ULL) {
            assert(p.hit_count == 3); // 1 compile + 2 hits
            assert(p.stage_hashes.size() == 2);
            assert(!p.is_compute);
        } else if (p.pipeline_hash == 0xCAFEBABEULL) {
            assert(p.hit_count == 2); // 1 compile + 1 hit
            assert(p.is_compute);
            assert(p.is_preloaded);
        }
    }

    // 5. Dump and verify JSON file
    const std::filesystem::path test_dir = "/tmp/bbport_test_discovery";
    std::filesystem::remove_all(test_dir);
    DumpInventory(test_dir.string());

    const std::filesystem::path json_file = test_dir / "shader_inventory.json";
    assert(std::filesystem::exists(json_file));

    std::ifstream ifs(json_file);
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    assert(content.find("\"unique_shaders\": 3") != std::string::npos);
    assert(content.find("\"unique_pipelines\": 2") != std::string::npos);
    assert(content.find("\"0x0000000012345678\"") != std::string::npos);
    assert(content.find("\"0xaabbccddeeff0011\"") != std::string::npos);
    assert(content.find("\"is_preloaded\": true") != std::string::npos);

    std::filesystem::remove_all(test_dir);

    // 6. Test BbStaticCatalog
    if (std::filesystem::exists("dist/user/cache/CUSA03173/static_shader_catalog.json")) {
        assert(BbStaticCatalog::AutoLoad("dist/user/cache/CUSA03173"));
        assert(BbStaticCatalog::IsLoaded());
        assert(BbStaticCatalog::GetTotalStaticShaders() == 4376);
        assert(BbStaticCatalog::GetTotalStaticInstances() == 20218);

        // Verify a known static shader (0x6320bb24 from gxshader.shaderbnd.dcx)
        assert(BbStaticCatalog::IsKnownStatic(0x000000006320bb24ULL));
        BbStaticCatalog::StaticShaderInfo sinfo{};
        assert(BbStaticCatalog::FindStaticShader(0x000000006320bb24ULL, sinfo));
        assert(sinfo.stage == "Vertex");

        BbStaticCatalog::RecordRuntimeShader(0x000000006320bb24ULL);
        assert(BbStaticCatalog::GetDiscoveredStaticCount() >= 1);
        assert(BbStaticCatalog::GetStaticCoveragePercentage() > 0.0f);
        std::printf("test_shader_discovery: static catalog coverage: %.2f%%\n",
                    BbStaticCatalog::GetStaticCoveragePercentage());
    }

    // 7. Test BbCompileQueue
    BbCompileQueue::Init(2);
    assert(BbCompileQueue::IsRunning());
    BbCompileQueue::EnqueueTask(0x11223344ULL, 0, "Central Yharnam");
    BbCompileQueue::WaitUntilIdle();
    auto qstats = BbCompileQueue::GetStats();
    assert(qstats.discovered_count == 1);
    assert(qstats.translated_count == 1);
    assert(qstats.validated_count == 1);
    assert(qstats.ready_count == 1);
    assert(qstats.verified_count == 1);
    assert(qstats.failed_count == 0);
    BbCompileQueue::Shutdown();
    assert(!BbCompileQueue::IsRunning());

    std::printf("test_shader_discovery: ALL ASSERTIONS PASSED!\n");
    return 0;
}
