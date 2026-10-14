// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_camera.h"
#include "bbport_settings.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace BbCamera {
namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u64 kBase = 0x400000;
constexpr u64 kFovClamp = 0x183af4e - kBase;
constexpr u8 kFovClampBytes[4] = {0x48, 0x0f, 0x46, 0xc6}; // cmovbe rax, rsi
constexpr u8 kFovNoClamp[4] = {0x48, 0x89, 0xf0, 0x90};    // mov rax, rsi; nop

constexpr u64 kRepositorySlot = 0x5940340 - kBase; // SoloParamRepository_ptr
constexpr unsigned kSlots = 63, kCapsPerSlot = 8, kLockCamSlot = 33;
constexpr std::size_t kSlotStride = 0x48, kSlotsAt = 0x70;

constexpr char kTypeName[] = "LOCK_CAM_PARAM_ST";
constexpr std::size_t kTypeAt = 0xc, kRowCountAt = 0xa, kRowsAt = 0x40, kRowRecord = 0x18, kRowSize = 0x20;
constexpr std::size_t kFovAt = 0x14;
constexpr std::size_t kFieldAt[3] = {0x0, 0xc, kFovAt}; // camDistTarget, chrOrgOffset_Y, camFovY

using Scales = std::array<float, 3>;

u8* g_image = nullptr;
std::mutex g_mutex;
u8* g_table_base = nullptr;
u32 g_table_rows = 0;
std::vector<Scales> g_orig;
std::vector<Scales> g_written;
Scales g_applied{1.0f, 1.0f, 1.0f};

bool Plausible(const u8* base) {
    if (!base || std::memcmp(base + kTypeAt, kTypeName, sizeof(kTypeName)) != 0) {
        return false;
    }
    u16 rows = 0;
    std::memcpy(&rows, base + kRowCountAt, 2);
    if (rows == 0 || rows > 4096) {
        return false;
    }
    for (u32 r = 0; r < rows; ++r) {
        u64 off = 0;
        std::memcpy(&off, base + kRowsAt + r * kRowRecord + 8, 8);
        if (off < kRowsAt + rows * kRowRecord || off > 0x100000) {
            return false;
        }
        float fov = 0.0f;
        std::memcpy(&fov, base + off + kFovAt, 4);
        if (!(fov > 1.0f && fov < 179.0f)) {
            return false;
        }
    }
    return true;
}

u8* CheckSlot(u64 repo, unsigned slot) {
    std::int32_t count = 0;
    std::memcpy(&count, reinterpret_cast<const void*>(repo + kSlotsAt + slot * kSlotStride), 4);
    if (count <= 0) {
        return nullptr;
    }
    for (unsigned k = 0; k < static_cast<unsigned>(count) && k < kCapsPerSlot; ++k) {
        u64 cap = 0, holder = 0, file = 0;
        std::memcpy(&cap, reinterpret_cast<const void*>(repo + kSlotsAt + slot * kSlotStride + 8 + 8 * k), 8);
        if (!cap) continue;
        std::memcpy(&holder, reinterpret_cast<const void*>(cap + 0x70), 8);
        if (!holder) continue;
        std::memcpy(&file, reinterpret_cast<const void*>(holder + 0x70), 8);
        if (!file) continue;
        auto* base = reinterpret_cast<u8*>(file);
        if (Plausible(base)) {
            return base;
        }
    }
    return nullptr;
}

u8* FindLockCamTable() {
    if (!g_image) {
        return nullptr;
    }
    u64 repo = 0;
    std::memcpy(&repo, g_image + kRepositorySlot, 8);
    if (!repo) {
        return nullptr;
    }
    if (u8* fast = CheckSlot(repo, kLockCamSlot)) {
        return fast;
    }
    for (unsigned slot = 0; slot < kSlots; ++slot) {
        if (slot == kLockCamSlot) continue;
        if (u8* found = CheckSlot(repo, slot)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

void PatchImage(unsigned char* image, std::uint64_t size) {
    if (!image || size <= kRepositorySlot + 8) {
        return;
    }
    g_image = image;
    u8* clamp_site = image + kFovClamp;
    if (std::memcmp(clamp_site, kFovClampBytes, 4) == 0 ||
        std::memcmp(clamp_site, kFovNoClamp, 4) == 0) {
        std::memcpy(clamp_site, kFovNoClamp, 4);
        std::printf("Camera: 48-degree FOV cap lifted at 0x%llx\n",
                    static_cast<unsigned long long>(kFovClamp));
    } else {
        std::printf("Camera: unexpected bytes at FOV clamp 0x%llx; left alone\n",
                    static_cast<unsigned long long>(kFovClamp));
    }
}

void Tick() {
    if (!g_image) {
        return;
    }
    const auto& s = BbSettings::Get();
    const Scales want{s.cam_distance.load(std::memory_order_relaxed),
                      s.cam_height.load(std::memory_order_relaxed),
                      s.fov_scale.load(std::memory_order_relaxed)};
    constexpr Scales kDefault{1.0f, 1.0f, 1.0f};
    if (want == kDefault && g_applied == kDefault) {
        return;
    }
    std::unique_lock<std::mutex> lock(g_mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return;
    }
    if (!g_table_base || !Plausible(g_table_base)) {
        g_table_base = FindLockCamTable();
        if (!g_table_base) {
            return;
        }
        u16 rows = 0;
        std::memcpy(&rows, g_table_base + kRowCountAt, 2);
        g_table_rows = rows;
        g_orig.assign(rows, Scales{0.0f, 0.0f, 0.0f});
        g_written.assign(rows, Scales{-1.0f, -1.0f, -1.0f});
        std::printf("Camera: LockCamParam located at %p (%u rows)\n",
                    static_cast<void*>(g_table_base), g_table_rows);
    }
    for (u32 r = 0; r < g_table_rows; ++r) {
        u64 off = 0;
        std::memcpy(&off, g_table_base + kRowsAt + r * kRowRecord + 8, 8);
        for (int f = 0; f < 3; ++f) {
            auto* v = reinterpret_cast<float*>(g_table_base + off + kFieldAt[f]);
            if (*v != g_written[r][f]) {
                g_orig[r][f] = *v;
            }
            const float target = g_orig[r][f] * want[f];
            if (*v != target) {
                *v = target;
            }
            g_written[r][f] = target;
        }
    }
    g_applied = want;
}

} // namespace BbCamera
