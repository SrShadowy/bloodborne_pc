// SPDX-License-Identifier: GPL-2.0-or-later
// bbport CPU affinity and thread scheduling optimizations for host execution.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/resource.h>
#include <sys/syscall.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Emits an architecture-appropriate pause instruction in spin/yield loops
static inline void extra_cpu_yield_pause(void) {
#if defined(__x86_64__) || defined(_M_X64)
    __asm__ __volatile__("pause" ::: "memory");
#elif defined(__aarch64__)
    __asm__ __volatile__("isb" ::: "memory");
#endif
}

// Maps and applies PS4 guest affinity mask to host CPU cores
static inline void extra_cpu_apply_affinity(pthread_t host_thread, uint64_t mask, bool is_current) {
#ifndef _WIN32
    static int affinity_mode = -1;
    static int avail_cpus[CPU_SETSIZE];
    static int count = 0;
    static int initialized = 0;

    if (!initialized) {
        initialized = 1;
        const char *env = getenv("BB_CPU_AFFINITY");
        affinity_mode = (!env || (env[0] != '0')) ? 1 : 0;

        cpu_set_t proc_set;
        CPU_ZERO(&proc_set);
        if (sched_getaffinity(getpid(), sizeof(proc_set), &proc_set) == 0) {
            int total_avail = CPU_COUNT(&proc_set);
            if (total_avail >= 4) {
                for (int i = 0; i < CPU_SETSIZE && count < total_avail; ++i) {
                    if (CPU_ISSET(i, &proc_set)) {
                        avail_cpus[count++] = i;
                    }
                }
            }
        }
    }

    if (!affinity_mode || count < 4 || mask == 0) return;

    cpu_set_t target_set;
    CPU_ZERO(&target_set);
    int target_count = 0;

    if ((mask & 0x7f) == 0x7f) {
        // General guest thread (all 7 PS4 cores allowed):
        // Confine to primary cluster (up to 7 cores), keeping them on the same CCD / P-cores.
        int cluster_size = count < 7 ? count : 7;
        for (int i = 0; i < cluster_size; ++i) {
            CPU_SET(avail_cpus[i], &target_set);
            target_count++;
        }
    } else {
        // Explicit mask assigned by engine: map requested guest cores to physical host cores.
        for (int guest_core = 0; guest_core < 8; ++guest_core) {
            if (mask & (1ULL << guest_core)) {
                int host_cpu = avail_cpus[guest_core % count];
                CPU_SET(host_cpu, &target_set);
                target_count++;
            }
        }
    }

    if (target_count > 0) {
        pthread_t target = is_current ? pthread_self() : host_thread;
        pthread_setaffinity_np(target, sizeof(target_set), &target_set);
    }
#else
    (void)host_thread;
    (void)mask;
    (void)is_current;
#endif
}

// Maps PS4 thread priority and role to host Linux scheduling niceness (-20..19)
static inline void extra_cpu_apply_priority(pthread_t host_thread, int prio, const char *name, bool is_current) {
#ifndef _WIN32
    (void)host_thread;
    static int prio_mode = -1;
    if (prio_mode < 0) {
        const char *env = getenv("BB_CPU_PRIORITY");
        prio_mode = (!env || (env[0] != '0')) ? 1 : 0;
    }
    if (!prio_mode) return;

    int nice_val = 0;
    if (name) {
        if (strstr(name, "main") || strstr(name, "Render") || strstr(name, "GX") || strstr(name, "Gpu") || prio < 600) {
            nice_val = -5;
        } else if (strstr(name, "Sound") || strstr(name, "Audio")) {
            nice_val = -4;
        } else if (strstr(name, "save") || strstr(name, "content") || strstr(name, "posix") || prio > 720) {
            nice_val = 4;
        }
    } else if (prio < 600) {
        nice_val = -5;
    } else if (prio > 720) {
        nice_val = 4;
    }

    if (is_current) {
#ifdef SYS_gettid
        id_t tid = (id_t)syscall(SYS_gettid);
        setpriority(PRIO_PROCESS, tid, nice_val);
#endif
    }
#else
    (void)host_thread;
    (void)prio;
    (void)name;
    (void)is_current;
#endif
}

#ifdef __cplusplus
}
#endif
