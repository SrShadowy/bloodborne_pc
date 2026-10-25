// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace BbCompileQueue {

enum class CompileState : uint32_t {
    Discovered = 0,
    Translated = 1,
    Validated = 2,
    PipelineReady = 3,
    Verified = 4,
    Failed = 5,
};

struct QueueTask {
    uint64_t hash = 0;
    uint32_t stage = 0;
    CompileState state = CompileState::Discovered;
    std::string location;
    std::string error_message;
    uint64_t enqueue_time_ns = 0;
    uint64_t finish_time_ns = 0;
};

struct QueueStats {
    uint32_t discovered_count = 0;
    uint32_t translated_count = 0;
    uint32_t validated_count = 0;
    uint32_t ready_count = 0;
    uint32_t verified_count = 0;
    uint32_t failed_count = 0;
    uint32_t active_workers = 0;
    uint32_t pending_tasks = 0;
};

void Init(uint32_t num_threads = 0);
void Shutdown();
bool IsRunning();

void EnqueueTask(uint64_t hash, uint32_t stage, const std::string& location = "");
void UpdateTaskState(uint64_t hash, CompileState state, const std::string& error = "");

QueueStats GetStats();
void WaitUntilIdle();

} // namespace BbCompileQueue
