// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_memory_dev.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace BbMemoryDev {

namespace {

std::atomic<Preset> s_preset{Preset::High};
std::atomic<bool> s_initialized{false};
std::atomic<uint64_t> s_evicted_count{0};
std::atomic<uint64_t> s_reclaimed_bytes{0};
std::atomic<uint64_t> s_clamped_count{0};
std::atomic<uint64_t> s_vram_limit_bytes{0};
std::atomic<int> s_auto_mip_clamp{-1}; // -1 = auto, 0 = disabled, 1 = enabled

struct MemoryRange {
    uintptr_t start;
    uintptr_t end;
};

std::mutex s_player_ranges_mutex;
std::vector<MemoryRange> s_player_ranges;

Preset ParsePreset(const char* val) {
    if (!val || val[0] == '\0') {
        return Preset::High;
    }
    if (std::strcmp(val, "low") == 0 || std::strcmp(val, "2") == 0 || std::strcmp(val, "apu") == 0) {
        return Preset::Low;
    }
    if (std::strcmp(val, "medium") == 0 || std::strcmp(val, "med") == 0 ||
        std::strcmp(val, "1") == 0) {
        return Preset::Medium;
    }
    return Preset::High;
}

uint64_t ParseBytes(const char* str) {
    if (!str || !*str) return 0;
    while (*str == ' ' || *str == '\t') ++str;
    char* end = nullptr;
    double val = std::strtod(str, &end);
    if (end == str || val <= 0.0) return 0;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end == 'g' || *end == 'G') {
        return static_cast<uint64_t>(val * 1024.0 * 1024.0 * 1024.0);
    }
    if (*end == 'm' || *end == 'M') {
        return static_cast<uint64_t>(val * 1024.0 * 1024.0);
    }
    if (*end == 'k' || *end == 'K') {
        return static_cast<uint64_t>(val * 1024.0);
    }
    // Bare number: if <= 32, interpret as GiB (e.g. 2 -> 2GB). Otherwise MiB.
    if (val <= 32.0) {
        return static_cast<uint64_t>(val * 1024.0 * 1024.0 * 1024.0);
    }
    return static_cast<uint64_t>(val * 1024.0 * 1024.0);
}

void ReadIniSettings() {
    const char* config_path = std::getenv("BB_CONFIG");
    if (!config_path || !*config_path) {
        config_path = "bbport.ini";
    }
    FILE* f = std::fopen(config_path, "r");
    if (!f) return;

    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        char* hash = std::strchr(line, '#');
        if (hash) *hash = '\0';
        char* semicolon = std::strchr(line, ';');
        if (semicolon) *semicolon = '\0';

        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';

        char* key = line;
        while (*key == ' ' || *key == '\t') ++key;
        char* k_end = key + std::strlen(key) - 1;
        while (k_end > key && (*k_end == ' ' || *k_end == '\t' || *k_end == '\r' || *k_end == '\n')) {
            *k_end-- = '\0';
        }

        char* val = eq + 1;
        while (*val == ' ' || *val == '\t') ++val;
        char* v_end = val + std::strlen(val) - 1;
        while (v_end > val && (*v_end == ' ' || *v_end == '\t' || *v_end == '\r' || *v_end == '\n')) {
            *v_end-- = '\0';
        }

        if (std::strcmp(key, "bb_vram_limit") == 0 || std::strcmp(key, "vram_limit") == 0) {
            uint64_t bytes = ParseBytes(val);
            if (bytes > 0) {
                s_vram_limit_bytes.store(bytes, std::memory_order_relaxed);
            }
        } else if (std::strcmp(key, "bb_auto_mip_clamp") == 0 || std::strcmp(key, "auto_mip_clamp") == 0) {
            if (std::strcmp(val, "1") == 0 || std::strcmp(val, "true") == 0 || std::strcmp(val, "on") == 0) {
                s_auto_mip_clamp.store(1, std::memory_order_relaxed);
            } else if (std::strcmp(val, "0") == 0 || std::strcmp(val, "false") == 0 || std::strcmp(val, "off") == 0) {
                s_auto_mip_clamp.store(0, std::memory_order_relaxed);
            }
        } else if (std::strcmp(key, "bb_memory_preset") == 0 || std::strcmp(key, "memory_preset") == 0) {
            s_preset.store(ParsePreset(val), std::memory_order_relaxed);
        }
    }
    std::fclose(f);
}

} // namespace

