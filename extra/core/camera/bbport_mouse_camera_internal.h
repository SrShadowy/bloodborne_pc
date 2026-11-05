// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the mouse camera's files share. bbport_mouse_camera.cpp owns the state and the
// public API, bbport_mouse_camera_patch.cpp the image hooks (the guest's camera writes redirected
// into ApplyMouseTurn). Only this module needs this header.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace BbMouseCamera::detail {

/// `angle` folded into (-pi, pi].
///
/// [extra] This used to be `while (angle > kPi) angle -= kTwoPi;` (and the mirror below), in
/// bbport_mouse_camera.cpp. That loop subtracts one turn per iteration, in `float`: from 2^27
/// (1.3e8) the spacing of floats is 16, so subtracting a turn (6.28) rounds straight back to the
/// same value and the loop never ends (an infinity does the same); below that it still costs
/// |angle|/(2*pi) iterations, up to ~2e7 per call and there are two calls a frame, for a value the
/// game left in a camera object it was still building. The main thread was found spinning in it
/// forever (thread dump, 2026-11-05, the freeze on leaving Iosefka's Clinic: 121 threads, the main
/// one here, the GPU idle because the game had stopped submitting). `std::remainder` is the exact
/// reduction, in one step, for any magnitude; non-finite in, 0 out. Covered by
/// tests/test_mouse_camera_angle.cpp.
inline float WrapPi(float angle) {
    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr float kTwoPi = 2.0f * kPi;
    if (!std::isfinite(angle)) {
        return 0.0f;
    }
    // remainder is in [-pi, pi]; -pi is the one value outside (-pi, pi].
    const float folded = std::remainder(angle, kTwoPi);
    return folded <= -kPi ? folded + kTwoPi : folded;
}

// ---- Offsets into the guest's camera update (CUSA03173 1.09, minus the eboot's 0x400000
// image base) ----

/// Where the game's camera update begins (verified by its prologue before patching).
constexpr std::uint64_t kUpdate = 0x143ac60;
/// Where the game stores the camera's pitch/yaw: the hook resumes right after.
constexpr std::uint64_t kTurnStore = 0x143ce54;
constexpr std::uint64_t kTurnResume = 0x143ce67;
/// The two exits of the game's auto-rotation ramp.
constexpr std::uint64_t kRampExits[2] = {0x143f9bc, 0x143fabf};
/// The three sites that keep the camera turned toward the lock-on target.
constexpr std::uint64_t kHoldSites[3] = {0x143c6e8, 0x143c984, 0x143dde6};

// ---- The camera object (from its base) ----

constexpr std::uintptr_t kPitchOffset = 0x140;
constexpr std::uintptr_t kYawOffset = 0x144;
/// The pitch limits the game itself clamps to.
constexpr std::uintptr_t kHighestPitchOffset = 0x1ec;
constexpr std::uintptr_t kLowestPitchOffset = 0x1f0;
/// The last byte the playback must be able to read (pitch and yaw).
constexpr std::uintptr_t kCameraObjectBytes = 0x1f4;
/// Byte set on rbp-0x3c4 to tell the game the camera changed this frame.
constexpr std::uintptr_t kCameraDirtyOffset = 0x3c4;

// ---- The port's own tuning ----

/// Radians of camera turn per mouse count, before the user's sensitivity.
constexpr float kRadiansPerCount = 0.0028f;
/// While a turn happened this recently, the mouse drives the camera and not the stick.
constexpr std::int64_t kLockWindowNs = 100'000'000;
/// Mouse motion older than this starts a new target-switch gesture.
constexpr std::int64_t kSwitchResetNs = 180'000'000;
/// How long one synthetic stick flick lasts.
constexpr std::int64_t kSwitchPulseNs = 55'000'000;
/// How long after a flick the next one is ignored (so one gesture is one switch).
constexpr std::int64_t kSwitchCoolNs = 160'000'000;
/// The turn a gesture must accumulate to emit a flick.
constexpr float kSwitchRadians = 0.065f;

// ---- State (bbport_mouse_camera.cpp) ----

extern std::atomic<bool> g_installed;
extern std::atomic<float> g_dx, g_dy;
extern std::atomic<float> g_switch_x, g_switch_y;
extern std::atomic<std::int64_t> g_last_turn_ns;
extern std::atomic<bool> g_using_mouse;
/// Non-zero while the port wants the game's lock-on hold skipped (read from the guest's code).
extern volatile std::uint8_t g_hold_active;

/// Re-reads the settings into g_hold_active.
void RefreshHoldState();

/// Monotonic time, in nanoseconds.
std::int64_t NowNs();

} // namespace BbMouseCamera::detail

namespace BbMouseCamera {

/// The turn itself: called from the guest's camera update with rdi = camera, rsi = rbp and
/// rdx = the stack's saved xmm4..xmm7 (bbport_mouse_camera.cpp).
extern "C" void ApplyMouseTurn(std::uint8_t* cam, std::uint8_t* rbp, float* stick);

} // namespace BbMouseCamera
