// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the FPS HUD's metric list -- FPS/frametime, GPU, CPU, VRAM (+ the memory clock), RAM and
// the upscaler with its render/output resolution. Every entry is on only when its setting is.
#include "bbport_fps_hud_internal.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "bbport_perf_metrics.h"
#include "bbport_strings.h"

namespace BbFpsHud::detail {
namespace {

constexpr double kBytesPerGb = 1024.0 * 1024.0 * 1024.0;
constexpr std::uint64_t kBytesPerGiB = 1024ULL * 1024 * 1024;

/// The "FSR 4.1.1 (FP8)" label, or empty when no upscaler is on.
std::string UpscalerLabel(const BbSettings::Values& s) {
    switch (s.upscaler) {
    case BbSettings::UpscalerFsr3:
        return "FSR 3.1";
    case BbSettings::UpscalerFsr4:
        return "FSR 4";
    case BbSettings::UpscalerFsr411:
        return s.fsr411_fp8.load() ? "FSR 4.1.1 (FP8)" : "FSR 4.1.1";
    case BbSettings::UpscalerTaa:
        return "TAA";
    case BbSettings::UpscalerDlss:
        return "DLSS";
    default:
        return {};
    }
}

/// "2560x1440 < 1440x810": the output size, then the internal render size.
std::string ResolutionText(const BbSettings::Values& s, bool has_upscaler) {
    if (!has_upscaler) {
        return {};
    }
    const auto out = BbSettings::CurrentOutput();
    const int render_w = s.active_render_width.load(std::memory_order_relaxed);
    const int render_h = s.active_render_height.load(std::memory_order_relaxed);
    const int final_w = out.width > 0 ? out.width : 1920;
    const int final_h = out.height > 0 ? out.height : 1080;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%dx%d < %dx%d", final_w, final_h,
                  render_w > 0 ? render_w : final_w, render_h > 0 ? render_h : final_h);
    return buf;
}

/// "%.0f \ %.0f FPS (12.3 ms)": the original and the generated framerate, when frame generation
/// is on.
std::string FpsText(const HudState& state) {
    const BbSettings::Values& s = state.settings;
    const bool show_fps = s.fps_hud_show_fps.load();
    const bool show_ft = s.fps_hud_show_frametime.load();
    const char* const ms_unit = BbStrings::Get(BbStrings::StringId::FpsMs, state.lang);

    char buf[128] = {};
    if (state.fg_active) {
        if (show_fps && show_ft) {
            std::snprintf(buf, sizeof(buf), "%.0f \\ %.0f FPS (%.1f %s)", state.orig_fps,
                          state.total_fps, state.display_ms, ms_unit);
        } else if (show_fps) {
            std::snprintf(buf, sizeof(buf), "%.0f \\ %.0f FPS", state.orig_fps, state.total_fps);
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f %s", state.display_ms, ms_unit);
        }
        return buf;
    }

    const float fps = state.total_fps > 0.0f ? state.total_fps : state.orig_fps;
    if (show_fps && show_ft) {
        std::snprintf(buf, sizeof(buf), "%.0f FPS (%.1f %s)", fps, state.display_ms, ms_unit);
    } else if (show_fps) {
        std::snprintf(buf, sizeof(buf), "%.0f FPS", fps);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f %s", state.display_ms, ms_unit);
    }
    return buf;
}

/// The GPU's memory: with GTT when the host imports it, else the process projection and the
/// system's total when they differ.
std::string VramText(const BbPerfMetrics::MetricsSnapshot& metrics) {
    const std::uint64_t vram = metrics.vram_bytes;
    const std::uint64_t gtt = metrics.gtt_bytes;
    const std::uint64_t process_gpu = vram + gtt;
    const std::uint64_t system_vram = metrics.vram_sys_bytes;
    char buf[64] = {};

    if (gtt > 0) {
        // APU or zero-copy host memory: the process' whole GPU memory, then the GTT share.
        if (process_gpu >= kBytesPerGiB) {
            std::snprintf(buf, sizeof(buf), "%.2f GB (+GTT %lluM)", double(process_gpu) / kBytesPerGb,
                          static_cast<unsigned long long>(gtt >> 20));
        } else {
            std::snprintf(buf, sizeof(buf), "%llu MB (+GTT %lluM)",
                          static_cast<unsigned long long>(process_gpu >> 20),
                          static_cast<unsigned long long>(gtt >> 20));
        }
    } else if (vram >= kBytesPerGiB) {
        if (system_vram > vram) {
            std::snprintf(buf, sizeof(buf), "%.2f GB (Tot: %.2f)", double(vram) / kBytesPerGb,
                          double(system_vram) / kBytesPerGb);
        } else {
            std::snprintf(buf, sizeof(buf), "%.2f GB", double(vram) / kBytesPerGb);
        }
    } else if (vram > 0) {
        if (system_vram > vram) {
            std::snprintf(buf, sizeof(buf), "%llu MB (Tot: %.2f GB)",
                          static_cast<unsigned long long>(vram >> 20),
                          double(system_vram) / kBytesPerGb);
        } else {
            std::snprintf(buf, sizeof(buf), "%llu MB", static_cast<unsigned long long>(vram >> 20));
        }
    } else if (system_vram > 0) {
        std::snprintf(buf, sizeof(buf), "%.2f GB", double(system_vram) / kBytesPerGb);
    } else {
        std::snprintf(buf, sizeof(buf), "0 MB");
    }
    return buf;
}

/// The process' RSS, with the system's used/total when the platform reports them.
std::string RamText(const BbPerfMetrics::MetricsSnapshot& metrics) {
    const double process_gb = double(metrics.ram_rss_bytes) / kBytesPerGb;
    char buf[64] = {};

    if (metrics.ram_sys_total_bytes > 0) {
        const double used_gb = double(metrics.ram_sys_used_bytes) / kBytesPerGb;
        const double total_gb = double(metrics.ram_sys_total_bytes) / kBytesPerGb;
        if (process_gb >= 1.0) {
            std::snprintf(buf, sizeof(buf), "%.2f GB (Sys: %.1f/%.0fG)", process_gb, used_gb, total_gb);
        } else {
            std::snprintf(buf, sizeof(buf), "%llu MB (Sys: %.1f/%.0fG)",
                          static_cast<unsigned long long>(metrics.ram_rss_bytes >> 20), used_gb,
                          total_gb);
        }
        return buf;
    }
    if (process_gb >= 1.0) {
        std::snprintf(buf, sizeof(buf), "%.2f GB", process_gb);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu MB", static_cast<unsigned long long>(metrics.ram_rss_bytes >> 20));
    }
    return buf;
}

} // namespace

