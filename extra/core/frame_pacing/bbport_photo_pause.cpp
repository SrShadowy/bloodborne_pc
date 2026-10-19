// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_photo_pause.h"
#include "bbport_settings.h"

#include <algorithm>
#include <cmath>

namespace Extra::PhotoPause {
namespace {

constexpr float Frame30 = 1.0f / 30.0f;
bool g_was_paused = false;

} // namespace

bool ShouldEnablePacing(int mode, const char* hooks_env) {
    if (hooks_env && hooks_env[0] == '0') return false;
    return mode >= 30;
}

bool HandleFrameStep(LiveState* live) {
    if (!live) return false;

    const bool paused = BbSettings::Get().photo_pause.load(std::memory_order_relaxed);
    if (paused) {
        g_was_paused = true;

        live->dt = 0.0f;
        for (float& d : live->decay95) d = 1.0f;
        live->approach30 = 0.0f;
        live->approach10 = 0.0f;
        live->step_down = 0.0f;
        live->step_up = 0.0f;
        live->decay08 = 1.0f;
        live->cloth = 1.0f;
        return true;
    }

    if (g_was_paused) {
        g_was_paused = false;

        // On resuming from pause, start clean with target frame time
        // rather than a stale/distorted measurement from the pause duration.
        const float dt = live->shortest > 0.0f ? live->shortest : Frame30;
        const float frames = dt * 30.0f;
        live->dt = dt;
        const float decay95 = std::pow(0.95f, frames);
        for (float& d : live->decay95) d = decay95;
        live->approach30 = 1.0f - std::pow(1.0f - Frame30, frames);
        live->approach10 = 1.0f - std::pow(0.9f, frames);
        live->step_down = -0.1f * frames;
        live->step_up = 0.1f * frames;
        live->decay08 = std::pow(0.8f, frames);
        live->cloth = std::clamp(frames, 0.5f, 1.0f);
        return true;
    }

    return false;
}

} // namespace Extra::PhotoPause
