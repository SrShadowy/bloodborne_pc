// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Guest memory Vulkan type selection (gpu/shim/bbport_guest_memory_types.h).
// Regression test for Intel integrated GPUs (Windows & Linux UMA) where every host-visible
// memory type carries eDeviceLocal, while ensuring discrete GPUs with ReBAR still prefer
// non-device-local system RAM first.
#include "shim/bbport_guest_memory_types.h"

#include <cassert>
#include <cstdio>

namespace {

using BbGuestMemory::detail::SelectGuestMemoryType;
using F = vk::MemoryPropertyFlagBits;

vk::PhysicalDeviceMemoryProperties MakeProps(std::initializer_list<vk::MemoryPropertyFlags> list) {
    vk::PhysicalDeviceMemoryProperties props{};
    for (const auto flags : list) {
        props.memoryTypes[props.memoryTypeCount++].propertyFlags = flags;
    }
    return props;
}

void TestIntelUmaIgpus() {
    // Intel UHD / Iris Xe unified memory: all memory types belong to the single DEVICE_LOCAL heap.
    // Index 0: uncached/WC; index 1: host-cached.
    const auto intel_uma = MakeProps({
        F::eDeviceLocal | F::eHostVisible | F::eHostCoherent,
        F::eDeviceLocal | F::eHostVisible | F::eHostCoherent | F::eHostCached,
    });
    // Both FindType (allow_uncached=false) and FindHostType (allow_uncached=true) must pick cached
    // index 1 rather than failing with ~0u (which triggered ErrorFeatureNotPresent / exit code 23).
    assert(SelectGuestMemoryType(intel_uma, 0b11u, false, false, false) == 1);
    assert(SelectGuestMemoryType(intel_uma, 0b11u, false, false, true) == 1);

    // If the driver only offers an uncached device-local host-visible type for a pointer/buffer:
    // FindHostType (allow_uncached=true) still succeeds on index 0, while FindType (cached-only)
    // refuses it.
    assert(SelectGuestMemoryType(intel_uma, 0b01u, false, false, true) == 0);
    assert(SelectGuestMemoryType(intel_uma, 0b01u, false, false, false) == ~0u);
}

void TestDiscreteGpuWithRebar() {
    // Typical NVIDIA / AMD discrete GPU with Resizable BAR:
    // 0: VRAM (DEVICE_LOCAL)
    // 1: System RAM uncached (HOST_VISIBLE | HOST_COHERENT)
    // 2: System RAM cached (HOST_VISIBLE | HOST_COHERENT | HOST_CACHED)
    // 3: ReBAR BAR VRAM (DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT)
    const auto discrete = MakeProps({
        F::eDeviceLocal,
        F::eHostVisible | F::eHostCoherent,
        F::eHostVisible | F::eHostCoherent | F::eHostCached,
        F::eDeviceLocal | F::eHostVisible | F::eHostCoherent,
    });
    // Must pick cached system RAM (2), never ReBAR (3).
    assert(SelectGuestMemoryType(discrete, 0b1111u, false, false, false) == 2);
    assert(SelectGuestMemoryType(discrete, 0b1111u, false, false, true) == 2);

    // Even when cached system RAM is excluded, FindHostType must pick uncached system RAM (1)
    // in pass 1 before considering ReBAR (3).
    assert(SelectGuestMemoryType(discrete, 0b1011u, false, false, true) == 1);

    // Only when in_vram=true (BB_GUEST_VRAM) should ReBAR (3) be preferred.
    assert(SelectGuestMemoryType(discrete, 0b1111u, false, true, false) == 3);
}

void TestAmdApuAndUncachedFilter() {
    // AMD APU (Steam Deck) exposes non-device-local GTT alongside device-local types.
    const auto amd_apu = MakeProps({
        F::eDeviceLocal,
        F::eHostVisible | F::eHostCoherent | F::eHostCached,
        F::eDeviceLocal | F::eHostVisible | F::eHostCoherent,
        F::eHostVisible | F::eHostCoherent | F::eHostCached | F::eDeviceUncachedAMD,
    });
    assert(SelectGuestMemoryType(amd_apu, 0b1111u, false, false, true) == 1);
    assert(SelectGuestMemoryType(amd_apu, 0b1111u, true, false, true) == 3);
}

} // namespace

int main() {
    TestIntelUmaIgpus();
    TestDiscreteGpuWithRebar();
    TestAmdApuAndUncachedFilter();
    std::puts("guest-memory-types: Intel UMA, discrete ReBAR, and AMD APU selection OK");
    return 0;
}