void Initialize(const char* preset_name) {
    if (s_initialized.exchange(true, std::memory_order_relaxed)) {
        return;
    }

    // 1. Read from ini first as baseline
    ReadIniSettings();

    const char* active_p = nullptr;
    if (preset_name && preset_name[0] != '\0') {
        active_p = preset_name;
        s_preset.store(ParsePreset(preset_name), std::memory_order_relaxed);
    } else {
        const char* env = std::getenv("BB_MEMORY_PRESET");
        if (env && env[0] != '\0') {
            active_p = env;
            s_preset.store(ParsePreset(env), std::memory_order_relaxed);
        }
    }
    if (active_p && std::strcmp(active_p, "apu") == 0) {
        if (s_vram_limit_bytes.load(std::memory_order_relaxed) == 0) {
            s_vram_limit_bytes.store(2048ULL << 20, std::memory_order_relaxed);
        }
        s_auto_mip_clamp.store(1, std::memory_order_relaxed);
    }

    const char* env_vram = std::getenv("BB_VRAM_LIMIT");
    if (!env_vram || !*env_vram) {
        env_vram = std::getenv("BB_VRAM_LIMIT_MB");
    }
    if (env_vram && *env_vram) {
        uint64_t bytes = ParseBytes(env_vram);
        if (bytes > 0) {
            s_vram_limit_bytes.store(bytes, std::memory_order_relaxed);
        }
    }

    const char* env_mip = std::getenv("BB_AUTO_MIP_CLAMP");
    if (env_mip && *env_mip) {
        if (std::strcmp(env_mip, "1") == 0 || std::strcmp(env_mip, "true") == 0 || std::strcmp(env_mip, "on") == 0) {
            s_auto_mip_clamp.store(1, std::memory_order_relaxed);
        } else if (std::strcmp(env_mip, "0") == 0 || std::strcmp(env_mip, "false") == 0 || std::strcmp(env_mip, "off") == 0) {
            s_auto_mip_clamp.store(0, std::memory_order_relaxed);
        }
    }

    const Preset p = s_preset.load(std::memory_order_relaxed);
    const char* p_str = (p == Preset::Low) ? "Low (4 GB VRAM / Aggressive RAM Eviction)"
                      : (p == Preset::Medium) ? "Medium (6-8 GB VRAM / Balanced)"
                                              : "High (12+ GB VRAM / Default BBPort)";

    uint64_t limit_bytes = s_vram_limit_bytes.load(std::memory_order_relaxed);
    if (limit_bytes > 0) {
        std::printf("[memoryDev] Memory & VRAM Preset: %s | VRAM limit: %llu MiB (%s)\n",
                    p_str, (unsigned long long)(limit_bytes >> 20),
                    IsAutoMipClampEnabled() ? "Auto Mip-Clamp ON" : "Auto Mip-Clamp OFF");
    } else {
        std::printf("[memoryDev] Memory & VRAM Preset initialized: %s\n", p_str);
    }
}

Preset GetPreset() {
    if (!s_initialized.load(std::memory_order_relaxed)) {
        Initialize();
    }
    return s_preset.load(std::memory_order_relaxed);
}

const char* GetPresetName() {
    switch (GetPreset()) {
    case Preset::Low:
        return "low";
    case Preset::Medium:
        return "medium";
    case Preset::High:
    default:
        return "high";
    }
}

bool IsHigh() {
    return GetPreset() == Preset::High;
}

bool IsMedium() {
    return GetPreset() == Preset::Medium;
}

bool IsLow() {
    return GetPreset() == Preset::Low;
}

uint64_t GetGcIdleSeconds() {
    const char* env = std::getenv("BB_GC_IDLE_SECONDS");
    if (env && env[0] != '\0') {
        char* end = nullptr;
        unsigned long long v = std::strtoull(env, &end, 10);
        if (end != env && v > 0) {
            return static_cast<uint64_t>(v);
        }
    }

    switch (GetPreset()) {
    case Preset::Low:
        return 4; // Fast recycling of distant/idle rooms
    case Preset::Medium:
        return 10;
    case Preset::High:
    default:
        return 20; // Default BBPort behavior
    }
}

uint64_t GetGcPressureIdleSeconds() {
    const char* env = std::getenv("BB_GC_PRESSURE_IDLE_SECONDS");
    if (env && env[0] != '\0') {
        char* end = nullptr;
        unsigned long long v = std::strtoull(env, &end, 10);
        if (end != env) {
            return static_cast<uint64_t>(v);
        }
    }

    switch (GetPreset()) {
    case Preset::Low:
        return 2; // Immediate purge under pressure
    case Preset::Medium:
        return 3;
    case Preset::High:
    default:
        return 5; // Default BBPort behavior
    }
}

