// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_shader_tracker.h"

namespace BbShaderTracker {

namespace {
std::atomic<uint32_t> g_active_compiles{0};
std::atomic<uint64_t> g_total_compiles{0};
std::atomic<int64_t> g_last_compile_end_ms{0};

// Decay grace window: keeps indicator visible for 800ms after last compile ends
// so rapid bursty compilations don't cause the indicator to flicker/strobe.
constexpr int64_t kDecayWindowMs = 800;
} // namespace

void OnCompileStart() {
    g_active_compiles.fetch_add(1, std::memory_order_relaxed);
    g_total_compiles.fetch_add(1, std::memory_order_relaxed);
}

void OnCompileEnd() {
    const uint32_t prev = g_active_compiles.fetch_sub(1, std::memory_order_acq_rel);
    if (prev <= 1) {
        const auto now = std::chrono::steady_clock::now();
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        g_last_compile_end_ms.store(now_ms, std::memory_order_release);
    }
}

bool IsCompiling() {
    if (g_active_compiles.load(std::memory_order_relaxed) > 0) {
        return true;
    }
    const int64_t last_end = g_last_compile_end_ms.load(std::memory_order_relaxed);
    if (last_end == 0) {
        return false;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return (now_ms >= last_end) && ((now_ms - last_end) < kDecayWindowMs);
}

uint32_t GetActiveCount() {
    return g_active_compiles.load(std::memory_order_relaxed);
}

uint64_t GetTotalCount() {
    return g_total_compiles.load(std::memory_order_relaxed);
}

uint32_t GetDecayElapsedMs() {
    if (g_active_compiles.load(std::memory_order_relaxed) > 0) {
        return 0;
    }
    const int64_t last_end = g_last_compile_end_ms.load(std::memory_order_relaxed);
    if (last_end == 0) {
        return 0xFFFF;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if (now_ms < last_end) {
        return 0;
    }
    return static_cast<uint32_t>(now_ms - last_end);
}

} // namespace BbShaderTracker
