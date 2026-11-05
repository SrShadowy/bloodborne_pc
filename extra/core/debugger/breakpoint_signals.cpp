// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the SIGSEGV/SIGTRAP handlers. A write to the watched page lands in SegvHandler, which
// records the hit and arms a single-step for that thread; the step then lands in TrapHandler,
// which re-protects the page. Both run in signal context, so they only touch atomics and the lock-
// free ring buffer -- decoding and the hit list belong to breakpoint_worker.cpp.
#include "breakpoint_internal.h"

#include <sys/mman.h>
#include <unistd.h>

#include "mem_scanner.h"

#ifndef TRAP_TRACE
#define TRAP_TRACE 2
#endif
#ifndef TRAP_HWBKPT
#define TRAP_HWBKPT 4
#endif
#ifndef SI_KERNEL
#define SI_KERNEL 128
#endif

namespace Debugger::detail {

struct sigaction g_old_segv_action;
struct sigaction g_old_trap_action;

} // namespace Debugger::detail

namespace Debugger {
namespace {

using detail::g_old_segv_action;
using detail::g_old_trap_action;

/// The instruction pointer of the faulting thread (0 off x86-64).
uintptr_t InstructionPointer(ucontext_t* ctx) {
#if defined(__x86_64__)
    return ctx ? ctx->uc_mcontext.gregs[REG_RIP] : 0;
#else
    (void)ctx;
    return 0;
#endif
}

/// Passes the fault to whoever handled it before us, or re-raises it with the default action.
void ForwardSegv(int sig, siginfo_t* info, void* uctx) {
    if (g_old_segv_action.sa_flags & SA_SIGINFO) {
        if (g_old_segv_action.sa_sigaction) {
            g_old_segv_action.sa_sigaction(sig, info, uctx);
            return;
        }
    } else if (g_old_segv_action.sa_handler && g_old_segv_action.sa_handler != SIG_DFL &&
               g_old_segv_action.sa_handler != SIG_IGN) {
        g_old_segv_action.sa_handler(sig);
        return;
    }
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
}

void ForwardTrap(int sig, siginfo_t* info, void* uctx) {
    if (g_old_trap_action.sa_flags & SA_SIGINFO) {
        if (g_old_trap_action.sa_sigaction) {
            g_old_trap_action.sa_sigaction(sig, info, uctx);
        }
    } else if (g_old_trap_action.sa_handler && g_old_trap_action.sa_handler != SIG_DFL &&
               g_old_trap_action.sa_handler != SIG_IGN) {
        g_old_trap_action.sa_handler(sig);
    }
}

void SegvHandler(int sig, siginfo_t* info, void* uctx) {
    auto& mgr = BreakpointManager::Get();
    const uintptr_t fault_addr = reinterpret_cast<uintptr_t>(info->si_addr);
    auto* ctx = static_cast<ucontext_t*>(uctx);
    const uintptr_t rip = InstructionPointer(ctx);
    const pid_t tid = gettid();

    if (mgr.HasActiveWatchpoint() &&
        (fault_addr & detail::kPageMask) == (mgr.GetWatchedAddress() & detail::kPageMask)) {
        mgr.OnSignalSegv(fault_addr, rip, tid, uctx);
        return;
    }
    ForwardSegv(sig, info, uctx);
}

void TrapHandler(int sig, siginfo_t* info, void* uctx) {
    auto& mgr = BreakpointManager::Get();
    auto* ctx = static_cast<ucontext_t*>(uctx);

    if (mgr.OnSignalTrap(InstructionPointer(ctx), gettid(), info, uctx)) {
        return; // Consumed our watchpoint single-step or breakpoint.
    }
    ForwardTrap(sig, info, uctx);
}

/// The faulting thread's registers, taken from the signal's ucontext.
CpuRegisters ReadRegisters(ucontext_t* ctx) {
    CpuRegisters regs;
#if defined(__x86_64__)
    regs.rax = ctx->uc_mcontext.gregs[REG_RAX];
    regs.rbx = ctx->uc_mcontext.gregs[REG_RBX];
    regs.rcx = ctx->uc_mcontext.gregs[REG_RCX];
    regs.rdx = ctx->uc_mcontext.gregs[REG_RDX];
    regs.rsi = ctx->uc_mcontext.gregs[REG_RSI];
    regs.rdi = ctx->uc_mcontext.gregs[REG_RDI];
    regs.rbp = ctx->uc_mcontext.gregs[REG_RBP];
    regs.rsp = ctx->uc_mcontext.gregs[REG_RSP];
    regs.r8 = ctx->uc_mcontext.gregs[REG_R8];
    regs.r9 = ctx->uc_mcontext.gregs[REG_R9];
    regs.r10 = ctx->uc_mcontext.gregs[REG_R10];
    regs.r11 = ctx->uc_mcontext.gregs[REG_R11];
    regs.r12 = ctx->uc_mcontext.gregs[REG_R12];
    regs.r13 = ctx->uc_mcontext.gregs[REG_R13];
    regs.r14 = ctx->uc_mcontext.gregs[REG_R14];
    regs.r15 = ctx->uc_mcontext.gregs[REG_R15];
    regs.rip = ctx->uc_mcontext.gregs[REG_RIP];
    regs.rflags = ctx->uc_mcontext.gregs[REG_EFL];
    regs.valid = true;
#else
    (void)ctx;
#endif
    return regs;
}

} // namespace

void BreakpointManager::OnSignalSegv(uintptr_t fault_addr, uintptr_t rip, pid_t tid, void* uctx) {
    auto* ctx = static_cast<ucontext_t*>(uctx);
    if (!ctx) {
        return;
    }
    const std::uint64_t now = detail::NowMs();

    RawHitEvent ev{};
    ev.rip = rip;
    ev.fault_addr = fault_addr;
    ev.thread_id = tid;
    ev.timestamp = now;
    ev.registers = ReadRegisters(ctx);
    PushRawHit(ev);

    if (auto_pause_on_hit.load(std::memory_order_relaxed)) {
        game_paused.store(true, std::memory_order_relaxed);
    }

    // Let the instruction that faulted complete.
    detail::ProtectWritable(watched_page_start, watched_page_size);

    // Arm this thread's single-step slot so the trap below can re-protect the page.
    for (auto& slot : step_slots) {
        pid_t expected = 0;
        if (slot.tid.load(std::memory_order_relaxed) == tid ||
            slot.tid.compare_exchange_strong(expected, tid, std::memory_order_acq_rel)) {
            slot.page.store(watched_page_start, std::memory_order_release);
            slot.arm_time_ms.store(now, std::memory_order_release);
            break;
        }
    }

#if defined(__x86_64__)
    // Enable the trap flag so the next instruction traps, straight after this one.
    ctx->uc_mcontext.gregs[REG_EFL] |= detail::kTrapFlag;
#endif
    waiting_single_step.store(true, std::memory_order_release);
}

bool BreakpointManager::OnSignalTrap(uintptr_t rip, pid_t tid, siginfo_t* info, void* uctx) {
    auto* ctx = static_cast<ucontext_t*>(uctx);
    if (!ctx) {
        return false;
    }

    // 1. A single-step (trace trap) from our own watchpoint?
    bool is_trace = false;
#if defined(__x86_64__)
    is_trace = (ctx->uc_mcontext.gregs[REG_EFL] & detail::kTrapFlag) != 0;
#endif
    if (info && (info->si_code == TRAP_TRACE || info->si_code == SI_KERNEL ||
                 info->si_code == TRAP_HWBKPT)) {
        is_trace = true;
    }

    bool tid_in_slots = false;
    for (auto& slot : step_slots) {
        if (slot.tid.load(std::memory_order_relaxed) == tid) {
            tid_in_slots = true;
            detail::ClearStepSlot(slot);
            break;
        }
    }

    if (tid_in_slots || is_trace || waiting_single_step.load(std::memory_order_relaxed)) {
#if defined(__x86_64__)
        // Clear the trap flag unconditionally, so later instructions do not keep trapping.
        ctx->uc_mcontext.gregs[REG_EFL] &= ~static_cast<greg_t>(detail::kTrapFlag);
#endif
        if (!detail::AnyThreadStepping(step_slots)) {
            waiting_single_step.store(false, std::memory_order_release);
            if (watchpoint_active.load(std::memory_order_relaxed) && watched_page_start) {
                detail::ProtectReadOnly(watched_page_start, watched_page_size);
            }
        }
        return true; // Consumed: never forward our single-step traps to the guest's hooks.
    }

    // 2. A software breakpoint (the INT3 we wrote): restore the byte, rewind RIP, record the hit.
    const uintptr_t bp_addr = rip ? (rip - 1) : 0;
    if (bp_addr && HasBreakpoint(bp_addr)) {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& bp_entry : breakpoints) {
            if (bp_entry.address != bp_addr || !bp_entry.enabled) {
                continue;
            }
            MemoryScanner::WriteMemory(bp_addr, &bp_entry.original_byte, 1);
#if defined(__x86_64__)
            // INT3 leaves RIP one past the instruction.
            ctx->uc_mcontext.gregs[REG_RIP] = bp_addr;
#endif
            RawHitEvent ev{};
            ev.rip = bp_addr;
            ev.fault_addr = bp_addr;
            ev.thread_id = tid;
            ev.timestamp = detail::NowMs();
            ev.registers = ReadRegisters(ctx);
            PushRawHit(ev);
            return true;
        }
    }
    return false;
}

namespace detail {

/// Installs the two handlers, remembering what they replaced (BreakpointManager::Init calls this).
void InstallHandlers() {
    struct sigaction segv {};
    segv.sa_flags = SA_SIGINFO | SA_NODEFER;
    segv.sa_sigaction = SegvHandler;
    sigemptyset(&segv.sa_mask);
    sigaction(SIGSEGV, &segv, &g_old_segv_action);

    struct sigaction trap {};
    trap.sa_flags = SA_SIGINFO | SA_NODEFER;
    trap.sa_sigaction = TrapHandler;
    sigemptyset(&trap.sa_mask);
    sigaction(SIGTRAP, &trap, &g_old_trap_action);
}

} // namespace detail

} // namespace Debugger
