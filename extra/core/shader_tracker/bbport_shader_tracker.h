// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace BbShaderTracker {

// Called when a shader or pipeline compilation begins.
void OnCompileStart();

// Called when a shader or pipeline compilation ends.
void OnCompileEnd();

// Check if shaders/pipelines are currently compiling or within the decay grace window (800ms)
// to prevent visual flicker during bursty compilations.
bool IsCompiling();

// Returns how many compilation jobs are currently running across all threads.
uint32_t GetActiveCount();

// Returns total number of compilations performed since launch.
uint64_t GetTotalCount();

// Returns time elapsed (in milliseconds) since compilation finished, or 0 if currently active.
uint32_t GetDecayElapsedMs();

// RAII helper to track compilation scope
struct ScopedCompile {
    ScopedCompile() { OnCompileStart(); }
    ~ScopedCompile() { OnCompileEnd(); }
};

} // namespace BbShaderTracker
