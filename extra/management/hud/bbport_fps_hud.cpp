// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the FPS HUD -- the window in a corner with the FPS/frametime, GPU, CPU, VRAM, RAM and
// upscaler lines, laid out as a row or a two-column table. Its metrics are built in
// bbport_fps_hud_metrics.cpp and its graphics drawn in bbport_fps_hud_graph.cpp.
#include "bbport_fps_hud.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string_view>

#include "bb_env.h"
#include "bbport_fps_hud_internal.h"
#include "bbport_perf_metrics.h"
#include "bbport_settings.h"
#include "bbport_shader_tracker.h"
#include "ui_strings.h"
#include <imgui.h>

namespace BbFpsHud {
namespace {

using detail::HudState;
using detail::MetricEntry;
using detail::Metrics;

/// The HUD's padding from the viewport's work area, before the user's scale.
constexpr float kViewportPad = 12.0f;
/// The gap between two metrics, and between a metric's tag and its value.
constexpr float kMetricGap = 6.0f;
constexpr float kTagGap = 4.0f;

/// The settings that show the shader-building indicator, when the environment allows it.
bool ShadersShown(const BbSettings::Values& s, bool compiling) {
    static const bool env_enabled = !bb_env_off("BB_SHADER_PROGRESS");
    return compiling && env_enabled && s.fps_hud_show_shaders.load(std::memory_order_relaxed);
}

/// The shader indicator's fade-out over the 400 ms after compiling stops.
float CompileAlpha(bool compiling) {
    if (!compiling) {
        return 1.0f;
    }
    const std::uint32_t decay_ms = BbShaderTracker::GetDecayElapsedMs();
    if (decay_ms >= 800) {
        return 0.0f;
    }
    if (decay_ms > 400) {
        return 1.0f - (float(decay_ms - 400) / 400.0f);
    }
    return 1.0f;
}

/// Where the window goes: 0 top-right (the default), 1 top-left, 2 bottom-right, 3 bottom-left.
void PlaceWindow(int position, float pad, ImVec2& pos, ImVec2& pivot) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float left = viewport->WorkPos.x + pad;
    const float top = viewport->WorkPos.y + pad;
    const float right = viewport->WorkPos.x + viewport->WorkSize.x - pad;
    const float bottom = viewport->WorkPos.y + viewport->WorkSize.y - pad;

    switch (position) {
    case 1:
        pos = ImVec2(left, top);
        pivot = ImVec2(0.0f, 0.0f);
        break;
    case 2:
        pos = ImVec2(right, bottom);
        pivot = ImVec2(1.0f, 1.0f);
        break;
    case 3:
        pos = ImVec2(left, bottom);
        pivot = ImVec2(0.0f, 1.0f);
        break;
    case 0:
    default:
        pos = ImVec2(right, top);
        pivot = ImVec2(1.0f, 0.0f);
        break;
    }
}

/// The shaders line: its tag, and "(N)" while N shaders are still building.
void ShadersLine(const HudState& state, unsigned active) {
    const char* const tag = UI::L("Shaders", "Shaders", "Шейдеры");
    if (active > 0) {
        ImGui::Text("%s (%u)", tag, active);
    } else {
        ImGui::TextUnformatted(tag);
    }
}

void SpinnerAtCursor(const HudState& state, ImDrawList* draw_list) {
    detail::DrawShaderSpinner(draw_list, ImGui::GetCursorScreenPos(), state);
    ImGui::Dummy(ImVec2(detail::SpinnerRadius(state) * 2.0f, ImGui::GetTextLineHeight()));
}

/// The row layout: every metric separated by "|", the upscaler carrying its resolution.
void RenderRow(const HudState& state, const Metrics& metrics, unsigned active, ImDrawList* draw_list) {
    const float gap = kMetricGap * state.user_scale;
    const auto separator = [&] {
        ImGui::SameLine(0.0f, gap);
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 160, 160, 140));
        ImGui::TextUnformatted("|");
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, gap);
    };

    for (std::size_t i = 0; i < metrics.entries.size(); ++i) {
        const MetricEntry& entry = metrics.entries[i];
        if (i > 0) {
            separator();
        }
        if (entry.is_standalone) {
            ImGui::PushStyleColor(ImGuiCol_Text, entry.tag_col);
            ImGui::TextUnformatted(entry.value.c_str());
            ImGui::PopStyleColor();
            if (std::string_view{entry.tag} == "UP" && !metrics.resolution.empty()) {
                ImGui::SameLine(0.0f, kTagGap * state.user_scale);
                ImGui::SetWindowFontScale(state.user_scale * 0.88f);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(185, 195, 205, 230));
                ImGui::Text("(%s)", metrics.resolution.c_str());
                ImGui::PopStyleColor();
                ImGui::SetWindowFontScale(state.user_scale);
            }
            continue;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, entry.tag_col);
        ImGui::TextUnformatted(entry.tag);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, kTagGap * state.user_scale);
        ImGui::TextUnformatted(entry.value.c_str());
    }

    if (!state.is_compiling || !state.show_shaders || state.compile_alpha <= 0.0f) {
        return;
    }
    separator();
    SpinnerAtCursor(state, draw_list);
    ImGui::SameLine(0.0f, detail::SpinnerSpacing(state));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          IM_COL32(180, 235, 255, static_cast<int>(240 * state.compile_alpha)));
    ShadersLine(state, active);
    ImGui::PopStyleColor();
}

