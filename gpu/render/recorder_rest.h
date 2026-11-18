// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>

namespace Vulkan::RecorderRest {

// Thresholds mirrored from bbhost's recorder_rest heuristic: let worker threads skip their
// active spin when a frame limit or the GPU holds the frame with CPU headroom to spare.
constexpr double kRestSlack = 0.15;
constexpr double kWakeSlack = 0.08;
constexpr double kGpuBound = 0.90;
constexpr double kGpuFree = 0.80;
constexpr double kCpRoom = 0.85;
constexpr double kCpFull = 0.92;
constexpr double kGpuRoom = 0.85;
constexpr double kGpuFull = 0.92;
constexpr int kQuietWindows = 2;
constexpr int kGpuWindows = 6;

struct State {
    bool cap = false;
    bool gpu = false;
    int quiet = 0;
    int bound = 0;
    double busy[kGpuWindows] = {};
    int busy_n = 0;

    [[nodiscard]] bool Resting() const noexcept {
        return cap || gpu;
    }
};

namespace detail {
inline double Median(const double* v, int n) noexcept {
    double tmp[kGpuWindows];
    n = std::min(n, kGpuWindows);
    std::copy(v, v + n, tmp);
    std::sort(tmp, tmp + n);
    return n ? (n & 1 ? tmp[n / 2] : 0.5 * (tmp[n / 2 - 1] + tmp[n / 2])) : 0.0;
}
} // namespace detail

/// Evaluates one ~0.5 s frame window and returns whether workers should skip active spinning.
/// `slack`: share of the window spent waiting on a frame cap; `gpu` / `cp`: GPU and command
/// processor busy shares (< 0 when unavailable); `loading`: non-world/loading frame.
inline bool Step(State& s, double slack, double gpu, double cp, bool loading) noexcept {
    if (loading) {
        s = State{};
        return false;
    }
    const bool gpu_known = gpu >= 0.0;
    const bool cp_full = cp >= kCpFull;
    if (s.cap) {
        if (slack < kWakeSlack || (gpu_known && gpu >= kGpuFull) || cp_full) {
            s.cap = false;
        }
    } else {
        s.quiet = slack >= kRestSlack && (!gpu_known || gpu < kGpuRoom) && !cp_full ? s.quiet + 1
                                                                                    : 0;
        if (s.quiet >= kQuietWindows) {
            s.cap = true;
            s.quiet = 0;
        }
    }
    if (gpu_known) {
        s.busy[s.busy_n % kGpuWindows] = gpu;
        ++s.busy_n;
    }
    if (s.gpu) {
        if (!gpu_known || cp_full || slack >= kRestSlack ||
            (s.busy_n >= kGpuWindows && detail::Median(s.busy, kGpuWindows) < kGpuFree)) {
            s.gpu = false;
        }
    } else {
        s.bound = gpu_known && gpu >= kGpuBound && cp < kCpRoom && slack < kRestSlack
                      ? s.bound + 1
                      : 0;
        if (s.bound >= kQuietWindows) {
            s.gpu = true;
            s.bound = 0;
        }
    }
    return s.Resting();
}

} // namespace Vulkan::RecorderRest
