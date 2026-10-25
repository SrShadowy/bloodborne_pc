// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_compile_queue.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/logging/log.h"

namespace BbCompileQueue {

namespace {

struct State {
    std::mutex mutex;
    std::condition_variable cv_work;
    std::condition_variable cv_idle;
    std::deque<uint64_t> work_queue;
    std::unordered_map<uint64_t, QueueTask> tasks;
    std::vector<std::thread> workers;
    bool running = false;
    uint32_t active_workers = 0;
    QueueStats stats{};
};

State& GetState() {
    static State s_state;
    return s_state;
}

uint64_t NowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

void WorkerLoop() {
    auto& s = GetState();
    while (true) {
        uint64_t hash = 0;
        {
            std::unique_lock lock(s.mutex);
            s.cv_work.wait(lock, [&] { return !s.running || !s.work_queue.empty(); });
            if (!s.running && s.work_queue.empty()) break;

            hash = s.work_queue.front();
            s.work_queue.pop_front();
            s.active_workers++;
        }

        // Simulate multi-phase pipeline compilation state transitions:
        // DISCOVERED -> TRANSLATED -> VALIDATED -> PIPELINE_READY -> VERIFIED
        UpdateTaskState(hash, CompileState::Translated);
        UpdateTaskState(hash, CompileState::Validated);
        UpdateTaskState(hash, CompileState::PipelineReady);
        UpdateTaskState(hash, CompileState::Verified);

        {
            std::lock_guard lock(s.mutex);
            s.active_workers--;
            if (s.work_queue.empty() && s.active_workers == 0) {
                s.cv_idle.notify_all();
            }
        }
    }
}

} // namespace

void Init(uint32_t num_threads) {
    auto& s = GetState();
    std::lock_guard lock(s.mutex);
    if (s.running) return;

    if (num_threads == 0) {
        const uint32_t hc = std::thread::hardware_concurrency();
        num_threads = std::clamp(hc > 2 ? hc / 2 : 1u, 1u, 8u);
    }

    s.running = true;
    s.workers.reserve(num_threads);
    for (uint32_t i = 0; i < num_threads; ++i) {
        s.workers.emplace_back(WorkerLoop);
    }
    LOG_INFO(Render, "BbCompileQueue: initialized with {} background worker threads", num_threads);
}

void Shutdown() {
    auto& s = GetState();
    {
        std::lock_guard lock(s.mutex);
        if (!s.running) return;
        s.running = false;
        s.cv_work.notify_all();
    }
    for (auto& w : s.workers) {
        if (w.joinable()) w.join();
    }
    std::lock_guard lock(s.mutex);
    s.workers.clear();
    s.active_workers = 0;
    LOG_INFO(Render, "BbCompileQueue: shut down cleanly");
}

bool IsRunning() {
    auto& s = GetState();
    std::lock_guard lock(s.mutex);
    return s.running;
}

void EnqueueTask(uint64_t hash, uint32_t stage, const std::string& location) {
    auto& s = GetState();
    std::lock_guard lock(s.mutex);
    if (!s.running) Init();

    auto [it, is_new] = s.tasks.try_emplace(hash);
    if (is_new) {
        QueueTask& task = it->second;
        task.hash = hash;
        task.stage = stage;
        task.state = CompileState::Discovered;
        task.location = location;
        task.enqueue_time_ns = NowNs();

        s.stats.discovered_count++;
        s.work_queue.push_back(hash);
        s.cv_work.notify_one();
    }
}

void UpdateTaskState(uint64_t hash, CompileState state, const std::string& error) {
    auto& s = GetState();
    std::lock_guard lock(s.mutex);
    auto it = s.tasks.find(hash);
    if (it == s.tasks.end()) return;

    QueueTask& task = it->second;
    task.state = state;
    if (!error.empty()) task.error_message = error;

    switch (state) {
    case CompileState::Translated: s.stats.translated_count++; break;
    case CompileState::Validated: s.stats.validated_count++; break;
    case CompileState::PipelineReady: s.stats.ready_count++; break;
    case CompileState::Verified:
        task.finish_time_ns = NowNs();
        s.stats.verified_count++;
        break;
    case CompileState::Failed:
        task.finish_time_ns = NowNs();
        s.stats.failed_count++;
        break;
    default: break;
    }
}

QueueStats GetStats() {
    auto& s = GetState();
    std::lock_guard lock(s.mutex);
    QueueStats stats = s.stats;
    stats.active_workers = s.active_workers;
    stats.pending_tasks = static_cast<uint32_t>(s.work_queue.size());
    return stats;
}

void WaitUntilIdle() {
    auto& s = GetState();
    std::unique_lock lock(s.mutex);
    s.cv_idle.wait(lock, [&] { return s.work_queue.empty() && s.active_workers == 0; });
}

} // namespace BbCompileQueue
