// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Window backend choice and trace for SDL (Hyprland/Wayland/XWayland reports).
// The decision itself lives in run.sh (BB_VIDEO_BACKEND, or the video_backend= line in
// bbport.ini); these hooks cover a run without run.sh and put the decision on stdout so a
// report shows which backend was taken even without a log file.
//
// Thread-safety: ApplyHint and TraceRequest run on the window thread before SDL video init;
// TraceCreated follows on the same thread. SurfaceZeroExtent runs on the render thread; its
// prints are single whole-line writes.

namespace BbVideoBackend {

// Before SDL video init: a BB_VIDEO_BACKEND of x11/wayland hints SDL's driver (auto does
// nothing here: run.sh and SDL's own preference decide).
void ApplyHint();

// Before the window is created: the requested backend on stdout, flushed, so a crash during
// creation still leaves the choice in the log.
void TraceRequest();

// After the window exists: the same line with the driver SDL actually took.
void TraceCreated(int width, int height);

// Swapchain surface 0x0 (a compositor did not map the window: a hidden workspace, minimised):
// reported once per stretch on stdout instead of waiting silently. A non-zero extent resets
// the report for the next stretch.
void SurfaceZeroExtent(bool zero);

} // namespace BbVideoBackend
