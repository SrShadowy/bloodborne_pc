// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_mouse_camera.h"

#include "bbport_code_patch.h"
#include "bbport_settings.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numbers>

namespace BbMouseCamera {
namespace {

// CUSA03173 1.09 offsets relative to eboot image base (bbhost BN VA - 0x400000):
constexpr std::uint64_t kUpdate = 0x143ac60;
constexpr std::uint64_t kTurnStore = 0x143ce54;
constexpr std::uint64_t kTurnResume = 0x143ce67;
constexpr std::uint64_t kRampExits[2] = {0x143f9bc, 0x143fabf};
constexpr std::uint64_t kHoldSites[3] = {0x143c6e8, 0x143c984, 0x143dde6};

constexpr std::uint8_t kUpdatePrologue[20] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
    0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0xd8, 0x03, 0x00, 0x00,
};
constexpr std::uint8_t kTurnBytes[19] = {
    0xc4, 0xc1, 0x7a, 0x11, 0x85, 0x40, 0x01, 0x00, 0x00, // vmovss [r13+0x140], xmm0
    0xc4, 0xc3, 0x79, 0x17, 0x8d, 0x44, 0x01, 0x00, 0x00, 0x01, // vextractps [r13+0x144], xmm1, 1
};
constexpr std::uint8_t kRampExitBytes[2][14] = {
    {0xc4, 0xc1, 0x7a, 0x11, 0x85, 0x40, 0x01, 0x00, 0x00, 0xe9, 0x9d, 0xd4, 0xff, 0xff},
    {0xc4, 0xc1, 0x7a, 0x11, 0x85, 0x40, 0x01, 0x00, 0x00, 0xe9, 0x9a, 0xd3, 0xff, 0xff},
};
constexpr std::uint8_t kHoldBytes[3][9] = {
    {0xc4, 0xc1, 0x7a, 0x11, 0x85, 0x44, 0x01, 0x00, 0x00}, // vmovss [r13+0x144], xmm0
    {0xc4, 0xc1, 0x7a, 0x11, 0x8d, 0x30, 0x01, 0x00, 0x00}, // vmovss [r13+0x130], xmm1
    {0xc4, 0xc1, 0x7a, 0x11, 0x85, 0x94, 0x02, 0x00, 0x00}, // vmovss [r13+0x294], xmm0
};

constexpr float kRadiansPerCount = 0.0028f;
constexpr std::int64_t kLockWindowNs = 100'000'000;
constexpr std::int64_t kSwitchResetNs = 180'000'000;
constexpr std::int64_t kSwitchPulseNs = 55'000'000;
constexpr std::int64_t kSwitchCoolNs = 160'000'000;
constexpr float kSwitchRadians = 0.065f;

std::atomic<bool> g_installed{false};
std::atomic<float> g_dx{0.0f}, g_dy{0.0f};
std::atomic<float> g_switch_x{0.0f}, g_switch_y{0.0f};
std::atomic<std::int64_t> g_last_turn_ns{0};
std::atomic<bool> g_using_mouse{false};
volatile std::uint8_t g_hold_active = 0;

std::int64_t g_switch_motion_ns = 0;
std::int64_t g_switch_pulse_end_ns = 0;
std::int64_t g_switch_cool_end_ns = 0;
std::uint8_t g_switch_rx = 128, g_switch_ry = 128;

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

float WrapPi(float a) {
    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr float kTwoPi = 2.0f * kPi;
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

extern "C" void ApplyMouseTurn(std::uint8_t* cam, std::uint8_t* rbp, float* stick) {
    g_last_turn_ns.store(NowNs(), std::memory_order_relaxed);
    g_switch_x.store(0.0f, std::memory_order_relaxed);
    g_switch_y.store(0.0f, std::memory_order_relaxed);

    const auto& s = BbSettings::Get();
    RefreshHoldState();
    if (!s.raw_mouse_camera.load(std::memory_order_relaxed)) {
        g_dx.store(0.0f, std::memory_order_relaxed);
        g_dy.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const float dx = g_dx.exchange(0.0f, std::memory_order_relaxed);
    const float dy = g_dy.exchange(0.0f, std::memory_order_relaxed);
    if (dx == 0.0f && dy == 0.0f) return;

    const float sens = std::clamp(s.mouse_sensitivity.load(std::memory_order_relaxed), 0.05f, 20.0f);
    const float scale = kRadiansPerCount * sens;
    const float sign_x = s.mouse_invert_x.load(std::memory_order_relaxed) ? 1.0f : -1.0f;
    const float sign_y = s.mouse_invert_y.load(std::memory_order_relaxed) ? -1.0f : 1.0f;

    float pitch = 0.0f, yaw = 0.0f, highest = 0.0f, lowest = 0.0f;
    std::memcpy(&pitch, cam + 0x140, sizeof(float));
    std::memcpy(&yaw, cam + 0x144, sizeof(float));
    std::memcpy(&highest, cam + 0x1ec, sizeof(float));
    std::memcpy(&lowest, cam + 0x1f0, sizeof(float));

    const float stick_pitch = stick[0], stick_yaw = stick[1];
    pitch += dy * scale * sign_y;
    yaw = WrapPi(yaw + dx * scale * sign_x);
    if (highest > lowest) pitch = std::clamp(pitch, lowest - stick_pitch, highest - stick_pitch);
    yaw = WrapPi(yaw);
    if (WrapPi(yaw + stick_yaw) < 0.0f && yaw + stick_yaw > 0.0f) {
        yaw -= 2.0f * std::numbers::pi_v<float>;
    } else if (WrapPi(yaw + stick_yaw) > 0.0f && yaw + stick_yaw < 0.0f) {
        yaw += 2.0f * std::numbers::pi_v<float>;
    }

    std::memcpy(cam + 0x140, &pitch, sizeof(float));
    std::memcpy(cam + 0x144, &yaw, sizeof(float));
    *(rbp - 0x3c4) = 1;
}

bool VerifyCameraSites(const unsigned char* image, std::uint64_t size) {
    if (!image || size <= kRampExits[1] + 14) return false;
    if (std::memcmp(image + kUpdate, kUpdatePrologue, sizeof(kUpdatePrologue)) != 0 ||
        std::memcmp(image + kTurnStore, kTurnBytes, sizeof(kTurnBytes)) != 0) {
        return false;
    }
    for (int i = 0; i < 2; ++i) {
        if (std::memcmp(image + kRampExits[i], kRampExitBytes[i], 14) != 0) return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (std::memcmp(image + kHoldSites[i], kHoldBytes[i], 9) != 0) return false;
    }
    return true;
}

std::uint8_t* EmitTurnStoreStub(std::uint8_t*& out, const std::uint8_t* resume_addr) {
    auto emit = [&](const void* b, std::size_t n) { std::memcpy(out, b, n); out += n; };
    std::uint8_t* stub = out;
    emit(kTurnBytes, sizeof(kTurnBytes));
    static constexpr std::uint8_t kSaveRegs[] = {
        0x50, 0x51, 0x52, 0x56, 0x57, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x53,
        0x48, 0x89, 0xe3, 0x48, 0x83, 0xe4, 0xf0, 0x48, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00,
        0xc5, 0xfa, 0x7f, 0x04, 0x24, 0xc5, 0xfa, 0x7f, 0x4c, 0x24, 0x10,
        0xc5, 0xfa, 0x7f, 0x54, 0x24, 0x20, 0xc5, 0xfa, 0x7f, 0x5c, 0x24, 0x30,
        0xc5, 0xfa, 0x7f, 0x64, 0x24, 0x40, 0xc5, 0xfa, 0x7f, 0x6c, 0x24, 0x50,
        0xc5, 0xfa, 0x7f, 0x74, 0x24, 0x60, 0xc5, 0xfa, 0x7f, 0x7c, 0x24, 0x70,
        0xc5, 0x7a, 0x7f, 0x84, 0x24, 0x80, 0, 0, 0, 0xc5, 0x7a, 0x7f, 0x8c, 0x24, 0x90, 0, 0, 0,
        0xc5, 0x7a, 0x7f, 0x94, 0x24, 0xa0, 0, 0, 0, 0xc5, 0x7a, 0x7f, 0x9c, 0x24, 0xb0, 0, 0, 0,
        0xc5, 0x7a, 0x7f, 0xa4, 0x24, 0xc0, 0, 0, 0, 0xc5, 0x7a, 0x7f, 0xac, 0x24, 0xd0, 0, 0, 0,
        0xc5, 0x7a, 0x7f, 0xb4, 0x24, 0xe0, 0, 0, 0, 0xc5, 0x7a, 0x7f, 0xbc, 0x24, 0xf0, 0, 0, 0,
        0x4c, 0x89, 0xef, 0x48, 0x89, 0xee, 0x48, 0x8d, 0x94, 0x24, 0xa0, 0x00, 0x00, 0x00,
        0x48, 0xb8,
    };
    emit(kSaveRegs, sizeof(kSaveRegs));
    const auto fn = reinterpret_cast<std::uint64_t>(&ApplyMouseTurn);
    emit(&fn, sizeof(fn));
    static constexpr std::uint8_t kCallAndRestore[] = {
        0xff, 0xd0,
        0xc5, 0xfa, 0x6f, 0x04, 0x24, 0xc5, 0xfa, 0x6f, 0x4c, 0x24, 0x10,
        0xc5, 0xfa, 0x6f, 0x54, 0x24, 0x20, 0xc5, 0xfa, 0x6f, 0x5c, 0x24, 0x30,
        0xc5, 0xfa, 0x6f, 0x64, 0x24, 0x40, 0xc5, 0xfa, 0x6f, 0x6c, 0x24, 0x50,
        0xc5, 0xfa, 0x6f, 0x74, 0x24, 0x60, 0xc5, 0xfa, 0x6f, 0x7c, 0x24, 0x70,
        0xc5, 0x7a, 0x6f, 0x84, 0x24, 0x80, 0, 0, 0, 0xc5, 0x7a, 0x6f, 0x8c, 0x24, 0x90, 0, 0, 0,
        0xc5, 0x7a, 0x6f, 0x94, 0x24, 0xa0, 0, 0, 0, 0xc5, 0x7a, 0x6f, 0x9c, 0x24, 0xb0, 0, 0, 0,
        0xc5, 0x7a, 0x6f, 0xa4, 0x24, 0xc0, 0, 0, 0, 0xc5, 0x7a, 0x6f, 0xac, 0x24, 0xd0, 0, 0, 0,
        0xc5, 0x7a, 0x6f, 0xb4, 0x24, 0xe0, 0, 0, 0, 0xc5, 0x7a, 0x6f, 0xbc, 0x24, 0xf0, 0, 0, 0,
        0x48, 0x89, 0xdc,
        0x5b, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, 0x5f, 0x5e, 0x5a, 0x59, 0x58,
        0xc4, 0xc1, 0x7a, 0x10, 0x85, 0x40, 0x01, 0x00, 0x00,
    };
    emit(kCallAndRestore, sizeof(kCallAndRestore));
    BbCodePatch::WriteRel32Jump(out, resume_addr, 5);
    out += 5;
    return stub;
}

std::uint8_t* EmitHoldStub(std::uint8_t*& out, const std::uint8_t* orig_bytes, const std::uint8_t* resume_addr) {
    auto emit = [&](const void* b, std::size_t n) { std::memcpy(out, b, n); out += n; };
    std::uint8_t* stub = out;
    static constexpr std::uint8_t kPrefix[] = {0x50, 0x9c, 0x48, 0xb8};
    emit(kPrefix, sizeof(kPrefix));
    const auto flag_addr = reinterpret_cast<std::uint64_t>(&g_hold_active);
    emit(&flag_addr, sizeof(flag_addr));
    static constexpr std::uint8_t kCheck[] = {0x80, 0x38, 0x00, 0x75, 0x09};
    emit(kCheck, sizeof(kCheck));
    emit(orig_bytes, 9);
    static constexpr std::uint8_t kSuffix[] = {0x9d, 0x58};
    emit(kSuffix, sizeof(kSuffix));
    BbCodePatch::WriteRel32Jump(out, resume_addr, 5);
    out += 5;
    return stub;
}

} // namespace

void PatchImage(unsigned char* image, std::uint64_t size) {
    if (!VerifyCameraSites(image, size)) return;
    std::uint8_t* stub_page = BbCodePatch::AllocateNearPage(image + kTurnStore);
    if (!stub_page) return;

    std::uint8_t* out = stub_page;
    std::uint8_t* turn_stub = EmitTurnStoreStub(out, image + kTurnResume);
    std::uint8_t* hold_stubs[3]{};
    for (int i = 0; i < 3; ++i) {
        hold_stubs[i] = EmitHoldStub(out, kHoldBytes[i], image + kHoldSites[i] + 9);
    }
    BbCodePatch::SealExecPage(stub_page);

    BbCodePatch::WriteRel32Jump(image + kTurnStore, turn_stub, sizeof(kTurnBytes));
    for (int i = 0; i < 3; ++i) {
        BbCodePatch::WriteRel32Jump(image + kHoldSites[i], hold_stubs[i], 9);
    }
    g_installed.store(true, std::memory_order_release);
    RefreshHoldState();
    std::printf("[BBPort] 1:1 raw mouse camera & auto-rotation hooks installed.\n");
}

bool IsActive() {
    return g_installed.load(std::memory_order_relaxed) &&
           BbSettings::Get().raw_mouse_camera.load(std::memory_order_relaxed);
}

void FeedRelativeMotion(float dx, float dy) {
    if (!IsActive()) return;
    g_using_mouse.store(true, std::memory_order_relaxed);
    RefreshHoldState();
    g_dx.fetch_add(dx, std::memory_order_relaxed);
    g_dy.fetch_add(dy, std::memory_order_relaxed);
    g_switch_x.fetch_add(dx, std::memory_order_relaxed);
    g_switch_y.fetch_add(dy, std::memory_order_relaxed);
}

void SetUsingMouse(bool using_mouse) {
    if (g_using_mouse.load(std::memory_order_relaxed) != using_mouse) {
        g_using_mouse.store(using_mouse, std::memory_order_relaxed);
        RefreshHoldState();
    }
}

bool TakeTargetSwitchStep(std::uint8_t* out_rx, std::uint8_t* out_ry) {
    if (!IsActive()) return false;
    const std::int64_t now = NowNs();
    if (now < g_switch_pulse_end_ns) {
        if (out_rx) *out_rx = g_switch_rx;
        if (out_ry) *out_ry = g_switch_ry;
        return true;
    }

    const float dx = g_switch_x.exchange(0.0f, std::memory_order_relaxed);
    const float dy = g_switch_y.exchange(0.0f, std::memory_order_relaxed);
    const std::int64_t last_turn = g_last_turn_ns.load(std::memory_order_relaxed);
    if (last_turn == 0 || now - last_turn < kLockWindowNs) return false;

    static float bucket_x = 0.0f, bucket_y = 0.0f;
    if (dx != 0.0f || dy != 0.0f) {
        if (now - g_switch_motion_ns > kSwitchResetNs) {
            bucket_x = 0.0f;
            bucket_y = 0.0f;
        }
        g_switch_motion_ns = now;
        bucket_x += dx;
        bucket_y += dy;
    }
    if (now < g_switch_cool_end_ns) {
        bucket_x = 0.0f;
        bucket_y = 0.0f;
        return false;
    }

    const float sens = std::clamp(BbSettings::Get().mouse_sensitivity.load(std::memory_order_relaxed), 0.05f, 20.0f);
    const float threshold = kSwitchRadians / (kRadiansPerCount * sens);
    if (std::fabs(bucket_x) < threshold && std::fabs(bucket_y) < threshold) return false;

    if (std::fabs(bucket_x) >= std::fabs(bucket_y)) {
        g_switch_rx = bucket_x > 0.0f ? 255 : 0;
        g_switch_ry = 128;
    } else {
        g_switch_rx = 128;
        g_switch_ry = bucket_y > 0.0f ? 255 : 0;
    }
    bucket_x = 0.0f;
    bucket_y = 0.0f;
    g_switch_pulse_end_ns = now + kSwitchPulseNs;
    g_switch_cool_end_ns = g_switch_pulse_end_ns + kSwitchCoolNs;
    if (out_rx) *out_rx = g_switch_rx;
    if (out_ry) *out_ry = g_switch_ry;
    return true;
}

} // namespace BbMouseCamera
