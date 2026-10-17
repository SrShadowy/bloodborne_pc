// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <chrono>

namespace BbFpsHud {

struct Stats {
    float orig_ms_avg = 0.0f;
    float total_ms_avg = 0.0f;
    std::chrono::steady_clock::time_point last_generated_time{};
    float base_scale = 1.0f;
};

// Renders the FPS, Frame Generation, and Shader Compilation HUD overlay
void Render(const Stats& stats);

} // namespace BbFpsHud
