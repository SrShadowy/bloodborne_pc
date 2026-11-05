// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the FPS HUD files share. bbport_fps_hud.cpp owns the layout and the metrics list,
// bbport_fps_hud_metrics.cpp builds that list, bbport_fps_hud_graph.cpp draws the spinner, the
// frametime graph and the compile beam. Only this module needs this header.
#pragma once

#include <string>
#include <vector>

#include <imgui.h>

#include "bbport_settings.h"

namespace BbFpsHud::detail {

/// One line of the HUD: a coloured tag and its value ("GPU 45%"), or a value alone ("120 FPS").
struct MetricEntry {
    const char* tag = "";
    ImU32 tag_col = IM_COL32_WHITE;
    std::string value;
    bool is_standalone = false;
};

/// The values Render() works out once and every helper here reads.
struct HudState {
    const BbSettings::Values& settings;
    int lang = BbSettings::LangEnglish;
    bool show_fps = false;
    bool show_shaders = false;
    bool is_compiling = false;
    bool fg_active = false;
    float user_scale = 1.0f;    ///< the user's HUD scale (settings)
    float base_scale = 1.0f;    ///< the resolution's scale (Stats)
    float compile_alpha = 1.0f; ///< the shader indicator's fade-out
    float orig_fps = 0.0f;
    float total_fps = 0.0f;
    float display_ms = 0.0f;
};

/// The metric list plus the resolution suffix shown after the UP line.
struct Metrics {
    std::vector<MetricEntry> entries;
    std::string resolution; ///< "2560x1440 < 1440x810", empty without an upscaler
};

/// The "FSR 4.1.1 (FP8) + FG" line and the render/output sizes under it.
Metrics BuildMetrics(const HudState& state);
/// The spinner's radius and the gap after it, both scaled by the resolution's scale.
inline float SpinnerRadius(const HudState& state) {
    return 5.0f * state.base_scale;
}
inline float SpinnerSpacing(const HudState& state) {
    return 6.0f * state.base_scale;
}

/// The spinner that marks the shader indicator, at the current cursor's screen position.
void DrawShaderSpinner(ImDrawList* draw_list, ImVec2 pos, const HudState& state);

/// The frametime graph (BbPerfMetrics::kFrameHistoryCapacity samples) under the metrics.
void DrawFrameGraph(const HudState& state, ImDrawList* draw_list);

/// The animated line along the window's bottom border while shaders compile.
void DrawCompileBeam(const HudState& state, ImDrawList* draw_list);

} // namespace BbFpsHud::detail
