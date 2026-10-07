// SPDX-License-Identifier: GPL-2.0-or-later
#include "breakpoint.h"
#include "mem_scanner.h"
#include "common/decoder.h"

#include <cstdio>
#include <cstring>
#include <chrono>
#include <algorithm>
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

    if (mgr.OnSignalTrap(rip, tid, uctx)) {
        return; // Consumed our watchpoint single-step! Do not forward to guest hooks!
    }

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

BreakpointManager::~BreakpointManager() {
    ClearWriteWatchpoint();
    StopWorker();
}

void BreakpointManager::Init() {
    if (initialized.exchange(true)) {
        return;
    }

    StartWorker();

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
    const size_t cur_w = ring_write_idx.load(std::memory_order_relaxed);
    const size_t next_w = (cur_w + 1) % RING_BUFFER_CAPACITY;
    if (next_w != ring_read_idx.load(std::memory_order_acquire)) {
        ring_buffer[cur_w] = ev;
        ring_write_idx.store(next_w, std::memory_order_release);
    }
}

void BreakpointManager::WorkerLoop() {
    while (worker_running.load(std::memory_order_relaxed)) {
        bool had_work = false;

        while (ring_read_idx.load(std::memory_order_relaxed) != ring_write_idx.load(std::memory_order_acquire)) {
            had_work = true;
            const size_t cur_r = ring_read_idx.load(std::memory_order_relaxed);
            RawHitEvent ev = ring_buffer[cur_r];
            ring_read_idx.store((cur_r + 1) % RING_BUFFER_CAPACITY, std::memory_order_release);

            {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = std::find_if(hits.begin(), hits.end(), [&](const WatchpointHit& h) {
                    return h.rip == ev.rip;
                });

                if (it != hits.end()) {
                    it->count++;
                    it->timestamp = ev.timestamp;
                    it->address = ev.fault_addr;
                    it->thread_id = ev.thread_id;
                    if (ev.registers.valid) {
                        it->registers = ev.registers;
                    }
                } else {
                    // Disassemble the instruction safely in the worker thread (outside signal handler)
                    std::string disasm_str;
                    uint8_t code_buf[16];
                    if (ev.rip && MemoryScanner::ReadMemory(ev.rip, code_buf, sizeof(code_buf))) {
                        ZydisDecodedInstruction inst;
                        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
                        if (ZYAN_SUCCESS(Common::Decoder::Instance()->decodeInstruction(inst, operands, code_buf, sizeof(code_buf)))) {
                            disasm_str = Common::Decoder::Instance()->disassembleInst(inst, operands, ev.rip);
                        }
                    }
                    if (disasm_str.empty()) {
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "RIP: 0x%016llx", static_cast<unsigned long long>(ev.rip));
                        disasm_str = buf;
                    }

                    hits.push_back({ev.rip, ev.fault_addr, ev.thread_id, ev.timestamp, disasm_str, ev.registers, 1});
                    if (hits.size() > 200) {
                        hits.erase(hits.begin());
                    }
                }
                last_registers = ev.registers;
                last_hit_rip.store(ev.rip, std::memory_order_relaxed);
            }
        }

        // Live value tracker & watchpoint protection watchdog in worker thread
        const uintptr_t target_addr = watched_address.load(std::memory_order_relaxed);
        if (target_addr != 0) {
            uint64_t cur_val = 0;
            if (MemoryScanner::ReadMemory(target_addr, &cur_val, sizeof(cur_val))) {
                if (has_last_watched_value.load(std::memory_order_relaxed)) {
                    if (cur_val != last_watched_value.load(std::memory_order_relaxed)) {
                        value_change_count.fetch_add(1, std::memory_order_relaxed);
                        last_watched_value.store(cur_val, std::memory_order_relaxed);
                    }
                } else {
                    last_watched_value.store(cur_val, std::memory_order_relaxed);
                    has_last_watched_value.store(true, std::memory_order_relaxed);
                }
            }

            // Watchdog: If watchpoint active and no single step pending, ensure page is read-only
            if (watchpoint_active.load(std::memory_order_relaxed) && watched_page_start && !waiting_single_step.load(std::memory_order_relaxed)) {
                mprotect(reinterpret_cast<void*>(watched_page_start), watched_page_size, PROT_READ);
            }
        }

        if (!had_work) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

bool BreakpointManager::SetWriteWatchpoint(uintptr_t address) {
    Init();
    std::lock_guard<std::mutex> lock(mutex);
    ClearWriteWatchpoint();

    if (!address) return false;

    constexpr uintptr_t PAGE_MASK = ~static_cast<uintptr_t>(4095);
    watched_page_start = address & PAGE_MASK;
    watched_page_size = 4096;
    watched_address.store(address, std::memory_order_release);
    has_last_watched_value.store(false, std::memory_order_release);
    value_change_count.store(0, std::memory_order_release);
    hits.clear();

    // Make page read-only so any write faults into SegvHandler
    if (mprotect(reinterpret_cast<void*>(watched_page_start), watched_page_size, PROT_READ) != 0) {
        watched_address.store(0, std::memory_order_release);
        return false;
    }

    watchpoint_active.store(true, std::memory_order_release);
    return true;
}

void BreakpointManager::ClearWriteWatchpoint() {
    if (!watchpoint_active.load(std::memory_order_relaxed)) return;
    if (watched_page_start) {
        mprotect(reinterpret_cast<void*>(watched_page_start), watched_page_size, PROT_READ | PROT_WRITE);
    }
    watchpoint_active.store(false, std::memory_order_release);
    watched_address.store(0, std::memory_order_release);
    has_last_watched_value.store(false, std::memory_order_release);
    watched_page_start = 0;
}

void BreakpointManager::OnSignalSegv(uintptr_t fault_addr, uintptr_t rip, pid_t tid, void* uctx) {
    auto* ctx = static_cast<ucontext_t*>(uctx);

    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();

    CpuRegisters regs{};
#if defined(__x86_64__)
    regs.rax = ctx->uc_mcontext.gregs[REG_RAX];
    regs.rbx = ctx->uc_mcontext.gregs[REG_RBX];
    regs.rcx = ctx->uc_mcontext.gregs[REG_RCX];
    regs.rdx = ctx->uc_mcontext.gregs[REG_RDX];
    regs.rsi = ctx->uc_mcontext.gregs[REG_RSI];
    regs.rdi = ctx->uc_mcontext.gregs[REG_RDI];
    regs.rbp = ctx->uc_mcontext.gregs[REG_RBP];
    regs.rsp = ctx->uc_mcontext.gregs[REG_RSP];
    regs.r8  = ctx->uc_mcontext.gregs[REG_R8];
    regs.r9  = ctx->uc_mcontext.gregs[REG_R9];
    regs.r10 = ctx->uc_mcontext.gregs[REG_R10];
    regs.r11 = ctx->uc_mcontext.gregs[REG_R11];
    regs.r12 = ctx->uc_mcontext.gregs[REG_R12];
    regs.r13 = ctx->uc_mcontext.gregs[REG_R13];
    regs.r14 = ctx->uc_mcontext.gregs[REG_R14];
    regs.r15 = ctx->uc_mcontext.gregs[REG_R15];
    regs.rip = ctx->uc_mcontext.gregs[REG_RIP];
    regs.rflags = ctx->uc_mcontext.gregs[REG_EFL];
    regs.valid = true;
#endif

    // Record the hit to lock-free ring buffer
    RawHitEvent ev{};
    ev.rip = rip;
    ev.fault_addr = fault_addr;
    ev.thread_id = tid;
    ev.timestamp = static_cast<uint64_t>(now);
    ev.registers = regs;
    PushRawHit(ev);

    if (auto_pause_on_hit.load(std::memory_order_relaxed)) {
        game_paused.store(true, std::memory_order_relaxed);
    }

    // Unprotect so the instruction can complete
    if (watched_page_start) {
        mprotect(reinterpret_cast<void*>(watched_page_start), watched_page_size, PROT_READ | PROT_WRITE);
    }

#if defined(__x86_64__)
    // Enable Trap Flag (single-step) so we catch control right after the instruction
    ctx->uc_mcontext.gregs[REG_EFL] |= 0x100;
#endif
    waiting_single_step.store(true, std::memory_order_release);
}

bool BreakpointManager::OnSignalTrap(uintptr_t rip, pid_t tid, void* uctx) {
    (void)rip;
    (void)tid;
    auto* ctx = static_cast<ucontext_t*>(uctx);
    if (waiting_single_step.exchange(false, std::memory_order_acq_rel)) {
#if defined(__x86_64__)
        // Clear Trap Flag so subsequent instructions do not trap!
        ctx->uc_mcontext.gregs[REG_EFL] &= ~0x100;
#endif
        // Single step completed: re-protect the page
        if (watchpoint_active.load(std::memory_order_relaxed) && watched_page_start) {
            mprotect(reinterpret_cast<void*>(watched_page_start), watched_page_size, PROT_READ);
        }
        return true;
    }
    return false;
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

bool BreakpointManager::HasBreakpoint(uintptr_t address) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& bp : breakpoints) {
        if (bp.address == address && bp.enabled) return true;
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
