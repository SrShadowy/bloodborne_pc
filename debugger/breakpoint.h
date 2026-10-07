// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <sys/types.h>

namespace Debugger {

struct WatchpointHit {
    uintptr_t rip{0};
    uintptr_t address{0};
    pid_t thread_id{0};
    uint64_t timestamp{0};
    std::string disassembly;
};

struct SoftwareBreakpoint {
    uintptr_t address{0};
    uint8_t original_byte{0};
    bool enabled{false};
    std::string label;
};

class BreakpointManager {
public:
    static BreakpointManager& Get();

    void Init();

    // Memory Write Watchpoint ("Find what writes to this address")
    bool SetWriteWatchpoint(uintptr_t address);
    void ClearWriteWatchpoint();
    bool HasActiveWatchpoint() const { return watchpoint_active.load(std::memory_order_relaxed); }
    uintptr_t GetWatchedAddress() const { return watched_address.load(std::memory_order_relaxed); }

    // Software Breakpoint (Code execution break)
    bool AddBreakpoint(uintptr_t address, const std::string& label = "");
    bool RemoveBreakpoint(uintptr_t address);
    std::vector<SoftwareBreakpoint> GetBreakpoints() const;

    // Hits history
    std::vector<WatchpointHit> GetHits() const;
    void ClearHits();

    // Game execution flow control
    bool IsPaused() const { return game_paused.load(std::memory_order_relaxed); }
    void SetPaused(bool paused) { game_paused.store(paused, std::memory_order_relaxed); }
    void StepFrame() { step_requested.store(true, std::memory_order_relaxed); }
    bool CheckAndClearStep() { return step_requested.exchange(false, std::memory_order_relaxed); }

    // Internal signal handlers callbacks
    void OnSignalSegv(uintptr_t fault_addr, uintptr_t rip, pid_t tid, void* ucontext);
    void OnSignalTrap(uintptr_t rip, pid_t tid, void* ucontext);

private:
    BreakpointManager() = default;

    mutable std::mutex mutex;
    std::atomic<bool> initialized{false};

    // Watchpoint state
    std::atomic<bool> watchpoint_active{false};
    std::atomic<uintptr_t> watched_address{0};
    uintptr_t watched_page_start{0};
    size_t watched_page_size{4096};
    std::atomic<bool> waiting_single_step{false};

    // Breakpoint state
    std::vector<SoftwareBreakpoint> breakpoints;
    std::vector<WatchpointHit> hits;

    // Execution control
    std::atomic<bool> game_paused{false};
    std::atomic<bool> step_requested{false};
};

} // namespace Debugger
