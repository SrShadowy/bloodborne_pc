// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the public side of the button prompts. The game's icons are known by their bytes
// (bbport_prompt_table.cpp), the replacements are drawn in bbport_prompt_draw.cpp, and this file
// answers the game's lookups and rebuilds the icon set when the device changes.
#include "bbport_button_prompts.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include <xxhash.h>

#include "bbport_prompt_internal.h"

extern "C" {
// runtime_pad.c
unsigned runtime_pad_prompt_generation(void);
int runtime_pad_prompt_device(void);
__attribute__((weak)) int runtime_pad_is_nintendo(void);
}

namespace BbPrompts {
namespace {

using detail::kIconCount;
using detail::Switch;
/// The device names the prompts log (and the settings window shows).
const char* DeviceName(int device) {
    if (device == 0) {
        return "the game's (PlayStation pad)";
    }
    if (device == 1) {
        const bool is_nintendo = runtime_pad_is_nintendo && runtime_pad_is_nintendo();
        return is_nintendo ? "the gamepad's (Nintendo layout)" : "the gamepad's (Xbox layout)";
    }
    if (device == 2) {
        return "the keyboard's and mouse's";
    }
    return "?";
}

} // namespace

void Start() {
    detail::StartLoader();
}

bool Enabled() {
    if (!Switch()) {
        return false;
    }
    detail::WaitTable();
    const auto& hashes = detail::g_icon_hashes;
    return std::any_of(hashes.begin(), hashes.end(), [](std::uint64_t h) { return h != 0; });
}

const std::uint8_t* IconBytes(std::uint32_t icon) {
    if (icon < 1 || icon > kIconCount || !detail::g_icon_hashes[icon - 1]) {
        return nullptr;
    }
    return detail::g_icon_bytes[icon - 1].data();
}

std::uint32_t IconOf(const void* data, std::size_t size) {
    if (!Switch() || size < detail::kGameIconBytes) {
        return 0;
    }
    detail::WaitTable();
    const std::uint64_t hash = XXH3_64bits(data, detail::kGameIconBytes);
    for (std::size_t i = 0; i < kIconCount; ++i) {
        if (detail::g_icon_hashes[i] != 0 && detail::g_icon_hashes[i] == hash) {
            return std::uint32_t(i + 1);
        }
    }
    return 0;
}

std::uint32_t AtlasOf(const void* data, std::size_t size, std::uint32_t width, std::uint32_t height) {
    if (!Switch()) {
        return 0;
    }
    for (std::size_t k = 0; k < detail::kAtlasCount; ++k) {
        const detail::AtlasDef& atlas = detail::kAtlases[k];
        if (atlas.width != width || atlas.height != height || size < atlas.bytes) {
            continue;
        }
        detail::WaitTable();
        if (detail::g_atlas_hashes[k] && !detail::g_atlas_cells[k].empty() &&
            XXH3_64bits(data, atlas.bytes) == detail::g_atlas_hashes[k]) {
            return std::uint32_t(k + 1);
        }
    }
    return 0;
}

std::span<const AtlasCell> AtlasCells(std::uint32_t atlas) {
    if (atlas == 0 || atlas > detail::kAtlasCount || !detail::g_table_ready) {
        return {};
    }
    return detail::g_atlas_cells[atlas - 1];
}

bool Update(std::uint32_t generation, Set& out) {
    if (!Switch()) {
        return false;
    }
    const std::uint32_t now = runtime_pad_prompt_generation();
    if (now == generation) {
        return false;
    }
    out.generation = now;
    out.count = std::uint32_t(kIconCount);
    out.drawn.assign(kIconCount, false);
    out.pixels.assign(kIconCount * IconSize * IconSize, 0);

    const auto& icons = detail::kIcons;
    for (std::size_t i = 0; i < kIconCount; ++i) {
        if (detail::g_icon_hashes[i] == 0 ||
            !detail::DrawIcon(icons[i], out.pixels.data() + i * IconSize * IconSize)) {
            continue;
        }
        out.drawn[i] = true;
    }

    std::printf("Button prompts: %s\n", DeviceName(runtime_pad_prompt_device()));
    return true;
}

} // namespace BbPrompts
