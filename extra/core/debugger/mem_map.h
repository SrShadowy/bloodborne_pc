// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include <chrono>

namespace Debugger {

struct MemorySegment {
    uintptr_t start = 0;
    uintptr_t end = 0;
    uint32_t prot = 0; // Bitmask: 1 = Read, 2 = Write, 4 = Exec
};

class MemoryMap {
public:
    static MemoryMap& Get();

    /// Queries protection and range for an address within Linux process maps.
    /// Returns true if address belongs to a mapped, valid segment.
    bool QueryProtection(uint64_t address, uintptr_t* start, uintptr_t* end, uint32_t* prot);

    /// Refreshes segments from /proc/self/maps.
    void Refresh();

    const std::vector<MemorySegment>& GetSegments() const;

private:
    MemoryMap();
    mutable std::mutex mutex_;
    std::vector<MemorySegment> segments_;
    std::chrono::steady_clock::time_point last_refresh_{};
};

} // namespace Debugger

extern "C" bool extra_query_memory_protection(uint64_t address, void** start, void** end, uint32_t* prot);
