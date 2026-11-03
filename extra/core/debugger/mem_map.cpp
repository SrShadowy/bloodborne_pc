// SPDX-License-Identifier: GPL-2.0-or-later
#include "mem_map.h"
#include <cstdio>
#include <algorithm>

namespace Debugger {

MemoryMap& MemoryMap::Get() {
    static MemoryMap instance;
    return instance;
}

MemoryMap::MemoryMap() {
    Refresh();
}

void MemoryMap::Refresh() {
    std::vector<MemorySegment> new_segments;
    FILE* f = std::fopen("/proc/self/maps", "re");
    if (!f) {
        return;
    }

    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        uintptr_t seg_start = 0, seg_end = 0;
        char r = 0, w = 0, x = 0;
        if (std::sscanf(line, "%lx-%lx %c%c%c", &seg_start, &seg_end, &r, &w, &x) >= 5) {
            uint32_t p = 0;
            if (r == 'r') p |= 1; // CpuRead
            if (w == 'w') p |= 2; // CpuWrite
            if (x == 'x') p |= 4; // CpuExec
            if (p != 0) {
                new_segments.push_back({seg_start, seg_end, p});
            }
        }
    }
    std::fclose(f);

    std::sort(new_segments.begin(), new_segments.end(),
              [](const MemorySegment& a, const MemorySegment& b) {
                  return a.start < b.start;
              });

    segments_ = std::move(new_segments);
    last_refresh_ = std::chrono::steady_clock::now();
}

bool MemoryMap::QueryProtection(uint64_t address, uintptr_t* start, uintptr_t* end, uint32_t* prot) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (segments_.empty()) {
        Refresh();
    }
    if (segments_.empty()) {
        return false;
    }
    auto it = std::upper_bound(segments_.begin(), segments_.end(), address,
        [](uint64_t addr, const MemorySegment& seg) {
            return addr < seg.start;
        });
    if (it != segments_.begin()) {
        --it;
        if (address >= it->start && address < it->end) {
            if (start) *start = it->start;
            if (end) *end = it->end;
            if (prot) *prot = it->prot;
            return true;
        }
    }
    return false;
}

const std::vector<MemorySegment>& MemoryMap::GetSegments() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return segments_;
}

} // namespace Debugger

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" __declspec(dllexport) bool extra_query_memory_protection(uint64_t address, void** start, void** end, uint32_t* prot) {
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi)) != sizeof(mbi)) {
        return false;
    }
    if (mbi.State != MEM_COMMIT) {
        return false;
    }
    uint32_t p = 0;
    if (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) p |= 1;
    if (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) p |= 2;
    if (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) p |= 4;
    if (p == 0) return false;

    if (start) *start = mbi.BaseAddress;
    if (end) *end = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize);
    if (prot) *prot = p;
    return true;
}
#else
extern "C" bool extra_query_memory_protection(uint64_t address, void** start, void** end, uint32_t* prot) {
    uintptr_t seg_start = 0, seg_end = 0;
    uint32_t p = 0;
    if (Debugger::MemoryMap::Get().QueryProtection(address, &seg_start, &seg_end, &p)) {
        if (start) *start = reinterpret_cast<void*>(seg_start);
        if (end) *end = reinterpret_cast<void*>(seg_end);
        if (prot) *prot = p;
        return true;
    }
    return false;
}
#endif
