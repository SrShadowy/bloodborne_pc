// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the breakpoint worker -- a background thread that drains the signal handler's ring
// buffer, disassembles each new hit outside signal context, keeps the live value of the watched
// address, and acts as the watchdog for threads stuck mid-single-step.
#include "breakpoint_internal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

#include "common/decoder.h"
#include "mem_scanner.h"

namespace Debugger {
namespace {

using detail::NowMs;

/// "RIP: 0x0000000000123456", for a hit the decoder could not read.
std::string FallbackText(uintptr_t rip) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "RIP: 0x%016llx", static_cast<unsigned long long>(rip));
    return buf;
}

/// The instruction at `rip` and its operands, or empty when the bytes cannot be read/decoded.
std::string Disassemble(uintptr_t rip) {
    if (!rip) {
        return {};
    }
    std::uint8_t code[16];
    if (!MemoryScanner::ReadMemory(rip, code, sizeof(code))) {
        return {};
    }
    auto* decoder = Common::Decoder::Instance();
    ZydisDecodedInstruction inst;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(decoder->decodeInstruction(inst, operands, code, sizeof(code)))) {
        return {};
    }
    return decoder->disassembleInst(inst, operands, rip);
}

} // namespace

void BreakpointManager::WorkerLoop() {
    while (worker_running.load(std::memory_order_relaxed)) {
        bool had_work = false;

        while (ring_read_idx.load(std::memory_order_relaxed) !=
               ring_write_idx.load(std::memory_order_acquire)) {
            had_work = true;
            const std::size_t cur_r = ring_read_idx.load(std::memory_order_relaxed);
            const RawHitEvent ev = ring_buffer[cur_r];
            ring_read_idx.store((cur_r + 1) % RING_BUFFER_CAPACITY, std::memory_order_release);

            std::lock_guard<std::mutex> lock(mutex);
            auto it = std::find_if(hits.begin(), hits.end(),
                                   [&](const WatchpointHit& h) { return h.rip == ev.rip; });
            if (it != hits.end()) {
                ++it->count;
                it->timestamp = ev.timestamp;
                it->address = ev.fault_addr;
                it->thread_id = ev.thread_id;
                if (ev.registers.valid) {
                    it->registers = ev.registers;
                }
            } else {
                std::string disasm = Disassemble(ev.rip);
                if (disasm.empty()) {
                    disasm = FallbackText(ev.rip);
                }
                hits.push_back({ev.rip, ev.fault_addr, ev.thread_id, ev.timestamp, disasm,
                                ev.registers, 1});
                if (hits.size() > detail::kMaxHits) {
                    hits.erase(hits.begin());
                }
            }
            last_registers = ev.registers;
            last_hit_rip.store(ev.rip, std::memory_order_relaxed);
        }

        const uintptr_t target_addr = watched_address.load(std::memory_order_relaxed);
        if (target_addr != 0) {
            TrackWatchedValue(target_addr);
            CheckStuckThreads();
        }

        if (!had_work) {
            std::this_thread::sleep_for(detail::kWorkerIdleSleep);
        }
    }
}

void BreakpointManager::TrackWatchedValue(uintptr_t address) {
    std::uint64_t current = 0;
    if (!MemoryScanner::ReadMemory(address, &current, sizeof(current))) {
        return;
    }
    if (!has_last_watched_value.load(std::memory_order_relaxed)) {
        last_watched_value.store(current, std::memory_order_relaxed);
        has_last_watched_value.store(true, std::memory_order_relaxed);
        return;
    }
    if (current != last_watched_value.load(std::memory_order_relaxed)) {
        value_change_count.fetch_add(1, std::memory_order_relaxed);
        last_watched_value.store(current, std::memory_order_relaxed);
    }
}

/// A thread whose single-step never arrived (a fault elsewhere, a signal swallowed) would leave
/// the page writable forever; after the timeout its slot is dropped and the page re-protected.
void BreakpointManager::CheckStuckThreads() {
    const std::uint64_t now = NowMs();
    bool any_timed_out = false;
    for (auto& slot : step_slots) {
        if (slot.tid.load(std::memory_order_relaxed) == 0) {
            continue;
        }
        const std::uint64_t armed = slot.arm_time_ms.load(std::memory_order_relaxed);
        if (armed != 0 && now - armed > detail::kStepTimeoutMs) {
            detail::ClearStepSlot(slot);
            any_timed_out = true;
        }
    }
    if (!any_timed_out || detail::AnyThreadStepping(step_slots)) {
        return;
    }
    waiting_single_step.store(false, std::memory_order_release);
    detail::ProtectReadOnly(watched_page_start, watched_page_size);
}

} // namespace Debugger
