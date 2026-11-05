// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the memory-dev files share. bbport_memory_dev_settings.cpp reads the preset, the VRAM
// limit and the auto mip-clamp from the environment and bbport.ini; bbport_memory_dev.cpp answers
// the renderer's questions from that state. Only this module needs this header.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "bbport_memory_dev.h"

namespace BbMemoryDev::detail {

// ---- Sizes ----

/// One MiB, in bytes.
constexpr std::uint64_t kMebibyte = 1024ULL * 1024ULL;
/// One GiB, in bytes.
constexpr std::uint64_t kGibibyte = 1024ULL * kMebibyte;

/// A bare number at or below this is read as GiB (2 means 2 GB); above it, as MiB.
constexpr double kBareNumberGiBCutoff = 32.0;
/// The VRAM an APU preset assumes when no limit was given.
constexpr std::uint64_t kApuVramLimitBytes = 2048ULL * kMebibyte;
/// At or below this VRAM limit, the auto mip-clamp turns itself on.
constexpr std::uint64_t kAutoMipClampVramLimitMb = 4096;
/// Textures smaller than this on either side keep their mip 0 (icons, decals, hair).
constexpr std::uint32_t kMinClampTextureSide = 256;
/// One line of bbport.ini.
constexpr std::size_t kIniLineBytes = 512;

// ---- Per-preset defaults (indexed by Preset: High, Medium, Low) ----

/// Seconds an unused texture cache entry lives before it may move to RAM.
constexpr std::uint64_t kGcIdleSeconds[] = {20, 10, 4};
/// The same, while VRAM is under pressure.
constexpr std::uint64_t kGcPressureIdleSeconds[] = {5, 3, 2};
/// The VRAM budget ceiling; 0 leaves it to the driver.
constexpr std::uint64_t kGcBudgetBytes[] = {0, 5500ULL * kMebibyte, 2048ULL * kMebibyte};

/// The value a preset's row of one of the tables above holds.
template <typename T, std::size_t N>
T PresetValue(const T (&table)[N], Preset preset) {
    const auto index = static_cast<std::size_t>(preset);
    return table[index < N ? index : 0];
}

// ---- State ----

extern std::atomic<Preset> g_preset;
extern std::atomic<bool> g_initialized;
extern std::atomic<std::uint64_t> g_evicted_count;
extern std::atomic<std::uint64_t> g_reclaimed_bytes;
extern std::atomic<std::uint64_t> g_clamped_count;
extern std::atomic<std::uint64_t> g_vram_limit_bytes;
/// -1: decide from the preset and the limit; 0: off; 1: on.
extern std::atomic<int> g_auto_mip_clamp;

/// Starts reading the settings, once.
void InitializeOnce();

/// "low"/"2"/"apu", "medium"/"med"/"1", else High.
Preset ParsePreset(const char* value);

/// "2G", "2048M", "512K" or a bare number (GiB when small, else MiB); 0 when unreadable.
std::uint64_t ParseBytes(const char* text);

/// The bbport.ini settings (BB_CONFIG, else bbport.ini); leaves what it does not find alone.
void ReadIniSettings();

/// The outcome of parsing one setting value.
enum class Toggle { NoChange, Off, On };

/// "1"/"true"/"on" and "0"/"false"/"off"; anything else leaves the setting alone.
Toggle ParseToggle(const char* value);

} // namespace BbMemoryDev::detail
