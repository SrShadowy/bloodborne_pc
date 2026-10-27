// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace BbMemoryDev {

enum class Preset : int {
    High = 0,    // High / Recommended (8+ GB VRAM, relaxed retention, maximum quality)
    Medium = 1,  // Balanced (6 GB VRAM, moderate swap to RAM, GC 10s)
    Low = 2,     // Low-end (4 GB VRAM / APU, aggressive eviction to RAM, GC 4s)
};

/// Initializes the memory preset from environment (BB_MEMORY_PRESET) or ini configuration.
void Initialize(const char* preset_name = nullptr);

/// Gets the active memory preset.
Preset GetPreset();

/// Returns a human-readable name of the active preset ("high", "medium", "low").
const char* GetPresetName();

/// Returns true if the active preset is High (default upstream behavior).
bool IsHigh();

/// Returns true if the active preset is Medium.
bool IsMedium();

/// Returns true if the active preset is Low.
bool IsLow();

/// Recommended Texture Cache idle seconds before an unused image is candidate for eviction to RAM.
uint64_t GetGcIdleSeconds();

/// Recommended Texture Cache idle seconds when VRAM is under pressure.
uint64_t GetGcPressureIdleSeconds();

/// Recommended VRAM GC budget ceiling in bytes (0 = driver default/unconstrained).
uint64_t GetGcBudgetBytes();

/// Returns user-defined VRAM limit in bytes (e.g. bb_vram_limit = 2GB), or 0 if unconstrained.
uint64_t GetVramLimitBytes();

/// Returns user-defined VRAM limit in MiB (e.g. 2048 for 2GB), or 0 if unconstrained.
uint64_t GetVramLimitMb();

/// Sets the VRAM limit in MiB.
void SetVramLimitMb(uint64_t mb);

/// Returns true if Auto Mip-Clamp is active (skip mip 0 for world textures to save bandwidth & VRAM).
bool IsAutoMipClampEnabled();

/// Sets whether Auto Mip-Clamp is enabled.
void SetAutoMipClamp(bool enabled);

/// Evaluates if a texture should skip mip 0 (clamped).
/// Exempts UI (uncompressed, 1 level, HUD, fonts), Player/Hunter assets, storage, render targets, depth.
bool ShouldClampTexture(uint32_t levels, bool is_block, uint32_t width, uint32_t height,
                         bool is_ui, bool is_player, bool is_target, bool is_storage,
                         bool is_depth);

/// Registers a memory range belonging to the Player character or equipment (c0000, parts).
void RegisterPlayerMemoryRange(uintptr_t start, uint64_t size);

/// Returns true if address falls within a registered Player memory range.
bool IsPlayerMemory(uintptr_t address);

/// Returns whether texture eviction to system RAM should be accelerated when under pressure.
bool ShouldAggressivelyEvictToRam();

/// Telemetry metrics for HUD / diagnostics
struct MemoryDevStats {
    uint64_t vram_budget_bytes;
    uint64_t vram_used_bytes;
    uint64_t evicted_to_ram_count;
    uint64_t reclaimed_bytes;
    uint64_t clamped_texture_count;
};

MemoryDevStats GetStats();
void RecordEviction(uint64_t bytes);
void RecordClampedTexture();

} // namespace BbMemoryDev
