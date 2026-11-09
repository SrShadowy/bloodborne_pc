// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the debugger's breakpoints. A write watchpoint makes the watched page read-only (so the
// first write traps) and single-steps the writing thread so the page can be re-protected; a
// software breakpoint writes INT3 over the instruction and restores it when hit. The signal
// handlers are breakpoint_signals.cpp and the hit processing breakpoint_worker.cpp.
#include "breakpoint.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <thread>

#include "breakpoint_internal.h"
#include "mem_scanner.h"

namespace Debugger {

namespace detail {

namespace {

/// Replaces the pages' protection, trying the executable form when the plain one fails (a code
/// page cannot be made read-only without keeping it executable).
bool Protect(uintptr_t page, std::size_t size, int plain, int executable) {
    if (page == 0) {
        return false;
    }
    if (mprotect(reinterpret_cast<void*>(page), size, plain) == 0) {
        return true;
    }
    return mprotect(reinterpret_cast<void*>(page), size, executable) == 0;
}

} // namespace

void InstallHandlers();

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

bool ProtectReadOnly(uintptr_t page, std::size_t size) {
    return Protect(page, size, PROT_READ, PROT_READ | PROT_EXEC);
}

bool ProtectWritable(uintptr_t page, std::size_t size) {
    return Protect(page, size, PROT_READ | PROT_WRITE, PROT_READ | PROT_WRITE | PROT_EXEC);
}

void ResetStepSlots(std::array<StepSlot, MAX_STEP_SLOTS>& slots) {
    for (auto& slot : slots) {
        ClearStepSlot(slot);
    }
}

void ClearStepSlot(StepSlot& slot) {
    slot.tid.store(0, std::memory_order_release);
    slot.page.store(0, std::memory_order_release);
    slot.arm_time_ms.store(0, std::memory_order_release);
}

bool AnyThreadStepping(const std::array<StepSlot, MAX_STEP_SLOTS>& slots) {
    return std::any_of(slots.begin(), slots.end(),
                       [](const StepSlot& slot) { return slot.tid.load(std::memory_order_relaxed) != 0; });
}

} // namespace detail

BreakpointManager& BreakpointManager::Get() {
    static BreakpointManager instance;
    return instance;
}

BreakpointManager::~BreakpointManager() {
    ClearWriteWatchpoint();
    StopWorker();
}

void BreakpointManager::Init() {
    if (initialized.exchange(true)) {
        return;
    }
    StartWorker();
    detail::InstallHandlers();
}

void BreakpointManager::StartWorker() {
    if (worker_running.exchange(true)) {
        return;
    }
    worker_thread = std::thread(&BreakpointManager::WorkerLoop, this);
}

void BreakpointManager::StopWorker() {
    if (!worker_running.exchange(false)) {
        return;
    }
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

void BreakpointManager::PushRawHit(const RawHitEvent& ev) {
    const std::size_t cur_w = ring_write_idx.load(std::memory_order_relaxed);
    const std::size_t next_w = (cur_w + 1) % RING_BUFFER_CAPACITY;
    if (next_w != ring_read_idx.load(std::memory_order_acquire)) {
        ring_buffer[cur_w] = ev;
        ring_write_idx.store(next_w, std::memory_order_release);
    }
}

bool BreakpointManager::SetWriteWatchpoint(uintptr_t address) {
    Init();
    std::lock_guard<std::mutex> lock(mutex);
    ClearWriteWatchpoint();
    if (!address) {
        return false;
    }

    watched_page_start = address & detail::kPageMask;
    watched_page_size = detail::kPageSize;
    watched_address.store(address, std::memory_order_release);
    has_last_watched_value.store(false, std::memory_order_release);
    value_change_count.store(0, std::memory_order_release);
    hits.clear();
    detail::ResetStepSlots(step_slots);
    waiting_single_step.store(false, std::memory_order_release);

    if (!detail::ProtectReadOnly(watched_page_start, watched_page_size)) {
        // Some regions are only reachable through the guest's 16 KiB mapping.
        const uintptr_t page16 = address & detail::kPageMask16K;
        if (!detail::ProtectReadOnly(page16, detail::kPageSize16K)) {
            std::fprintf(stderr,
                         "BreakpointManager::SetWriteWatchpoint failed for 0x%lx: %s (errno=%d)\n",
                         static_cast<unsigned long>(address), std::strerror(errno), errno);
            watched_address.store(0, std::memory_order_release);
            watched_page_start = 0;
            return false;
        }
        watched_page_start = page16;
        watched_page_size = detail::kPageSize16K;
    }

    watchpoint_active.store(true, std::memory_order_release);
    return true;
}

void BreakpointManager::ClearWriteWatchpoint() {
    if (!watchpoint_active.load(std::memory_order_relaxed)) {
        return;
    }
    detail::ProtectWritable(watched_page_start, watched_page_size);
    watchpoint_active.store(false, std::memory_order_release);
    watched_address.store(0, std::memory_order_release);
    has_last_watched_value.store(false, std::memory_order_release);
    watched_page_start = 0;
    waiting_single_step.store(false, std::memory_order_release);
    detail::ResetStepSlots(step_slots);
}

bool BreakpointManager::AddBreakpoint(uintptr_t address, const std::string& label) {
    Init();
    std::lock_guard<std::mutex> lock(mutex);
    std::uint8_t original = 0;
    if (!MemoryScanner::ReadMemory(address, &original, 1)) {
        return false;
    }
    constexpr std::uint8_t kInt3 = detail::kInt3Opcode;
    if (!MemoryScanner::WriteMemory(address, &kInt3, 1)) {
        return false;
    }
    breakpoints.push_back({address, original, true, label});
    return true;
}

bool BreakpointManager::RemoveBreakpoint(uintptr_t address) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto it = breakpoints.begin(); it != breakpoints.end(); ++it) {
        if (it->address == address) {
            MemoryScanner::WriteMemory(address, &it->original_byte, 1);
            breakpoints.erase(it);
            return true;
        }
    }
    return false;
}

bool BreakpointManager::HasBreakpoint(uintptr_t address) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& bp : breakpoints) {
        if (bp.address == address && bp.enabled) {
            return true;
        }
    }
    return false;
}

std::vector<SoftwareBreakpoint> BreakpointManager::GetBreakpoints() const {
    std::lock_guard<std::mutex> lock(mutex);
    return breakpoints;
}

std::vector<WatchpointHit> BreakpointManager::GetHits() const {
    std::lock_guard<std::mutex> lock(mutex);
    return hits;
}

void BreakpointManager::ClearHits() {
    std::lock_guard<std::mutex> lock(mutex);
    hits.clear();
}

CpuRegisters BreakpointManager::GetLastRegisters() const {
    std::lock_guard<std::mutex> lock(mutex);
    return last_registers;
}

uintptr_t BreakpointManager::GetLastHitRip() const {
    return last_hit_rip.load(std::memory_order_relaxed);
}

} // namespace Debugger
