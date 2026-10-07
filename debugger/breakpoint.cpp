// SPDX-License-Identifier: GPL-2.0-or-later
#include "breakpoint.h"
#include "mem_scanner.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <ucontext.h>

namespace Debugger {

static struct sigaction old_segv_action;
static struct sigaction old_trap_action;

static void SegvHandler(int sig, siginfo_t* info, void* uctx) {
    auto& mgr = BreakpointManager::Get();
    const uintptr_t fault_addr = reinterpret_cast<uintptr_t>(info->si_addr);
    auto* ctx = static_cast<ucontext_t*>(uctx);
#if defined(__x86_64__)
    const uintptr_t rip = ctx->uc_mcontext.gregs[REG_RIP];
#else
    const uintptr_t rip = 0;
#endif
    const pid_t tid = gettid();

    if (mgr.HasActiveWatchpoint()) {
        const uintptr_t watched = mgr.GetWatchedAddress();
        constexpr uintptr_t PAGE_MASK = ~static_cast<uintptr_t>(4095);
        if ((fault_addr & PAGE_MASK) == (watched & PAGE_MASK)) {
            mgr.OnSignalSegv(fault_addr, rip, tid, uctx);
            return;
        }
    }

    if (old_segv_action.sa_flags & SA_SIGINFO) {
        if (old_segv_action.sa_sigaction) {
            old_segv_action.sa_sigaction(sig, info, uctx);
            return;
        }
    } else if (old_segv_action.sa_handler && old_segv_action.sa_handler != SIG_DFL &&
               old_segv_action.sa_handler != SIG_IGN) {
        old_segv_action.sa_handler(sig);
        return;
    }
    // Default action if unhandled
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
}

static void TrapHandler(int sig, siginfo_t* info, void* uctx) {
    auto& mgr = BreakpointManager::Get();
    auto* ctx = static_cast<ucontext_t*>(uctx);
#if defined(__x86_64__)
    const uintptr_t rip = ctx->uc_mcontext.gregs[REG_RIP];
#else
    const uintptr_t rip = 0;
#endif
    const pid_t tid = gettid();

    mgr.OnSignalTrap(rip, tid, uctx);

    if (old_trap_action.sa_flags & SA_SIGINFO) {
        if (old_trap_action.sa_sigaction) {
            old_trap_action.sa_sigaction(sig, info, uctx);
            return;
        }
    } else if (old_trap_action.sa_handler && old_trap_action.sa_handler != SIG_DFL &&
               old_trap_action.sa_handler != SIG_IGN) {
        old_trap_action.sa_handler(sig);
        return;
    }
}

BreakpointManager& BreakpointManager::Get() {
    static BreakpointManager instance;
    return instance;
}

void BreakpointManager::Init() {
    if (initialized.exchange(true)) {
        return;
    }

    struct sigaction sa{};
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sa.sa_sigaction = SegvHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &old_segv_action);

    struct sigaction sa_trap{};
    sa_trap.sa_flags = SA_SIGINFO | SA_NODEFER;
    sa_trap.sa_sigaction = TrapHandler;
    sigemptyset(&sa_trap.sa_mask);
    sigaction(SIGTRAP, &sa_trap, &old_trap_action);
}

bool BreakpointManager::SetWriteWatchpoint(uintptr_t address) {
    Init();
    std::lock_guard<std::mutex> lock(mutex);
    ClearWriteWatchpoint();

    if (!address) return false;

    constexpr uintptr_t PAGE_MASK = ~static_cast<uintptr_t>(4095);
    watched_page_start = address & PAGE_MASK;
    watched_page_size = 4096;
    watched_address.store(address);

    // Make page read-only so any write faults into SegvHandler
    if (mprotect(reinterpret_cast<void*>(watched_page_start), watched_page_size, PROT_READ) != 0) {
        watched_address.store(0);
        return false;
    }

    watchpoint_active.store(true);
    return true;
}

void BreakpointManager::ClearWriteWatchpoint() {
    if (!watchpoint_active.load()) return;
    if (watched_page_start) {
        mprotect(reinterpret_cast<void*>(watched_page_start), watched_page_size, PROT_READ | PROT_WRITE);
    }
    watchpoint_active.store(false);
    watched_address.store(0);
    watched_page_start = 0;
}

void BreakpointManager::OnSignalSegv(uintptr_t fault_addr, uintptr_t rip, pid_t tid, void* uctx) {
    auto* ctx = static_cast<ucontext_t*>(uctx);

    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();

    // Log the hit
    {
        std::lock_guard<std::mutex> lock(mutex);
        char disasm[64];
        std::snprintf(disasm, sizeof(disasm), "Written by rip: 0x%016llx", static_cast<unsigned long long>(rip));
        hits.push_back({rip, fault_addr, tid, static_cast<uint64_t>(now), disasm});
        if (hits.size() > 100) {
            hits.erase(hits.begin());
        }
    }

    // Unprotect so the instruction can complete
    mprotect(reinterpret_cast<void*>(watched_page_start), watched_page_size, PROT_READ | PROT_WRITE);

#if defined(__x86_64__)
    // Enable Trap Flag (single-step) so we catch control right after the instruction
    ctx->uc_mcontext.gregs[REG_EFL] |= 0x100;
#endif
    waiting_single_step.store(true);
}

void BreakpointManager::OnSignalTrap(uintptr_t rip, pid_t tid, void* uctx) {
    (void)rip;
    (void)tid;
    (void)uctx;
    if (waiting_single_step.exchange(false)) {
        // Single step completed: re-protect the page
        if (watchpoint_active.load() && watched_page_start) {
            mprotect(reinterpret_cast<void*>(watched_page_start), watched_page_size, PROT_READ);
        }
    }
}

bool BreakpointManager::AddBreakpoint(uintptr_t address, const std::string& label) {
    Init();
    std::lock_guard<std::mutex> lock(mutex);
    uint8_t orig = 0;
    if (!MemoryScanner::ReadMemory(address, &orig, 1)) {
        return false;
    }
    constexpr uint8_t INT3 = 0xCC;
    if (!MemoryScanner::WriteMemory(address, &INT3, 1)) {
        return false;
    }
    breakpoints.push_back({address, orig, true, label});
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

} // namespace Debugger