/// The column layout: a two-column table (tag, value) with the same rows.
void RenderColumn(const HudState& state, const Metrics& metrics, unsigned active) {
    if (!ImGui::BeginTable("##hud_metrics_tbl", 2, ImGuiTableFlags_SizingFixedFit)) {
        return;
    }
    ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, 48.0f * state.user_scale);
    ImGui::TableSetupColumn("Val", ImGuiTableColumnFlags_WidthStretch);

    for (const MetricEntry& entry : metrics.entries) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushStyleColor(ImGuiCol_Text, entry.tag_col);
        ImGui::TextUnformatted(entry.tag);
        ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(entry.value.c_str());
        if (std::string_view{entry.tag} == "UP" && !metrics.resolution.empty()) {
            ImGui::SetWindowFontScale(state.user_scale * 0.88f);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(185, 195, 205, 230));
            ImGui::TextUnformatted(metrics.resolution.c_str());
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(state.user_scale);
        }
    }

    if (state.is_compiling && state.show_shaders && state.compile_alpha > 0.0f) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        SpinnerAtCursor(state, ImGui::GetWindowDrawList());

        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_Text,
                              IM_COL32(180, 235, 255, static_cast<int>(240 * state.compile_alpha)));
        ShadersLine(state, active);
        ImGui::PopStyleColor();
    }
    ImGui::EndTable();
}

/// The HUD when only the shader indicator is on (no FPS line).
void RenderCompilingOnly(const HudState& state, unsigned active) {
    SpinnerAtCursor(state, ImGui::GetWindowDrawList());
    ImGui::SameLine(0.0f, detail::SpinnerSpacing(state));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          IM_COL32(180, 235, 255, static_cast<int>(240 * state.compile_alpha)));
    if (active > 0) {
        ImGui::Text("%s (%u)...", UI::L("Compiling shaders", "Compilando shaders", "Компиляция шейдеров"),
                    active);
    } else {
        ImGui::TextUnformatted(
            UI::L("Compiling shaders...", "Compilando shaders...", "Компиляция шейдеров..."));
    }
    ImGui::PopStyleColor();
}

} // namespace

bool IsVisible() {
    const auto& s = BbSettings::Get();
    if (s.hide_hud) {
        return false;
    }
    if (s.show_fps) {
        return true;
    }
    return ShadersShown(s, BbShaderTracker::IsCompiling());
}

void Render(const Stats& stats) {
    if (!IsVisible()) {
        return;
    }

    const BbSettings::Values& s = BbSettings::Get();
    const bool is_compiling = BbShaderTracker::IsCompiling();
    const bool show_shaders = ShadersShown(s, is_compiling);

    HudState state{
        .settings = s,
        .lang = s.menu_language.load(),
        .show_fps = s.show_fps,
        .show_shaders = show_shaders,
        .is_compiling = is_compiling,
        .user_scale = std::clamp(s.fps_hud_scale.load(std::memory_order_relaxed), 0.5f, 3.0f),
        .base_scale = stats.base_scale,
        .compile_alpha = CompileAlpha(is_compiling),
    };

    state.orig_fps = stats.orig_ms_avg > 0.0f ? 1000.0f / stats.orig_ms_avg : 0.0f;
    state.total_fps = stats.total_ms_avg > 0.0f ? 1000.0f / stats.total_ms_avg : state.orig_fps;
    state.display_ms = stats.total_ms_avg > 0.0f ? stats.total_ms_avg : stats.orig_ms_avg;

    // Sample/update the background metrics (throttled) and the frametime ring buffer.
    BbPerfMetrics::Update(state.display_ms);

    const auto now = std::chrono::steady_clock::now();
    state.fg_active = s.frame_generation.load() ||
                      (stats.last_generated_time.time_since_epoch().count() != 0 &&
                       (now - stats.last_generated_time) < std::chrono::milliseconds(1500));

    const Metrics metrics = detail::BuildMetrics(state);
    const unsigned active_compiles = BbShaderTracker::GetActiveCount();

    ImVec2 win_pos, win_pivot;
    PlaceWindow(std::clamp(s.fps_hud_position.load(), 0, 3), kViewportPad * state.base_scale, win_pos,
                win_pivot);
    ImGui::SetNextWindowPos(win_pos, ImGuiCond_Always, win_pivot);
    ImGui::SetNextWindowBgAlpha(std::clamp(s.fps_hud_bg_opacity.load(), 0.0f, 1.0f));
    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::SetWindowFontScale(state.user_scale);
    ImDrawList* const draw_list = ImGui::GetWindowDrawList();

    const bool has_metrics = state.show_fps && !metrics.entries.empty();
    if (has_metrics) {
        if (std::clamp(s.fps_hud_layout.load(), 0, 1) == 0) {
            RenderRow(state, metrics, active_compiles, draw_list);
        } else {
            RenderColumn(state, metrics, active_compiles);
        }
    } else if (state.is_compiling && state.show_shaders && state.compile_alpha > 0.0f) {
        RenderCompilingOnly(state, active_compiles);
    }

    if (state.show_fps && s.fps_hud_show_graph.load()) {
        detail::DrawFrameGraph(state, draw_list);
    }
    if (state.is_compiling && state.show_shaders && state.compile_alpha > 0.0f) {
        detail::DrawCompileBeam(state, draw_list);
    }

    if (state.fg_active && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s: %.0f FPS | %s: %.0f FPS",
                          UI::L("Original Frames", "Quadros Originais", "Исходные кадры"), state.orig_fps,
                          UI::L("Total Frames", "Quadros Totais", "Всего кадров"), state.total_fps);
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::End();
}

} // namespace BbFpsHud
