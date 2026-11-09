// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the breakpoint files share. breakpoint.cpp is the public API and the watchpoint
// state, breakpoint_signals.cpp handles SIGSEGV/SIGTRAP, breakpoint_worker.cpp drains the hit ring
// buffer. Only this module needs this header.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <signal.h>
#include <ucontext.h>

#include "breakpoint.h"

namespace Debugger::detail {

// ---- Sizes and timings ----

/// The host's page size, and the guest's (the PS4 maps 16 KiB pages, which some regions need).
constexpr std::uintptr_t kPageMask = ~static_cast<std::uintptr_t>(4095);
constexpr std::uintptr_t kPageMask16K = ~static_cast<std::uintptr_t>(16383);
constexpr std::size_t kPageSize = 4096;
constexpr std::size_t kPageSize16K = 16384;
/// A thread that has been single-stepping longer than this is stuck: the watchdog releases it.
constexpr std::uint64_t kStepTimeoutMs = 250;
/// How many hits the history keeps before dropping the oldest.
constexpr std::size_t kMaxHits = 200;
/// How long the worker sleeps when the ring buffer had nothing.
constexpr auto kWorkerIdleSleep = std::chrono::milliseconds(10);

/// The x86 trap flag in RFLAGS: set to single-step, cleared when the step trap arrives.
constexpr std::uint64_t kTrapFlag = 0x100;
/// What a software breakpoint writes over the instruction's first byte.
constexpr std::uint8_t kInt3Opcode = 0xCC;

// ---- Shared state ----

/// The handlers we replaced, so an unrelated fault still reaches whoever had it.
extern struct sigaction g_old_segv_action;
extern struct sigaction g_old_trap_action;

// ---- Helpers ----

/// Replace this process's SIGSEGV/SIGTRAP handlers, remembering what they replaced.
void InstallHandlers();

/// Milliseconds since the steady clock's epoch.
std::uint64_t NowMs();

/// Turns a page read-only, falling back to read+execute for code pages. False when it failed.
bool ProtectReadOnly(std::uintptr_t page, std::size_t size);

/// Turns a page read+write, falling back to read+write+execute for code pages.
bool ProtectWritable(std::uintptr_t page, std::size_t size);

/// Forgets every thread's pending single-step.
void ResetStepSlots(std::array<StepSlot, MAX_STEP_SLOTS>& slots);

/// Whether any thread is still single-stepping.
bool AnyThreadStepping(const std::array<StepSlot, MAX_STEP_SLOTS>& slots);

/// Forgets one thread's pending single-step.
void ClearStepSlot(StepSlot& slot);

} // namespace Debugger::detail
