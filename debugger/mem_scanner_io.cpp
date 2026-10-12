// SPDX-License-Identifier: GPL-2.0-or-later
#include "mem_scanner.h"
#include "gpu/bbgpu.h"

#include <cstdio>
#include <cstring>
#include <csetjmp>
#include <csignal>

extern "C" __thread sigjmp_buf* runtime_fault_recover;
extern "C" int runtime_memory_write_backing(uintptr_t address, const void* data, uint64_t size);

namespace Debugger {

bool MemoryScanner::ReadMemory(uintptr_t addr, void* dest, size_t size) {
    if (!addr || !dest || size == 0) return false;
    sigjmp_buf recover;
    if (sigsetjmp(recover, 1) != 0) {
        runtime_fault_recover = nullptr;
        return false;
    }
    runtime_fault_recover = &recover;
    std::memcpy(dest, reinterpret_cast<const void*>(addr), size);
    runtime_fault_recover = nullptr;
    return true;
}

bool MemoryScanner::WriteMemory(uintptr_t addr, const void* src, size_t size) {
    if (!addr || !src || size == 0) return false;
    if (runtime_memory_write_backing(addr, src, size)) {
        return true;
    }
    sigjmp_buf recover;
    if (sigsetjmp(recover, 1) != 0) {
        runtime_fault_recover = nullptr;
        return false;
    }
    runtime_fault_recover = &recover;
    std::memcpy(reinterpret_cast<void*>(addr), src, size);
    runtime_fault_recover = nullptr;
    return true;
}

std::vector<MemoryRegion> MemoryScanner::QueryRegions(ScanScope scope) {
    std::vector<MemoryRegion> regions;
    FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) return regions;

    const uintptr_t img_base = bbgpu_get_guest_image_base();
    const uint64_t img_size = bbgpu_get_guest_image_size();
    const uintptr_t img_end = (img_base && img_size) ? (img_base + img_size) : (img_base + 0x20000000ULL);

    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        uintptr_t start = 0, end = 0;
        char perms[5] = {0};
        if (std::sscanf(line, "%lx-%lx %4s", &start, &end, perms) == 3) {
            if (perms[0] != 'r') continue;
            if (scope == ScanScope::ExecutableOnly) {
                if (img_base != 0) {
                    if (start >= img_base && end <= img_end) {
                        regions.push_back({start, end});
                    }
                } else if (start >= 0x400000 && end <= 0x20000000) {
                    regions.push_back({start, end});
                }
            } else if (scope == ScanScope::GuestHeap) {
                const uintptr_t min_addr = img_base ? img_base : 0x400000;
                if (start >= min_addr && end <= 0x100000000000ULL) {
                    regions.push_back({start, end});
                }
            } else {
                regions.push_back({start, end});
            }
        }
    }
    std::fclose(f);
    return regions;
}

} // namespace Debugger
