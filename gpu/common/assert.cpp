// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: GPU-side check failures stop the port with exit code 23.
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include "common/assert.h"
#include "common/logging/log.h"
#include <execinfo.h>
#include <unistd.h>

// bb-probe (probe.c): set while the port restarts itself through run.sh. The device fd is closed
// before exec; Vulkan calls failing then are not errors: this thread waits for the exec instead.
extern "C" __attribute__((weak)) volatile int runtime_restarting; // absent in the tests

bool g_bb_assert_throws = false;

// [extra] W0.0 (NextStep plan): BB_SHADER_ASSERT_LOG=1 makes a failed check throw like the
// import path's (g_bb_assert_throws); the pipeline code catches, logs "[AssertLog]" with the
// pipeline/shader hash and skips the draw, so ONE run lists every shader the translator rejects
// instead of dying on the first (exit 23, then crash 139). Off by default: nothing changes
// without it.
bool BbAssertLogEnabled() {
    static const bool on = [] {
        const char* env = std::getenv("BB_SHADER_ASSERT_LOG");
        return env && env[0] == '1';
    }();
    return on;
}

void assert_fail_impl() {
    if (g_bb_assert_throws || BbAssertLogEnabled()) {
        throw std::runtime_error("assertion failed");
    }
    if (&runtime_restarting && runtime_restarting) {
        for (;;) {
            pause();
        }
    }
    std::fflush(stdout);
    // The calls that led here (module+offset: addr2line -e out/gpu/libbbgpu.so).
    void* frames[48];
    const int depth = backtrace(frames, 48);
    std::fputs("Assertion call stack:\n", stderr);
    backtrace_symbols_fd(frames, depth, STDERR_FILENO);
    std::fputs("STOP: GPU library assertion failed (see GPU log above)\n", stderr);
    std::fflush(stderr); // [extra]
    std::_Exit(23);
}

[[noreturn]] void unreachable_impl() {
    assert_fail_impl();
    throw std::runtime_error("Unreachable code");
}

void assert_fail_debug_msg(const char* msg) {
    LOG_CRITICAL(Debug, "Assertion Failed!\n{}", msg);
    assert_fail_impl();
}
