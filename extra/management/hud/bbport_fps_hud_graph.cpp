// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the FPS HUD's graphics -- the shader-compiling spinner, the frametime graph with its
// 60/30 FPS reference lines, and the beam travelling along the window's bottom border while
// shaders compile. All of it is drawn through ImGui's window draw list.
#include "bbport_fps_hud_internal.h"

#include <algorithm>
#include <cmath>

#include "bbport_perf_metrics.h"

namespace BbFpsHud::detail {
namespace {

/// The graph's vertical range: a frame slower than this is drawn at the top.
constexpr float kGraphMaxMs = 50.0f;
/// The two references the graph draws: one frame of 60 FPS, and one of 30 FPS.
constexpr float kRefFrameMs60 = 16.67f;
constexpr float kRefFrameMs30 = 33.33f;
/// A frame slower than these reads as a warning.
constexpr float kSlowFrameMs = 16.8f;
constexpr float kWarnFrameMs = 33.4f;

} // namespace

void DrawShaderSpinner(ImDrawList* draw_list, ImVec2 pos, const HudState& state) {
    const float radius = SpinnerRadius(state);
    const float line_height = ImGui::GetTextLineHeight();
    const ImVec2 center(pos.x + radius, pos.y + line_height * 0.5f);

    draw_list->AddCircle(center, radius,
                         IM_COL32(255, 255, 255, static_cast<int>(40 * state.compile_alpha)), 16,
                         1.5f * state.base_scale);

    // An arc of 1.35 * pi that turns 7.5 rad/s: a smooth, never-repeating sweep.
    constexpr int kSegments = 16;
    constexpr float kArcRadians = 3.14159265f * 1.35f;
    const float spin_angle = static_cast<float>(ImGui::GetTime()) * 7.5f;
    draw_list->PathClear();
    for (int i = 0; i <= kSegments; ++i) {
        const float a = spin_angle + (float(i) / float(kSegments)) * kArcRadians;
        draw_list->PathLineTo(
            ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius));
    }
    draw_list->PathStroke(
        IM_COL32(0, 220, 255, static_cast<int>(240 * state.compile_alpha)), 0, 2.0f * state.base_scale);
}

void DrawFrameGraph(const HudState& state, ImDrawList* draw_list) {
    const float* samples = nullptr;
    std::size_t count = 0;
    std::size_t offset = 0;
    BbPerfMetrics::GetFrameHistory(samples, count, offset);
    if (count <= 1) {
        return;
    }

    const BbSettings::Values& s = state.settings;
    const float user_w = std::clamp(s.fps_hud_graph_width.load(std::memory_order_relaxed), 100.0f, 600.0f);
    const float user_h = std::clamp(s.fps_hud_graph_height.load(std::memory_order_relaxed), 16.0f, 120.0f);
    const float user_thickness =
        std::clamp(s.fps_hud_graph_thickness.load(std::memory_order_relaxed), 0.5f, 5.0f);

    const float graph_w = std::max(user_w * state.user_scale, ImGui::GetContentRegionAvail().x);
    const float graph_h = user_h * state.user_scale;
    const float line_thick = user_thickness * state.user_scale;

    // Explicit separation, so the graph never touches the text above it.
    ImGui::Dummy(ImVec2(0.0f, 4.0f * state.user_scale));

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + graph_w, p0.y + graph_h);
    draw_list->AddRectFilled(p0, p1, IM_COL32(16, 18, 22, 210), 3.0f);
    draw_list->AddRect(p0, p1, IM_COL32(255, 255, 255, 25), 3.0f);

    const auto ms_to_y = [&](float ms) {
        const float clamped = std::clamp(ms, 0.0f, kGraphMaxMs);
        return p1.y - 2.0f - (clamped / kGraphMaxMs) * (graph_h - 4.0f);
    };
    const float y60 = ms_to_y(kRefFrameMs60);
    draw_list->AddLine(ImVec2(p0.x, y60), ImVec2(p1.x, y60), IM_COL32(0, 220, 120, 60), 1.0f);
    const float y30 = ms_to_y(kRefFrameMs30);
    draw_list->AddLine(ImVec2(p0.x, y30), ImVec2(p1.x, y30), IM_COL32(240, 190, 40, 60), 1.0f);

    // The ring buffer holds the newest kFrameHistoryCapacity samples; the graph walks them in
    // chronological order.
    constexpr std::size_t capacity = BbPerfMetrics::kFrameHistoryCapacity;
    const float step_x = graph_w / float(capacity - 1);
    const auto sample = [&](std::size_t i) {
        return samples[count < capacity ? i : (offset + i) % capacity];
    };
    for (std::size_t i = 1; i < count; ++i) {
        const float current = sample(i);
        ImU32 color = IM_COL32(0, 225, 255, 240);
        if (current > kWarnFrameMs) {
            color = IM_COL32(255, 75, 75, 240); // Warning Red
        } else if (current > kSlowFrameMs) {
            color = IM_COL32(240, 200, 50, 230); // Yellow
        }
        draw_list->AddLine(ImVec2(p0.x + float(i - 1) * step_x, ms_to_y(sample(i - 1))),
                           ImVec2(p0.x + float(i) * step_x, ms_to_y(current)), color, line_thick);
    }

    ImGui::Dummy(ImVec2(graph_w, graph_h));
}

void DrawCompileBeam(const HudState& state, ImDrawList* draw_list) {
    const ImVec2 window_pos = ImGui::GetWindowPos();
    const ImVec2 window_size = ImGui::GetWindowSize();
    const float line_y = window_pos.y + window_size.y - 2.5f * state.base_scale;
    const float x0 = window_pos.x + 8.0f * state.base_scale;
    const float x1 = window_pos.x + window_size.x - 8.0f * state.base_scale;
    const float span = x1 - x0;
    if (span <= 10.0f) {
        return;
    }

    draw_list->AddLine(ImVec2(x0, line_y), ImVec2(x1, line_y),
                       IM_COL32(255, 255, 255, static_cast<int>(25 * state.compile_alpha)),
                       2.0f * state.base_scale);

    // A beam 35% of the span wide travels one full cycle every ~0.77 s.
    const float beam_len = span * 0.35f;
    const float travel = span + beam_len;
    const float cycle = std::fmod(static_cast<float>(ImGui::GetTime()) * 1.3f, 1.0f);
    const float beam_start = (x0 - beam_len) + travel * cycle;
    const float bx0 = std::max(x0, beam_start);
    const float bx1 = std::min(x1, beam_start + beam_len);
    if (bx1 <= bx0) {
        return;
    }

    draw_list->AddLine(ImVec2(bx0, line_y), ImVec2(bx1, line_y),
                       IM_COL32(0, 180, 255, static_cast<int>(60 * state.compile_alpha)),
                       4.5f * state.base_scale);
    draw_list->AddLine(ImVec2(bx0, line_y), ImVec2(bx1, line_y),
                       IM_COL32(0, 225, 255, static_cast<int>(240 * state.compile_alpha)),
                       2.0f * state.base_scale);
}

} // namespace BbFpsHud::detail
