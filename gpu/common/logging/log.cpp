// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: see log.h.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>

#include <fmt/format.h>

#include "common/logging/log.h"
#include "shim/bb_env.h"

namespace Common::Log {
namespace {

constexpr const char* LevelNames[] = {"trace", "debug", "info", "warning", "error", "critical"};

// [extra] A line used to be one fprintf + one fflush: with BB_GPU_LOG=debug (the launcher's
// verbose mode) that was two syscalls per line on the logs' hot paths. Lines are collected here
// and handed to stderr in one write per batch: warnings and worse at once, the rest at 64 lines,
// 64 KiB, or when 250 ms passed since the last flush (checked as a line arrives: a burst batches,
// and the first line after a lull goes out with it), so a crash loses at most that much. stderr
// itself keeps the buffering the process configured (probe.c makes it unbuffered; a second setvbuf
// after the first write is undefined), which is also why the batching lives here and not in the
// stream.
constexpr unsigned kFlushLines = 64;
constexpr auto kFlushInterval = std::chrono::milliseconds(250);
constexpr std::size_t kMaxPending = 64 * 1024; // a flood never grows the buffer without bound

// A function-local static, not a namespace-scope one: a log line can be the first thing another
// translation unit's static initializer does, before this file's globals would be constructed.
// It is never destroyed: static destructors run in reverse construction order, so one that logs
// would find the batch destroyed (and re-create it over a dead mutex). The tail goes out through
// atexit instead, and the _Exit paths call Flush() themselves.
struct Batch {
    std::mutex mutex;
    std::string pending;
    std::chrono::steady_clock::time_point last_flush = std::chrono::steady_clock::now();
    unsigned lines = 0;

    void FlushLocked() {
        if (!pending.empty()) {
            std::fwrite(pending.data(), 1, pending.size(), stderr);
            pending.clear();
        }
        std::fflush(stderr);
        last_flush = std::chrono::steady_clock::now();
        lines = 0;
    }
};

Batch& GetBatch() {
    static Batch& batch = []() -> Batch& {
        Batch& made = *new Batch; // never freed: it outlives every static destructor
        std::atexit([] {
            Batch& b = GetBatch();
            // Does not wait on the mutex, so a thread stuck in Write() cannot hang the exit.
            std::unique_lock lock{b.mutex, std::try_to_lock};
            if (lock.owns_lock()) {
                b.FlushLocked();
            } else {
                std::fflush(stderr);
            }
        });
        return made;
    }();
    return batch;
}

Level Threshold() {
    static const Level threshold = [] {
        const char* env = bb_env("BB_GPU_LOG");
        if (env) {
            if (std::strcmp(env, "off") == 0) {
                return Level::off;
            }
            for (int i = 0; i < 6; ++i) {
                if (std::strcmp(env, LevelNames[i]) == 0) {
                    return Level(i);
                }
            }
        }
        return Level::warn;
    }();
    return threshold;
}

} // namespace

bool ShouldLog(Level level) {
    return level >= Threshold();
}

void Flush() {
    Batch& batch = GetBatch();
    // try_lock: Flush also runs on the fatal path (assert), and waiting for a thread inside
    // Write() would turn the crash into a hang. The batch in flight is lost instead.
    std::unique_lock lock{batch.mutex, std::try_to_lock};
    if (!lock.owns_lock()) {
        std::fflush(stderr);
        return;
    }
    batch.FlushLocked();
}

void Write(std::string_view log_class, Level level, const char* file, int line,
           std::string&& message) {
    const char* slash = std::strrchr(file, '/');
    const int index = int(level) >= 0 && int(level) < 6 ? int(level) : 5;
    Batch& batch = GetBatch();
    std::scoped_lock lock{batch.mutex};
    fmt::format_to(std::back_inserter(batch.pending), "gpu {} ({}): {} [{}:{}]\n",
                   LevelNames[index], log_class, message, slash ? slash + 1 : file, line);
    if (level >= Level::warn || ++batch.lines >= kFlushLines ||
        batch.pending.size() >= kMaxPending ||
        std::chrono::steady_clock::now() - batch.last_flush >= kFlushInterval) {
        batch.FlushLocked();
    }
}

} // namespace Common::Log
