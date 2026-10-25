// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: offline pipeline reconstruction with Monster Hunter Rise style splash screen
// and rigorous technical metric auditing (Cold vs Warm, Portable vs Device-Specific).
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "common/elf_info.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "cp/command_processor.h"
#include "extra/core/shader_precompile/bbport_shader_discovery.h"
#include "extra/core/shader_precompile/bbport_shader_pack.h"
#include "extra/core/shader_precompile/bbport_static_catalog.h"
#include "extra/core/shader_precompile/bbport_vk_pipeline_cache.h"
#include "extra/ui/splash/bbport_shader_splash.h"
#include "games/game_profile.h"
#include "render/pipeline_cache.h"
#include "render/scheduler.h"
#include "vulkan/instance.h"

int main(int argc, char** argv) {
    std::printf("================================================================================\n");
    std::printf("BBPort — Offline Pipeline Reconstruction & Shader Metrics Audit\n");
    std::printf("================================================================================\n");

    const char* user_dir = "user";
    bool headless = false;
    bool force_cold = false;
    bool export_report = (std::getenv("BB_SHADER_REPORT") != nullptr);

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless") {
            headless = true;
        } else if (arg == "--cold") {
            force_cold = true;
        } else if (arg == "--warm") {
            force_cold = false;
        } else if (arg == "--report" || arg == "--diag") {
            export_report = true;
        } else if (arg == "--prune") {
            setenv("BB_PRUNE_STALE_CACHE", "1", 1);
        } else if (argv[i][0] != '-') {
            user_dir = argv[i];
        }
    }

    setenv("BB_GPU_USER_DIR", user_dir, 1);
    setenv("BB_SHADER_DISCOVERY", "1", 1);
    setenv("BB_GUEST_IN_PLACE", "1", 1);
    setenv("BB_LAYER_MEMORY", "1", 1);

    if (force_cold) {
        setenv("BB_SHADER_COLD_RUN", "1", 1);
        setenv("MESA_SHADER_CACHE_DISABLE", "true", 1);
    } else {
        unsetenv("BB_SHADER_COLD_RUN");
        unsetenv("MESA_SHADER_CACHE_DISABLE");
    }

    Common::ElfInfo::Instance().Set("CUSA03173", "Bloodborne", 0x01700000, 0);
    Game::Select("CUSA03173");

    const auto cache_dir = Common::FS::GetUserPath(Common::FS::PathType::CacheDir) / "CUSA03173";
    std::filesystem::create_directories(cache_dir);

    auto compile_task = [&]() -> bool {
        // Phase 1: I/O & Master Cache restore
        const auto t_io_start = std::chrono::steady_clock::now();
        std::printf("[Audit 1/4] Checking and restoring master Hunter's Grimoire assets...\n");
        BbShaderPack::RestoreMasterCacheIfEmpty(cache_dir);

        size_t key_count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(cache_dir)) {
            if (entry.path().extension() == ".key") {
                key_count++;
            }
        }
        const auto t_io_end = std::chrono::steady_clock::now();
        const double io_sec = std::chrono::duration_cast<std::chrono::microseconds>(t_io_end - t_io_start).count() / 1000000.0;
        std::printf("  -> Found %zu pipeline keys on disk (I/O scan: %.3f s).\n", key_count, io_sec);

        // Phase 2: Vulkan context initialization
        const auto t_vk_init_start = std::chrono::steady_clock::now();
        std::printf("[Audit 2/4] Initializing headless Vulkan instance (%s mode)...\n",
                    force_cold ? "COLD COMPILE" : "WARM CACHE");
        Vulkan::Instance instance(-1, false);
        Vulkan::Scheduler scheduler(instance, false);
        BbCp::CommandProcessor liverpool;
        const auto t_vk_init_end = std::chrono::steady_clock::now();
        const double vk_init_sec = std::chrono::duration_cast<std::chrono::microseconds>(t_vk_init_end - t_vk_init_start).count() / 1000000.0;

        // Phase 3: PipelineCache initialization & WarmUp()
        std::printf("[Audit 3/4] Rebuilding pipelines in Vulkan driver...\n");
        const auto t_warmup_start = std::chrono::steady_clock::now();
        Vulkan::PipelineCache pipeline_cache(instance, scheduler, &liverpool, 16);
        const auto t_warmup_end = std::chrono::steady_clock::now();
        const double warmup_sec = std::chrono::duration_cast<std::chrono::microseconds>(t_warmup_end - t_warmup_start).count() / 1000000.0;

        const auto t_sync_start = std::chrono::steady_clock::now();
        pipeline_cache.Sync();
        const auto t_sync_end = std::chrono::steady_clock::now();
        const double sync_sec = std::chrono::duration_cast<std::chrono::microseconds>(t_sync_end - t_sync_start).count() / 1000000.0;

        std::printf("PRECOMPILE_PROGRESS: 60/100 (60%%)\n");
        std::fflush(stdout);

        // Extract audited metrics
        const auto driver_status = BbVkPipelineCache::GetDriverCacheStatus();
        uint32_t preloaded = 0, total_keys = 0;
        BbShaderDiscovery::GetProgress(preloaded, total_keys);
        const uint32_t stale_keys = (total_keys > preloaded) ? (total_keys - preloaded) : 0;
        const auto stats = BbShaderDiscovery::GetStats();
        const double avg_us_per_pipe = preloaded > 0 ? ((warmup_sec * 1000000.0) / preloaded) : 0.0;

        std::printf("\n================================================================================\n");
        std::printf("                   AUDITED PIPELINE RECONSTRUCTION REPORT                       \n");
        std::printf("================================================================================\n");
        std::printf("Target GPU / Driver:        %s (Vendor: 0x%04x, Device: 0x%04x)\n",
                    driver_status.device_name.empty() ? "Vulkan Device" : driver_status.device_name.c_str(),
                    driver_status.vendor_id, driver_status.device_id);
        std::printf("Execution Mode:             %s\n",
                    driver_status.cold_forced ? "COLD COMPILE (Driver Cache & Mesa Cache Disabled)"
                                              : (driver_status.loaded ? "WARM CACHE (Driver Cache Hit)" : "COLD COMPILE (No Driver Cache File)"));
        std::printf("Driver Cache Blob:          %s (%zu bytes)\n",
                    driver_status.loaded ? "LOADED [HIT]" : "NOT LOADED [MISS]", driver_status.size_bytes);
        std::printf("--------------------------------------------------------------------------------\n");
        std::printf("TIMING BREAKDOWN:\n");
        std::printf("  1. Disk & Keys Scan (T_io):        %8.3f s\n", io_sec);
        std::printf("  2. Vulkan Device Init:             %8.3f s\n", vk_init_sec);
        std::printf("  3. Driver WarmUp() Compilation:    %8.3f s\n", warmup_sec);
        std::printf("  4. Driver Cache Serialization:     %8.3f s\n", sync_sec);
        std::printf("--------------------------------------------------------------------------------\n");
        std::printf("PIPELINE HEALTH & SUCCESS METRICS:\n");
        std::printf("  Total Keys Inspected:              %u\n", total_keys > 0 ? total_keys : static_cast<uint32_t>(key_count));
        std::printf("  Successfully Rebuilt Pipelines:    %u (%.1f%%)\n",
                    preloaded, total_keys > 0 ? (100.0 * preloaded / total_keys) : 0.0);
        std::printf("  Stale / Damaged Keys Rejected:     %u (%.1f%% - conflicting permutations/hashes)\n",
                    stale_keys, total_keys > 0 ? (100.0 * stale_keys / total_keys) : 0.0);
        std::printf("  Average Latency per Pipeline:      %.1f µs (%.3f ms)\n",
                    avg_us_per_pipe, avg_us_per_pipe / 1000.0);
        std::printf("  Unique Shaders Registered:         %u\n", stats.unique_shaders);
        std::printf("--------------------------------------------------------------------------------\n");

        // Phase 4: Packing Hunter's Grimoire (Portable 3-File Archive)
        std::printf("[Audit 4/4] Consolidating portable Grimoire assets...\n");
        const auto t_pack_start = std::chrono::steady_clock::now();
        const bool packed_ok = BbShaderPack::PackDirectory(cache_dir, "CUSA03173");
        const auto t_pack_end = std::chrono::steady_clock::now();
        const double pack_sec = std::chrono::duration_cast<std::chrono::microseconds>(t_pack_end - t_pack_start).count() / 1000000.0;

        if (packed_ok) {
            const auto pstats = BbShaderPack::GetArchiveStats(cache_dir);
            const bool verified = BbShaderPack::VerifyArchive(cache_dir);
            std::printf("PORTABLE ARTIFACTS (Hunter's Grimoire - Agnostic to GPU):\n");
            std::printf("  [1] Index:    %s (%zu bytes, %u shaders, %u pipelines)\n",
                        BbShaderPack::ARCHIVE_INDEX_NAME, pstats.index_file_size, pstats.shader_count, pstats.pipeline_count);
            std::printf("  [2] Shaders:  %s (%zu bytes - SPIR-V & GCN blobs)\n",
                        BbShaderPack::ARCHIVE_SHADERS_NAME, pstats.shaders_file_size);
            std::printf("  [3] Metadata: %s (%zu bytes - Pipeline state records)\n",
                        BbShaderPack::ARCHIVE_METADATA_NAME, pstats.meta_file_size);
            std::printf("  Verification: %s (Packaging Time: %.3f s)\n",
                        verified ? "PASSED [OK]" : "FAILED [ERROR]", pack_sec);
        } else {
            std::printf("Failed to pack portable Grimoire archive.\n");
        }

        if (export_report) {
            const auto diag_report = cache_dir / "shader_diagnostic_report.json";
            if (BbStaticCatalog::ExportDiagnosticReport(diag_report)) {
                std::printf("DIAGNOSTIC TELEMETRY REPORT:\n");
                std::printf("  JSON Report:  %s (%zu bytes)\n",
                            diag_report.string().c_str(), std::filesystem::file_size(diag_report));
            }
        }
        std::printf("================================================================================\n");

        std::printf("PRECOMPILE_PROGRESS: 100/100 (100%%)\n");
        std::printf("PRECOMPILE_FINISHED: 100%% (Rebuilt %u pipelines successfully)\n", preloaded);
        std::fflush(stdout);
        return packed_ok;
    };

    BbShaderSplash::SplashOptions splash_opts{
        .cache_dir = cache_dir,
        .target_pipelines = 801,
        .headless = headless,
        .splash_image_path = "packaging/splash_bloodborne.png"
    };

    bool ok = BbShaderSplash::Run(splash_opts, compile_task);
    return ok ? 0 : 1;
}
