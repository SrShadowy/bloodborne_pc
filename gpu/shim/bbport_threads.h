// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: helper thread sizing. Counts follow the hardware threads this process may run on
// (the affinity mask, so `taskset` can emulate a Steam Deck), and speculative helpers run as
// SCHED_IDLE: they use cores the game leaves idle and never take time from its threads.

#pragma once

#include <algorithm>
#include "shim/bb_env.h"
#include <cstdlib>
#include <thread>
#ifndef _WIN32
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace BbThreads {

/// Hardware threads available to the process.
inline unsigned Available() {
#ifdef _WIN32
    return bb_win32_available_cpus();
#else
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        return std::max(1, CPU_COUNT(&set));
    }
    return std::max(1u, std::thread::hardware_concurrency());
#endif
}

/// The calling thread only runs on otherwise idle cores (falls back to the lowest nice level).
inline void MakeBackground() {
#ifdef _WIN32
    bb_win32_background_thread();
#else
    sched_param param{};
    if (sched_setscheduler(0, SCHED_IDLE, &param) != 0) {
        setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), 19);
    }
#endif
}

/// The draw pipeline's critical threads (GPU command thread, draw recording, Vulkan recording):
/// ahead of the game's own threads for the CPU. All threads ran at one priority (the guest's are
/// not mapped), so on 4 cores the stages took turns with a dozen game threads and some frames
/// recorded at half speed. BB_STAGE_PRIORITY=0 off (default), 1 above normal, 2 highest
/// (Windows; on Linux a lower nice value where the process may set one). GTX 1050 laptop, 4 cores,
/// 1080p: 31.6-31.9 -> 32.5-32.8 FPS, frames over 50 ms 16-25 -> 10-15 a minute; 2 was worse.
/// Off by default: with it the game sent command buffers it had not finished writing (a packet
/// cut short at load, "Bad PM4 packet", 2 of 10 runs vs 0 of ~25): its threads, preempted by
/// ours, race in ways the console's strict priorities never let happen (2026-10-25).
inline void MakeCritical() {
    static const int level = [] {
        const char* env = bb_env("BB_STAGE_PRIORITY");
        return env ? std::atoi(env) : 0;
    }();
    if (level <= 0) {
        return;
    }
#ifdef _WIN32
    bb_win32_critical_thread(level);
#else
    setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), level >= 2 ? -10 : -5);
#endif
}

/// Helper threads of the GPU side that the GPU command thread's progress depends on (Vulkan
/// recording threads, guest memory copy threads): a fault there must never wait for that thread
/// (a read trap's copy back goes through it for the game's threads). Set once per thread.
inline thread_local bool gpu_side_helper = false;

} // namespace BbThreads
