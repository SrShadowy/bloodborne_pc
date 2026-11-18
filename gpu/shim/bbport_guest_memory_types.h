// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Pure memory-type selection for BbGuestMemory (header-only so unit tests run without a device).
#pragma once

#include <cstdint>
#include "vulkan/vk_common.h"

namespace BbGuestMemory::detail {

/// Returns whether a Vulkan memory type's property flags match BB_GUEST_UNCACHED.
[[nodiscard]] inline bool GuestMemoryTypeAllowed(vk::MemoryPropertyFlags flags, bool uncached) {
    return bool(flags & vk::MemoryPropertyFlagBits::eDeviceUncachedAMD) == uncached;
}

/// Picks a memory type index from `props` allowed by `bits`.
/// When `in_vram` is true, prefers host-visible device-local (Resizable BAR) memory first.
/// Otherwise prefers non-device-local system RAM (avoiding PCIe BAR VRAM on discrete GPUs),
/// falling back to device-local host-visible memory only on unified-memory GPUs where every
/// host-visible type is device-local (e.g. Intel iGPUs). Within each pass, cached host memory
/// is preferred over uncached (`allow_uncached`).
[[nodiscard]] inline std::uint32_t SelectGuestMemoryType(
    const vk::PhysicalDeviceMemoryProperties& props, std::uint32_t bits, bool uncached,
    bool in_vram, bool allow_uncached) {
    if (in_vram) {
        const auto want = vk::MemoryPropertyFlagBits::eHostVisible |
                          vk::MemoryPropertyFlagBits::eDeviceLocal;
        for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want) {
                return i;
            }
        }
    }
    for (const bool allow_device_local : {false, true}) {
        for (const bool cached : {true, false}) {
            if (!cached && !allow_uncached) {
                continue;
            }
            const auto want = vk::MemoryPropertyFlagBits::eHostVisible |
                              (cached ? vk::MemoryPropertyFlagBits::eHostCached
                                      : vk::MemoryPropertyFlags{});
            for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
                const auto flags = props.memoryTypes[i].propertyFlags;
                if ((bits & (1u << i)) && (flags & want) == want &&
                    (allow_device_local || !(flags & vk::MemoryPropertyFlagBits::eDeviceLocal)) &&
                    GuestMemoryTypeAllowed(flags, uncached)) {
                    return i;
                }
            }
        }
    }
    return ~0u;
}

} // namespace BbGuestMemory::detail
