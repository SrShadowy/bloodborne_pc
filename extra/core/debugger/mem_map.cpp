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

    auto find_in_segments = [this, address](uintptr_t* out_start, uintptr_t* out_end, uint32_t* out_prot) -> bool {
        if (segments_.empty()) return false;
        auto it = std::upper_bound(segments_.begin(), segments_.end(), address,
            [](uint64_t addr, const MemorySegment& seg) {
                return addr < seg.start;
            });
        if (it != segments_.begin()) {
            --it;
            if (address >= it->start && address < it->end) {
                if (out_start) *out_start = it->start;
                if (out_end) *out_end = it->end;
                if (out_prot) *out_prot = it->prot;
                return true;
            }
        }
        return false;
    };

    if (find_in_segments(start, end, prot)) {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_refresh_ >= std::chrono::seconds(1)) {
        Refresh();
        return find_in_segments(start, end, prot);
    }

    return false;
}

const std::vector<MemorySegment>& MemoryMap::GetSegments() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return segments_;
}

} // namespace Debugger

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
