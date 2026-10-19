// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace Extra::PhotoPause {

/// Live values struct layout compatible with bbport_frame_rate.cpp's struct Live.
struct LiveState {
    float dt;
    float shortest;
    float pad[2];
    float decay95[4];
    float approach30;
    float approach10;
    float step_down;
    float step_up;
    float decay08;
    float cloth;
};

/// Checks whether frame pacing and simulation control hooks should be enabled.
/// In BBPort, hooks are enabled whenever mode >= 30 (including 30 FPS, to support Photo Mode pause).
bool ShouldEnablePacing(int mode, const char* hooks_env);

/// Handles photo pause gating and unpause transition inside FrameStep.
/// Returns true if pause or unpause was handled (caller should skip regular FrameStep computation).
/// Returns false to let FrameStep execute its standard frame rate calculation.
bool HandleFrameStep(LiveState* live);

} // namespace Extra::PhotoPause
