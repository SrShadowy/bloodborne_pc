// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_perf_metrics.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "bbport_toggles.h"

namespace BbPerfMetrics {

namespace {

MetricsSnapshot s_cached_snapshot{};
std::chrono::steady_clock::time_point s_last_sample_time{};
uint64_t s_last_cpu_ns = 0;

float s_frame_history[kFrameHistoryCapacity]{};
size_t s_history_count = 0;
size_t s_history_offset = 0;

int s_gpu_fd = -1;
bool s_tried_gpu_open = false;

void SampleMetrics() {
    // 1. Process CPU Usage via CLOCK_PROCESS_CPUTIME_ID (fast VDSO, ~15 ns, in-process, 0 files)
    struct timespec ts{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) {
        const uint64_t cpu_ns = uint64_t(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
        const auto now = std::chrono::steady_clock::now();
        if (s_last_sample_time.time_since_epoch().count() != 0) {
            const uint64_t delta_wall_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(now - s_last_sample_time).count();
            const uint64_t delta_cpu_ns = (cpu_ns >= s_last_cpu_ns) ? (cpu_ns - s_last_cpu_ns) : 0;
            if (delta_wall_ns > 0) {
                static const long num_cores = std::max(1L, sysconf(_SC_NPROCESSORS_ONLN));
                const float raw_pct = (float(delta_cpu_ns) / float(delta_wall_ns)) * 100.0f;
                // Normalized process CPU % across available machine cores
                s_cached_snapshot.cpu_percent = std::clamp(raw_pct / float(num_cores), 0.0f, 100.0f);
            }
        }
        s_last_cpu_ns = cpu_ns;
        s_last_sample_time = now;
    }

    // 2. Process RAM (Resident Set Size) via getrusage (kernel process accounting, 0 files, no sudo)
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        // ru_maxrss is in KiB on Linux
        s_cached_snapshot.ram_rss_bytes = uint64_t(usage.ru_maxrss) * 1024ULL;
    }

    // 3. Process VRAM from internal Vulkan atomic memory trackers (0 ns, in-process memory)
    const uint64_t alloc = BbStats::device_alloc_bytes.load(std::memory_order_relaxed);
    const uint64_t freed = BbStats::device_free_bytes.load(std::memory_order_relaxed);
    s_cached_snapshot.vram_bytes = (alloc > freed) ? (alloc - freed) : 0;

    // 4. GPU Usage: probe all available DRM cards and track the active/dedicated GPU
    static int s_gpu_fds[4] = {-1, -1, -1, -1};
    static int s_vram_fds[4] = {-1, -1, -1, -1};
    static bool s_probed = false;
    if (!s_probed) {
        s_probed = true;
        for (int i = 0; i < 4; ++i) {
            char path[80];
            std::snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/gpu_busy_percent", i);
            s_gpu_fds[i] = open(path, O_RDONLY);
            std::snprintf(path, sizeof(path), "/sys/class/drm/card%d/device/mem_info_vram_used", i);
            s_vram_fds[i] = open(path, O_RDONLY);
        }
    }

    float max_busy = -1.0f;
    uint64_t vram_used_sysfs = 0;
    for (int i = 0; i < 4; ++i) {
        if (s_gpu_fds[i] >= 0) {
            char buf[16]{};
            const ssize_t n = pread(s_gpu_fds[i], buf, sizeof(buf) - 1, 0);
            if (n > 0) {
                buf[n] = '\0';
                int val = 0;
                if (std::sscanf(buf, "%d", &val) == 1) {
                    const float fval = float(std::clamp(val, 0, 100));
                    if (fval > max_busy) {
                        max_busy = fval;
                        if (s_vram_fds[i] >= 0) {
                            char vbuf[32]{};
                            const ssize_t vn = pread(s_vram_fds[i], vbuf, sizeof(vbuf) - 1, 0);
                            if (vn > 0) {
                                vbuf[vn] = '\0';
                                unsigned long long v = 0;
                                if (std::sscanf(vbuf, "%llu", &v) == 1 && v > 0) {
                                    vram_used_sysfs = v;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (max_busy >= 0.0f) {
        s_cached_snapshot.gpu_percent = max_busy;
        if (vram_used_sysfs > 0) {
            s_cached_snapshot.vram_bytes = vram_used_sysfs;
        }
    } else {
        // Fallback: estimate from Vulkan Presenter wait and thread state
        const uint64_t wait_ns = BbStats::present_wait_ns.load(std::memory_order_relaxed);
        if (wait_ns > 0) {
            s_cached_snapshot.gpu_percent = std::clamp(float(wait_ns / 1000000ULL), 0.0f, 100.0f);
        }
    }
}

} // namespace

void Update(float frame_time_ms) {
    // Record frametime in ring buffer (O(1), < 2 ns)
    s_frame_history[s_history_offset] = frame_time_ms;
    s_history_offset = (s_history_offset + 1) % kFrameHistoryCapacity;
    if (s_history_count < kFrameHistoryCapacity) {
        s_history_count++;
    }

    // Throttled background metrics sampling (every 500 ms)
    const auto now = std::chrono::steady_clock::now();
    if (s_last_sample_time.time_since_epoch().count() == 0 ||
        (now - s_last_sample_time) >= std::chrono::milliseconds(500)) {
        SampleMetrics();
    }
}

MetricsSnapshot GetSnapshot() {
    return s_cached_snapshot;
}

void GetFrameHistory(float const*& out_samples, size_t& out_count, size_t& out_offset) {
    out_samples = s_frame_history;
    out_count = s_history_count;
    out_offset = s_history_offset;
}

} // namespace BbPerfMetrics
