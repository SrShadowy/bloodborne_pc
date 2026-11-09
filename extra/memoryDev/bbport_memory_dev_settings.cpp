// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: where the memory preset comes from -- BB_MEMORY_PRESET, bbport.ini (BB_CONFIG), then the
// individual BB_VRAM_LIMIT / BB_AUTO_MIP_CLAMP overrides -- and how their values are parsed.
// bbport_memory_dev.cpp answers the renderer's questions from the state set here.
#include "bbport_memory_dev_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace BbMemoryDev {
namespace detail {

std::atomic<Preset> g_preset{Preset::High};
std::atomic<bool> g_initialized{false};
std::atomic<std::uint64_t> g_evicted_count{0};
std::atomic<std::uint64_t> g_reclaimed_bytes{0};
std::atomic<std::uint64_t> g_clamped_count{0};
std::atomic<std::uint64_t> g_vram_limit_bytes{0};
std::atomic<int> g_auto_mip_clamp{-1};

Preset ParsePreset(const char* value) {
    if (!value || value[0] == '\0') {
        return Preset::High;
    }
    if (std::strcmp(value, "low") == 0 || std::strcmp(value, "2") == 0 ||
        std::strcmp(value, "apu") == 0) {
        return Preset::Low;
    }
    if (std::strcmp(value, "medium") == 0 || std::strcmp(value, "med") == 0 ||
        std::strcmp(value, "1") == 0) {
        return Preset::Medium;
    }
    return Preset::High;
}

std::uint64_t ParseBytes(const char* text) {
    if (!text || !*text) {
        return 0;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || value <= 0.0) {
        return 0;
    }
    while (*end == ' ' || *end == '\t') {
        ++end;
    }
    const auto scale = [&](double unit) { return static_cast<std::uint64_t>(value * unit); };
    if (*end == 'g' || *end == 'G') {
        return scale(double(kGibibyte));
    }
    if (*end == 'm' || *end == 'M') {
        return scale(double(kMebibyte));
    }
    if (*end == 'k' || *end == 'K') {
        return scale(1024.0);
    }
    return value <= kBareNumberGiBCutoff ? scale(double(kGibibyte)) : scale(double(kMebibyte));
}

Toggle ParseToggle(const char* value) {
    if (!value) {
        return Toggle::NoChange;
    }
    if (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0 ||
        std::strcmp(value, "on") == 0) {
        return Toggle::On;
    }
    if (std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
        std::strcmp(value, "off") == 0) {
        return Toggle::Off;
    }
    return Toggle::NoChange;
}

namespace {

/// Trims a key or a value in place, returning where it starts.
char* Trim(char* text) {
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    char* end = text + std::strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return text;
}

/// Applies one "key = value" line; keys are accepted with and without the bb_ prefix.
void ApplySetting(const char* key, const char* value) {
    const bool vram_limit = std::strcmp(key, "bb_vram_limit") == 0 ||
                            std::strcmp(key, "vram_limit") == 0;
    const bool auto_mip = std::strcmp(key, "bb_auto_mip_clamp") == 0 ||
                          std::strcmp(key, "auto_mip_clamp") == 0;
    const bool preset = std::strcmp(key, "bb_memory_preset") == 0 ||
                        std::strcmp(key, "memory_preset") == 0;

    if (vram_limit) {
        const std::uint64_t bytes = ParseBytes(value);
        if (bytes > 0) {
            g_vram_limit_bytes.store(bytes, std::memory_order_relaxed);
        }
    } else if (auto_mip) {
        switch (ParseToggle(value)) {
        case Toggle::On:
            g_auto_mip_clamp.store(1, std::memory_order_relaxed);
            break;
        case Toggle::Off:
            g_auto_mip_clamp.store(0, std::memory_order_relaxed);
            break;
        case Toggle::NoChange:
            break;
        }
    } else if (preset) {
        g_preset.store(ParsePreset(value), std::memory_order_relaxed);
    }
}

} // namespace

void ReadIniSettings() {
    const char* config_path = std::getenv("BB_CONFIG");
    if (!config_path || !*config_path) {
        config_path = "bbport.ini";
    }
    FILE* file = std::fopen(config_path, "r");
    if (!file) {
        return;
    }

    char line[kIniLineBytes];
    while (std::fgets(line, sizeof(line), file)) {
        // '#' and ';' start a comment, always.
        if (char* hash = std::strchr(line, '#')) {
            *hash = '\0';
        }
        if (char* semicolon = std::strchr(line, ';')) {
            *semicolon = '\0';
        }
        char* eq = std::strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        char* const key = Trim(line);
        char* const value = Trim(eq + 1);
        ApplySetting(key, value);
    }
    std::fclose(file);
}

} // namespace detail