uint64_t GetGcBudgetBytes() {
    uint64_t limit = s_vram_limit_bytes.load(std::memory_order_relaxed);
    if (limit > 0) {
        return limit;
    }

    const char* env = std::getenv("BB_GC_BUDGET_MB");
    if (env && env[0] != '\0') {
        char* end = nullptr;
        unsigned long long v = std::strtoull(env, &end, 10);
        if (end != env && v > 0) {
            return static_cast<uint64_t>(v) << 20;
        }
    }

    switch (GetPreset()) {
    case Preset::Low:
        // Default ceiling for low-end 4GB GPUs or APUs
        return 2048ULL * 1024ULL * 1024ULL;
    case Preset::Medium:
        return 5500ULL * 1024ULL * 1024ULL;
    case Preset::High:
    default:
        return 0; // 0 = rely on driver memory budget directly
    }
}

uint64_t GetVramLimitBytes() {
    if (!s_initialized.load(std::memory_order_relaxed)) {
        Initialize();
    }
    return s_vram_limit_bytes.load(std::memory_order_relaxed);
}

uint64_t GetVramLimitMb() {
    return GetVramLimitBytes() >> 20;
}

void SetVramLimitMb(uint64_t mb) {
    s_vram_limit_bytes.store(mb << 20, std::memory_order_relaxed);
}

bool IsAutoMipClampEnabled() {
    if (!s_initialized.load(std::memory_order_relaxed)) {
        Initialize();
    }
    int val = s_auto_mip_clamp.load(std::memory_order_relaxed);
    if (val != -1) {
        return val == 1;
    }
    // Auto: enabled when preset is Low, or when user sets VRAM limit <= 4096 MB
    uint64_t limit_mb = GetVramLimitMb();
    if (limit_mb > 0 && limit_mb <= 4096) {
        return true;
    }
    return GetPreset() == Preset::Low;
}

void SetAutoMipClamp(bool enabled) {
    s_auto_mip_clamp.store(enabled ? 1 : 0, std::memory_order_relaxed);
}

bool ShouldClampTexture(uint32_t levels, bool is_block, uint32_t width, uint32_t height,
                         bool is_ui, bool is_player, bool is_target, bool is_storage,
                         bool is_depth) {
    if (!IsAutoMipClampEnabled()) {
        return false;
    }
    // Only clamp textures with mip levels that can be stepped down
    if (levels <= 1) {
        return false;
    }
    // Do NOT clamp storage images, render targets, or depth targets
    if (is_storage || is_target || is_depth) {
        return false;
    }
    // Do NOT clamp UI textures or Player textures ("mantem ui e player ligado")
    if (is_ui || is_player) {
        return false;
    }
    // UI elements, fonts and LUTs are uncompressed: exempt them
    if (!is_block) {
        return false;
    }
    // Exempt small textures (icons, decals, eyes, hair highlights < 256x256)
    if (width < 256 || height < 256) {
        return false;
    }
    return true;
}

void RegisterPlayerMemoryRange(uintptr_t start, uint64_t size) {
    if (start == 0 || size == 0) return;
    std::lock_guard<std::mutex> lock(s_player_ranges_mutex);
    s_player_ranges.push_back({start, start + size});
}

bool IsPlayerMemory(uintptr_t address) {
    if (address == 0) return false;
    std::lock_guard<std::mutex> lock(s_player_ranges_mutex);
    for (const auto& r : s_player_ranges) {
        if (address >= r.start && address < r.end) {
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
        .evicted_to_ram_count = s_evicted_count.load(std::memory_order_relaxed),
        .reclaimed_bytes = s_reclaimed_bytes.load(std::memory_order_relaxed),
        .clamped_texture_count = s_clamped_count.load(std::memory_order_relaxed),
    };
}

void RecordEviction(uint64_t bytes) {
    s_evicted_count.fetch_add(1, std::memory_order_relaxed);
    s_reclaimed_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void RecordClampedTexture() {
    s_clamped_count.fetch_add(1, std::memory_order_relaxed);
}

} // namespace BbMemoryDev

extern "C" void extra_memory_dev_register_player(uintptr_t start, uint64_t size) {
    BbMemoryDev::RegisterPlayerMemoryRange(start, size);
}
