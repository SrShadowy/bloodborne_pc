// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the 1:1 mouse camera -- mouse motion turns the camera directly instead of emulating the
// right stick, and the game's lock-on auto-rotation is suppressed while the mouse is in use. The
// binary hooks that feed this are in bbport_mouse_camera_patch.cpp.
#include "bbport_mouse_camera.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numbers>

#ifdef _WIN32
#include <windows.h> // [extra] C-009: VirtualQuery before reading the camera object
#endif

#include "bbport_mouse_camera_internal.h"
#include "bbport_settings.h"

namespace BbMouseCamera {
namespace detail {

std::atomic<bool> g_installed{false};
std::atomic<float> g_dx{0.0f};
std::atomic<float> g_dy{0.0f};
std::atomic<float> g_switch_x{0.0f};
std::atomic<float> g_switch_y{0.0f};
std::atomic<std::int64_t> g_last_turn_ns{0};
std::atomic<bool> g_using_mouse{false};
volatile std::uint8_t g_hold_active = 0;

/// The target-switch gesture: when it last moved, when its pulse ends, when the next may start.
/// Only touched by TakeTargetSwitchStep, on the thread that reads the pad.
std::int64_t g_switch_motion_ns = 0;
std::int64_t g_switch_pulse_end_ns = 0;
std::int64_t g_switch_cool_end_ns = 0;
/// The stick values the current pulse holds, and the accumulated motion of the gesture.
struct SwitchGesture {
    float x = 0.0f;
    float y = 0.0f;
    std::uint8_t rx = 128;
    std::uint8_t ry = 128;
} g_switch;

std::int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void RefreshHoldState() {
    const auto& s = BbSettings::Get();
    const bool want = g_installed.load(std::memory_order_relaxed) &&
                      s.raw_mouse_camera.load(std::memory_order_relaxed) &&
                      s.mouse_hold_auto_rotate.load(std::memory_order_relaxed) &&
                      g_using_mouse.load(std::memory_order_relaxed);
    g_hold_active = want ? 1 : 0;
}

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;
/// The user's sensitivity is clamped to this before use.
constexpr float kMinSensitivity = 0.05f;
constexpr float kMaxSensitivity = 20.0f;
/// The synthetic flick's stick extremes, and its neutral value.
constexpr std::uint8_t kStickMax = 255;
constexpr std::uint8_t kStickCentre = 128;

// WrapPi (the fold into (-pi, pi]) is in bbport_mouse_camera_internal.h: it is the part that has a
// test of its own, and it must stay reachable without the guest.

float Sensitivity() {
    return std::clamp(BbSettings::Get().mouse_sensitivity.load(std::memory_order_relaxed),
                      kMinSensitivity, kMaxSensitivity);
}

/// Drops the accumulated motion (the frame's turn is lost).
void DropPendingMotion() {
    g_dx.store(0.0f, std::memory_order_relaxed);
    g_dy.store(0.0f, std::memory_order_relaxed);
}

#ifdef _WIN32
/// [extra] C-009: the camera pointer may be null or freed while the game is still booting, and a
/// freed (not yet null) camera reads the same way. Querying costs ~1 us and this runs once per
/// mouse move; the cost is invisible next to the trap the turn needs.
bool CameraReadable(const std::uint8_t* cam) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(cam, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    return base + info.RegionSize >= reinterpret_cast<std::uintptr_t>(cam) + kCameraObjectBytes;
}
#endif

} // namespace

} // namespace detail

/// The turn itself, called from the guest's camera update with rdi = camera, rsi = rbp and
/// rdx = the stack's saved xmm4..xmm7.
extern "C" void ApplyMouseTurn(std::uint8_t* cam, std::uint8_t* rbp, float* stick) {
    using namespace detail;

    g_last_turn_ns.store(NowNs(), std::memory_order_relaxed);
    g_switch_x.store(0.0f, std::memory_order_relaxed);
    g_switch_y.store(0.0f, std::memory_order_relaxed);

    const auto& s = BbSettings::Get();
    RefreshHoldState();
    if (!s.raw_mouse_camera.load(std::memory_order_relaxed)) {
        DropPendingMotion();
        return;
    }

    const float dx = g_dx.exchange(0.0f, std::memory_order_relaxed);
    const float dy = g_dy.exchange(0.0f, std::memory_order_relaxed);
    if (dx == 0.0f && dy == 0.0f) {
        return;
    }
    // [extra] C-009: the hook is installed before the camera object exists and the game still calls
    // this path (mouse button held at boot, a menu repaint). Reading cam+0x140 then faulted at
    // address 0x140 (5/5 crashes in out/win-dev uncap runs, 2026-11-03). No camera, no turn: the
    // accumulated delta is dropped with the frame, as when raw_mouse_camera is off.
    if (!cam || !stick) {
        DropPendingMotion();
        return;
    }
#ifdef _WIN32
    if (!CameraReadable(cam)) {
        DropPendingMotion();
        return;
    }
#endif

    const float scale = kRadiansPerCount * Sensitivity();
    const float sign_x = s.mouse_invert_x.load(std::memory_order_relaxed) ? -1.0f : 1.0f;
    const float sign_y = s.mouse_invert_y.load(std::memory_order_relaxed) ? -1.0f : 1.0f;

    float pitch = 0.0f, yaw = 0.0f, highest = 0.0f, lowest = 0.0f;
    std::memcpy(&pitch, cam + kPitchOffset, sizeof(float));
    std::memcpy(&yaw, cam + kYawOffset, sizeof(float));
    std::memcpy(&highest, cam + kHighestPitchOffset, sizeof(float));
    std::memcpy(&lowest, cam + kLowestPitchOffset, sizeof(float));
    // [extra] A camera the game is still building holds whatever was in the memory (C-009 faulted
    // on a null camera the same way). A NaN there would reach the game's own arithmetic and an
    // infinity would spend the frame in WrapPi: the turn starts from a sane angle instead.
    if (!std::isfinite(yaw)) {
        yaw = 0.0f;
    }
    if (!std::isfinite(pitch)) {
        pitch = 0.0f;
    }
    const float stick_pitch = stick[0], stick_yaw = stick[1];
    pitch += dy * scale * sign_y;
    // A stray non-finite delta would make it through WrapPi (which folds the yaw) into the pitch.
    if (!std::isfinite(pitch)) {
        pitch = 0.0f;
    }
    yaw = WrapPi(yaw + dx * scale * sign_x);
    // The game clamps its own pitch between the two limits, minus what the stick already adds.
    // The bounds must satisfy std::clamp's contract (finite, lo <= hi); the camera object can still
    // hold junk (C-009) and the stick comes from guest registers, so a non-finite or inverted pair
    // is skipped instead of handed to the clamp. `pitch` itself is already finite here.
    const float lowest_bound = lowest - stick_pitch;
    const float highest_bound = highest - stick_pitch;
    const bool has_limits = std::isfinite(lowest) && std::isfinite(highest) &&
                            std::isfinite(lowest_bound) && std::isfinite(highest_bound) &&
                            highest_bound >= lowest_bound;
    if (has_limits) {
        pitch = std::clamp(pitch, lowest_bound, highest_bound);
    }
    yaw = WrapPi(yaw);
    // Keep the yaw on the same side of zero as the stick's contribution, so the two do not cancel
    // across the wrap.
    const float combined = yaw + stick_yaw;
    if (WrapPi(combined) < 0.0f && combined > 0.0f) {
        yaw -= kTwoPi;
    } else if (WrapPi(combined) > 0.0f && combined < 0.0f) {
        yaw += kTwoPi;
    }

    std::memcpy(cam + kPitchOffset, &pitch, sizeof(float));
    std::memcpy(cam + kYawOffset, &yaw, sizeof(float));
    *(rbp - kCameraDirtyOffset) = 1;
}