void Initialize(const char* preset_name) {
    if (detail::g_initialized.exchange(true, std::memory_order_relaxed)) {
        return;
    }

    // 1. The ini is the baseline.
    detail::ReadIniSettings();

    // 2. The preset named by the launcher, else by BB_MEMORY_PRESET.
    const char* active_preset = nullptr;
    if (preset_name && preset_name[0] != '\0') {
        active_preset = preset_name;
        detail::g_preset.store(detail::ParsePreset(preset_name), std::memory_order_relaxed);
    } else if (const char* env = std::getenv("BB_MEMORY_PRESET"); env && env[0] != '\0') {
        active_preset = env;
        detail::g_preset.store(detail::ParsePreset(env), std::memory_order_relaxed);
    }
    if (active_preset && std::strcmp(active_preset, "apu") == 0) {
        // An APU shares the system's RAM as VRAM: keep the budget small and mip 0 out.
        if (detail::g_vram_limit_bytes.load(std::memory_order_relaxed) == 0) {
            detail::g_vram_limit_bytes.store(detail::kApuVramLimitBytes, std::memory_order_relaxed);
        }
        detail::g_auto_mip_clamp.store(1, std::memory_order_relaxed);
    }

    // 3. The individual overrides, which win over the preset.
    const char* env_vram = std::getenv("BB_VRAM_LIMIT");
    if (!env_vram || !*env_vram) {
        env_vram = std::getenv("BB_VRAM_LIMIT_MB");
    }
    if (env_vram && *env_vram) {
        const std::uint64_t bytes = detail::ParseBytes(env_vram);
        if (bytes > 0) {
            detail::g_vram_limit_bytes.store(bytes, std::memory_order_relaxed);
        }
    }
    if (const char* env_mip = std::getenv("BB_AUTO_MIP_CLAMP"); env_mip && *env_mip) {
        switch (detail::ParseToggle(env_mip)) {
        case detail::Toggle::On:
            detail::g_auto_mip_clamp.store(1, std::memory_order_relaxed);
            break;
        case detail::Toggle::Off:
            detail::g_auto_mip_clamp.store(0, std::memory_order_relaxed);
            break;
        case detail::Toggle::NoChange:
            break;
        }
    }

    const char* const preset_text =
        GetPreset() == Preset::Low      ? "Low (4 GB VRAM / Aggressive RAM Eviction)"
        : GetPreset() == Preset::Medium ? "Medium (6-8 GB VRAM / Balanced)"
                                        : "High (12+ GB VRAM / Default BBPort)";
    const std::uint64_t limit = detail::g_vram_limit_bytes.load(std::memory_order_relaxed);
    if (limit > 0) {
        std::printf("[memoryDev] Memory & VRAM Preset: %s | VRAM limit: %llu MiB (%s)\n", preset_text,
                    static_cast<unsigned long long>(limit >> 20),
                    IsAutoMipClampEnabled() ? "Auto Mip-Clamp ON" : "Auto Mip-Clamp OFF");
    } else {
        std::printf("[memoryDev] Memory & VRAM Preset initialized: %s\n", preset_text);
    }
}

Preset GetPreset() {
    if (!detail::g_initialized.load(std::memory_order_relaxed)) {
        Initialize();
    }
    return detail::g_preset.load(std::memory_order_relaxed);
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

} // namespace BbMemoryDev
