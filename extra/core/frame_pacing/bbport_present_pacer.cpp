// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_present_pacer.h"
#include "bbport_settings.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace Extra::PresentPacer {
namespace {

using Clock = std::chrono::steady_clock;
Clock::time_point g_last_start{};
Clock::time_point g_present_start{};
double g_interval_ms = 33.333; // Default baseline interval for 30 FPS base game
bool g_was_photo_pause = false;

} // namespace

void OnPresentStart() {
    const bool photo_pause = BbSettings::Get().photo_pause.load(std::memory_order_relaxed);
    if (photo_pause) {
        g_was_photo_pause = true;
    } else if (g_was_photo_pause) {
        g_was_photo_pause = false;
        g_last_start = Clock::time_point{};
        g_interval_ms = 33.333;
    }

    const auto now = Clock::now();
    g_present_start = now;

    if (g_last_start.time_since_epoch().count() != 0) {
        const double ms = std::chrono::duration<double, std::milli>(now - g_last_start).count();
        // Sane engine frame intervals between consecutive real frames (10ms to 45ms).
        // At 30 FPS base, ms is ~33.3ms; at 60 FPS base, ms is ~16.7ms.
        // Hitches, pauses, or menu delays (> 45.0ms) must NOT inflate interval_ms.
        if (ms >= 10.0 && ms <= 45.0) {
            g_interval_ms = g_interval_ms * 0.9 + ms * 0.1;
        } else if (ms > 45.0) {
            // Reset to clean 30 FPS default upon hitch or load screen
            g_interval_ms = 33.333;
        }
    }
    g_last_start = now;
    g_interval_ms = std::clamp(g_interval_ms, 8.0, 40.0);
}

void SleepInterpolated(bool presented) {
    if (!presented) return;

    // Symmetric 50% presentation target: real frame is displayed exactly halfway through the real frame interval.
    const double half_interval = std::clamp(g_interval_ms * 0.5, 4.0, 20.0);
    const auto target = g_present_start + std::chrono::duration_cast<Clock::duration>(
                                              std::chrono::duration<double, std::milli>(half_interval));

    // High-precision hybrid sleep: coarse OS sleep until 1.5ms before target,
    // then busy-spin with CPU pause instruction to eliminate Linux scheduler jitter.
    const auto coarse_target = target - std::chrono::microseconds(1500);
    if (Clock::now() < coarse_target) {
        std::this_thread::sleep_until(coarse_target);
    }
    while (Clock::now() < target) {
        #if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
        #endif
    }
}

void OnRealFramePresented() {
    // Retained for interface stability; g_last_start is now tracked at OnPresentStart
    // to measure true full-frame intervals without sleep skew.
}

} // namespace Extra::PresentPacer