bool IsActive() {
    return detail::g_installed.load(std::memory_order_relaxed) &&
           BbSettings::Get().raw_mouse_camera.load(std::memory_order_relaxed);
}

void FeedRelativeMotion(float dx, float dy) {
    if (!IsActive()) {
        return;
    }
    detail::g_using_mouse.store(true, std::memory_order_relaxed);
    detail::RefreshHoldState();
    detail::g_dx.fetch_add(dx, std::memory_order_relaxed);
    detail::g_dy.fetch_add(dy, std::memory_order_relaxed);
    detail::g_switch_x.fetch_add(dx, std::memory_order_relaxed);
    detail::g_switch_y.fetch_add(dy, std::memory_order_relaxed);
}

void SetUsingMouse(bool using_mouse) {
    if (detail::g_using_mouse.load(std::memory_order_relaxed) != using_mouse) {
        detail::g_using_mouse.store(using_mouse, std::memory_order_relaxed);
        detail::RefreshHoldState();
    }
}

bool TakeTargetSwitchStep(std::uint8_t* out_rx, std::uint8_t* out_ry) {
    using namespace detail;

    if (!IsActive()) {
        return false;
    }
    const std::int64_t now = NowNs();
    // The previous flick is still being held.
    if (now < g_switch_pulse_end_ns) {
        if (out_rx) {
            *out_rx = g_switch.rx;
        }
        if (out_ry) {
            *out_ry = g_switch.ry;
        }
        return true;
    }

    const float dx = g_switch_x.exchange(0.0f, std::memory_order_relaxed);
    const float dy = g_switch_y.exchange(0.0f, std::memory_order_relaxed);
    const std::int64_t last_turn = g_last_turn_ns.load(std::memory_order_relaxed);
    // No turn yet this session, or the camera is still being driven: not a switch gesture.
    if (last_turn == 0 || now - last_turn < kLockWindowNs) {
        return false;
    }

    // A pause starts the gesture over.
    if (dx != 0.0f || dy != 0.0f) {
        if (now - g_switch_motion_ns > kSwitchResetNs) {
            g_switch.x = 0.0f;
            g_switch.y = 0.0f;
        }
        g_switch_motion_ns = now;
        g_switch.x += dx;
        g_switch.y += dy;
    }
    // The previous flick's cooldown swallows the gesture's remainder.
    if (now < g_switch_cool_end_ns) {
        g_switch.x = 0.0f;
        g_switch.y = 0.0f;
        return false;
    }

    const float threshold = kSwitchRadians / (kRadiansPerCount * Sensitivity());
    if (std::fabs(g_switch.x) < threshold && std::fabs(g_switch.y) < threshold) {
        return false;
    }

    // The dominant axis picks the switch direction.
    if (std::fabs(g_switch.x) >= std::fabs(g_switch.y)) {
        g_switch.rx = g_switch.x > 0.0f ? kStickMax : 0;
        g_switch.ry = kStickCentre;
    } else {
        g_switch.rx = kStickCentre;
        g_switch.ry = g_switch.y > 0.0f ? kStickMax : 0;
    }
    g_switch.x = 0.0f;
    g_switch.y = 0.0f;
    g_switch_pulse_end_ns = now + kSwitchPulseNs;
    g_switch_cool_end_ns = g_switch_pulse_end_ns + kSwitchCoolNs;
    if (out_rx) {
        *out_rx = g_switch.rx;
    }
    if (out_ry) {
        *out_ry = g_switch.ry;
    }
    return true;
}

} // namespace BbMouseCamera
