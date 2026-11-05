// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the renderer asks the memory preset -- texture-cache idle times, the VRAM budget, the
// auto mip-clamp's verdict per texture, and whether an address belongs to the player. The settings
// are read in bbport_memory_dev_settings.cpp.
#include "bbport_memory_dev.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "bbport_memory_dev_internal.h"

namespace BbMemoryDev {
namespace {

using detail::PresetValue;

/// An address range the player's assets live in (the c0000 character and its equipment).
struct MemoryRange {
    std::uintptr_t start;
    std::uintptr_t end;
};

std::mutex s_player_ranges_mutex;
std::vector<MemoryRange> s_player_ranges;

/// Reads a decimal environment variable; false when it is absent or not a number.
bool EnvCount(const char* name, std::uint64_t& out) {
    const char* env = std::getenv(name);
    if (!env || env[0] == '\0') {
        return false;
    }
    char* end = nullptr;
    const unsigned long long value = std::strtoull(env, &end, 10);
    if (end == env) {
        return false;
    }
    out = static_cast<std::uint64_t>(value);
    return true;
}

} // namespace

bool IsHigh() {
    return GetPreset() == Preset::High;
}

bool IsMedium() {
    return GetPreset() == Preset::Medium;
}

bool IsLow() {
    return GetPreset() == Preset::Low;
}

std::uint64_t GetGcIdleSeconds() {
    std::uint64_t env = 0;
    if (EnvCount("BB_GC_IDLE_SECONDS", env) && env > 0) {
        return env;
    }
    return PresetValue(detail::kGcIdleSeconds, GetPreset());
}

std::uint64_t GetGcPressureIdleSeconds() {
    std::uint64_t env = 0;
    if (EnvCount("BB_GC_PRESSURE_IDLE_SECONDS", env)) {
        return env;
    }
    return PresetValue(detail::kGcPressureIdleSeconds, GetPreset());
}

std::uint64_t GetGcBudgetBytes() {
    const std::uint64_t limit = detail::g_vram_limit_bytes.load(std::memory_order_relaxed);
    if (limit > 0) {
        return limit;
    }
    std::uint64_t env_mb = 0;
    if (EnvCount("BB_GC_BUDGET_MB", env_mb) && env_mb > 0) {
        return env_mb * detail::kMebibyte;
    }
    return PresetValue(detail::kGcBudgetBytes, GetPreset());
}

std::uint64_t GetVramLimitBytes() {
    if (!detail::g_initialized.load(std::memory_order_relaxed)) {
        Initialize();
    }
    return detail::g_vram_limit_bytes.load(std::memory_order_relaxed);
}

std::uint64_t GetVramLimitMb() {
    return GetVramLimitBytes() >> 20;
}

void SetVramLimitMb(std::uint64_t mb) {
    detail::g_vram_limit_bytes.store(mb << 20, std::memory_order_relaxed);
}

bool IsAutoMipClampEnabled() {
    if (!detail::g_initialized.load(std::memory_order_relaxed)) {
        Initialize();
    }
    const int forced = detail::g_auto_mip_clamp.load(std::memory_order_relaxed);
    if (forced != -1) {
        return forced == 1;
    }
    // Auto: on for the low-end preset, or whenever the VRAM limit is at most 4 GiB.
    const std::uint64_t limit_mb = GetVramLimitMb();
    if (limit_mb > 0 && limit_mb <= detail::kAutoMipClampVramLimitMb) {
        return true;
    }
    return GetPreset() == Preset::Low;
}

void SetAutoMipClamp(bool enabled) {
    detail::g_auto_mip_clamp.store(enabled ? 1 : 0, std::memory_order_relaxed);
}

bool ShouldClampTexture(std::uint32_t levels, bool is_block, std::uint32_t width,
                        std::uint32_t height, bool is_ui, bool is_player, bool is_target,
                        bool is_storage, bool is_depth) {
    if (!IsAutoMipClampEnabled()) {
        return false;
    }
    // Only textures with a mip chain can be stepped down.
    if (levels <= 1) {
        return false;
    }
    // Storage images, render targets and depth targets are addressed by their base level.
    if (is_storage || is_target || is_depth) {
        return false;
    }
    // UI and the player's own textures stay as they are.
    if (is_ui || is_player) {
        return false;
    }
    // Uncompressed textures are the UI's fonts and LUTs, not world detail.
    if (!is_block) {
        return false;
    }
    // Icons, decals, eyes and hair highlights are already small.
    if (width < detail::kMinClampTextureSide || height < detail::kMinClampTextureSide) {
        return false;
    }
    return true;
}

void RegisterPlayerMemoryRange(std::uintptr_t start, std::uint64_t size) {
    if (start == 0 || size == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(s_player_ranges_mutex);
    s_player_ranges.push_back({start, start + size});
}

bool IsPlayerMemory(std::uintptr_t address) {
    if (address == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_player_ranges_mutex);
    for (const MemoryRange& range : s_player_ranges) {
        if (address >= range.start && address < range.end) {
            return true;
        }
    }
    return false;
}

bool ShouldAggressivelyEvictToRam() {
    return GetPreset() == Preset::Low || GetVramLimitBytes() != 0;
}

MemoryDevStats GetStats() {
    return MemoryDevStats{
        .vram_budget_bytes = GetGcBudgetBytes(),
        .vram_used_bytes = 0,
        .evicted_to_ram_count = detail::g_evicted_count.load(std::memory_order_relaxed),
        .reclaimed_bytes = detail::g_reclaimed_bytes.load(std::memory_order_relaxed),
        .clamped_texture_count = detail::g_clamped_count.load(std::memory_order_relaxed),
    };
}

void RecordEviction(std::uint64_t bytes) {
    detail::g_evicted_count.fetch_add(1, std::memory_order_relaxed);
    detail::g_reclaimed_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void RecordClampedTexture() {
    detail::g_clamped_count.fetch_add(1, std::memory_order_relaxed);
}

} // namespace BbMemoryDev

extern "C" void extra_memory_dev_register_player(uintptr_t start, uint64_t size) {
    BbMemoryDev::RegisterPlayerMemoryRange(start, size);
}