Metrics BuildMetrics(const HudState& state) {
    const BbSettings::Values& s = state.settings;
    Metrics out;
    std::string upscaler = UpscalerLabel(s);
    if (state.fg_active) {
        upscaler += upscaler.empty() ? "FG" : " + FG";
    }
    out.resolution = ResolutionText(s, !upscaler.empty());
    if (!state.show_fps) {
        return out;
    }

    const auto metrics = BbPerfMetrics::GetSnapshot();
    if (s.fps_hud_show_fps.load() || s.fps_hud_show_frametime.load()) {
        out.entries.push_back(MetricEntry{"FPS", IM_COL32(250, 250, 250, 255), FpsText(state), true});
    }

    if (s.fps_hud_show_gpu.load()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0f%%", metrics.gpu_percent);
        out.entries.push_back(MetricEntry{"GPU", IM_COL32(80, 225, 120, 255), buf, false});
    }
    if (s.fps_hud_show_cpu.load()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0f%%", metrics.cpu_percent);
        out.entries.push_back(MetricEntry{"CPU", IM_COL32(80, 190, 255, 255), buf, false});
    }
    if (s.fps_hud_show_vram.load()) {
        out.entries.push_back(MetricEntry{"VRAM", IM_COL32(230, 130, 255, 255), VramText(metrics), false});
        if (metrics.mem_clock_mhz > 0) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%u MHz", metrics.mem_clock_mhz);
            out.entries.push_back(MetricEntry{"MEM", IM_COL32(240, 160, 255, 255), buf, false});
        }
    }
    if (s.fps_hud_show_ram.load()) {
        out.entries.push_back(MetricEntry{"RAM", IM_COL32(255, 185, 75, 255), RamText(metrics), false});
    }
    if (!upscaler.empty()) {
        out.entries.push_back(MetricEntry{"UP", IM_COL32(180, 235, 255, 255), upscaler, true});
    }
    return out;
}

} // namespace BbFpsHud::detail
