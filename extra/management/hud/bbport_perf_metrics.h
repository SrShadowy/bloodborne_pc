// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace BbPerfMetrics {

struct MetricsSnapshot {
    float cpu_percent = 0.0f;     // Process CPU usage % (0-100%, normalized across cores)
    float gpu_percent = 0.0f;     // GPU load %
    uint64_t ram_rss_bytes = 0;   // Process resident RAM in bytes
    uint64_t vram_bytes = 0;      // Process VRAM in bytes (game/project only)
    uint64_t vram_sys_bytes = 0;  // System-wide GPU VRAM in bytes (whole device)
};

constexpr size_t kFrameHistoryCapacity = 240;

// Updates ring buffer each frame, and throttles process metrics sampling to 500ms intervals.
void Update(float frame_time_ms);

// Returns latest cached metrics snapshot (0 ns overhead, pure memory read).
MetricsSnapshot GetSnapshot();

// Retrieves frametime history buffer for plotting.
void GetFrameHistory(float const*& out_samples, size_t& out_count, size_t& out_offset);

} // namespace BbPerfMetrics
