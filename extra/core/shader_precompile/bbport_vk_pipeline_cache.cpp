// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the persistent VkPipelineCache -- the cache handle, the background saver that writes it
// after a compile lull (so disk I/O never stalls the present thread), the create guard that keeps
// creations parallel while a save is exclusive, and the per-lull metrics line. The blob's reading
// and writing is in bbport_vk_pipeline_cache_io.cpp.
#include "bbport_vk_pipeline_cache.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#include "bbport_shader_pack.h"
#include "bbport_vk_pipeline_cache_internal.h"
#include "common/assert.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace BbVkPipelineCache {
namespace detail {

DriverCacheStatus g_status{};
std::shared_mutex g_mutex;

} // namespace detail

namespace {

using Clock = std::chrono::steady_clock;

// The cache handle is created once (Create) and never destroyed before the process ends (the game
// exits via _exit, so there is no teardown to hook).
vk::Device s_device{};
vk::PipelineCache s_cache{};
std::atomic<std::uint64_t> s_creates{0};       // pipeline creations since boot (the dirty signal)
std::atomic<std::uint64_t> s_saved_creates{0}; // creations covered by the last successful save
std::atomic<std::int64_t> s_last_create_ns{0}; // steady-clock ns of the newest create
Clock::time_point s_last_save{};               // last save attempt (present thread only)
std::uint64_t s_stats_line_creates = 0;        // creations covered by the last metrics line

// The background saver, so disk I/O and serialization never stall the render/present thread.
std::atomic<bool> s_stop_worker{false};
std::atomic<bool> s_save_requested{false};
std::mutex s_worker_mutex;
std::condition_variable s_worker_cv;
std::thread s_worker_thread;

/// Serializes the in-memory cache into `data`; false when nothing new has to be saved.
bool QueryCacheData(std::vector<std::uint8_t>& data, std::uint64_t& covered_creates) {
    using detail::g_mutex;
    using detail::g_status;

    if (!s_device || !s_cache || g_status.cold_forced) {
        return false;
    }
    // A creation holding the lock shared must not starve; busy means the save retries later.
    std::unique_lock lock(g_mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return false;
    }
    covered_creates = s_creates.load(std::memory_order_relaxed);
    if (covered_creates == s_saved_creates.load(std::memory_order_relaxed)) {
        return false;
    }
    std::size_t data_size = 0;
    if (s_device.getPipelineCacheData(s_cache, &data_size, nullptr) != vk::Result::eSuccess ||
        data_size <= sizeof(detail::PipelineCacheHeader)) {
        return false;
    }
    data.resize(data_size);
    if (s_device.getPipelineCacheData(s_cache, &data_size, data.data()) != vk::Result::eSuccess) {
        data.clear();
        return false;
    }
    return true;
}

/// Writes an already-serialized blob next to the driver's cache (temporary file, then rename).
bool WriteCacheData(const std::vector<std::uint8_t>& data, std::uint64_t covered_creates) {
    const auto final_dir = detail::ResolveCacheDir({});
    std::error_code ec;
    std::filesystem::create_directories(final_dir, ec);
    const auto tmp_path = final_dir / (std::string(DRIVER_CACHE_NAME) + ".tmp");
    const auto final_path = final_dir / DRIVER_CACHE_NAME;

    std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    file.close();
    if (!file.good()) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        return false;
    }
    s_saved_creates.store(covered_creates, std::memory_order_release);
    std::printf("[Shaders] VkPipelineCache: (background) Saved %zu bytes to %s\n", data.size(),
                final_path.string().c_str());
    return true;
}

void BackgroundWorker() {
    while (!s_stop_worker.load(std::memory_order_relaxed)) {
        {
            std::unique_lock lock(s_worker_mutex);
            s_worker_cv.wait_for(lock, std::chrono::seconds(2), [] {
                return s_save_requested.load(std::memory_order_relaxed) ||
                       s_stop_worker.load(std::memory_order_relaxed);
            });
        }
        if (s_stop_worker.load(std::memory_order_relaxed)) {
            break;
        }
        if (!s_save_requested.exchange(false, std::memory_order_acq_rel)) {
            continue;
        }

        // Wait for a compile storm to settle.
        const auto now = Clock::now().time_since_epoch().count();
        if (now - s_last_create_ns.load(std::memory_order_relaxed) < detail::kLullNs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            s_save_requested.store(true, std::memory_order_release);
            continue;
        }

        std::vector<std::uint8_t> data;
        std::uint64_t covered_creates = 0;
        if (!QueryCacheData(data, covered_creates)) {
            // A creation was in flight: come back once it is done.
            s_save_requested.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        // A gentle pause so background disk writing never contests the game.
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        WriteCacheData(data, covered_creates);
    }
}

} // namespace

DriverCacheStatus GetDriverCacheStatus() {
    DriverCacheStatus st = detail::g_status;
    const BbVkPipelineStats::Stats stats = BbVkPipelineStats::GetStats();
    st.feedback_armed = BbVkPipelineStats::s_armed.load(std::memory_order_relaxed);
    st.creates = stats.creates;
    st.hits = stats.hits;
    st.feedback_valid = stats.feedback_valid;
    st.compile_ns = stats.compile_ns;
    st.longest_ns = stats.longest_ns;
    return st;
}

vk::UniquePipelineCache Create(vk::Device device, vk::PhysicalDevice physical_device) {
    // Grimoire migration (blood_echoes.meta -> .bmeta): keeps the ~400 KB archive blob out of
    // BlobStore's *.meta glob; runs here so it always precedes the preload's meta scans.
    BbShaderPack::MigrateArchiveNames();

    const std::vector<std::uint8_t> initial = LoadDriverCache(physical_device);
    vk::PipelineCacheCreateInfo info{};
    if (!initial.empty()) {
        info.initialDataSize = initial.size();
        info.pInitialData = initial.data();
    }
    auto [result, cache] = device.createPipelineCacheUnique(info);
    if (result != vk::Result::eSuccess && !initial.empty()) {
        // A corrupt/foreign blob must never break the boot: retry without the initial data.
        std::printf("[Shaders] VkPipelineCache: driver rejected the saved cache (%s); starting "
                    "empty\n",
                    vk::to_string(result).c_str());
        auto [retry_result, retry_cache] = device.createPipelineCacheUnique({});
        result = retry_result;
        cache = std::move(retry_cache);
    }
    ASSERT_MSG(result == vk::Result::eSuccess, "Failed to create pipeline cache: {}",
               vk::to_string(result));
    s_device = device;
    s_cache = cache.get();

    // Start the background saver at the lowest priority, once.
    static std::once_flag s_worker_init_flag;
    std::call_once(s_worker_init_flag, [] {
        s_stop_worker = false;
        s_worker_thread = std::thread([] {
#ifdef _WIN32
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
#endif
            BackgroundWorker();
        });
        s_worker_thread.detach();
    });

    return std::move(cache);
}

PipelineCreateGuard::PipelineCreateGuard(vk::ComputePipelineCreateInfo& ci)
    : lock_{detail::g_mutex}, begin_{Clock::now()} {
    BbVkPipelineStats::AttachForCreate(ci, feedback_, feedback_data_);
    MarkCreate();
}

PipelineCreateGuard::PipelineCreateGuard(vk::GraphicsPipelineCreateInfo& ci)
    : lock_{detail::g_mutex}, begin_{Clock::now()} {
    BbVkPipelineStats::AttachForCreate(ci, feedback_, feedback_data_);
    MarkCreate();
}

PipelineCreateGuard::~PipelineCreateGuard() {
    // The wall time brackets the create call and its scope tail; when the feedback extension is
    // armed the driver's own duration is preferred (recorded by RecordCreate).
    BbVkPipelineStats::RecordCreate(
        feedback_,
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin_).count());
}

void PipelineCreateGuard::MarkCreate() {
    // Inside the shared lock: a saver holding the unique lock sees every increment already backed
    // by a finished create (the guard outlives its vkCreate*Pipelines call).
    s_last_create_ns.store(Clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    s_creates.fetch_add(1, std::memory_order_release);
}

void FlushTick() {
    if (!s_cache) {
        return;
    }
    const std::uint64_t creates = s_creates.load(std::memory_order_acquire);
    const std::uint64_t saved = s_saved_creates.load(std::memory_order_acquire);
    if (creates == saved && creates == s_stats_line_creates) {
        return; // nothing new to save nor to report since the last time
    }
    const auto now = Clock::now().time_since_epoch().count();
    if (now - s_last_save.time_since_epoch().count() < detail::kBackoffNs) {
        return;
    }
    if (now - s_last_create_ns.load(std::memory_order_relaxed) < detail::kLullNs) {
        return; // still in a compile storm (boot): wait for the lull
    }

    s_last_save = Clock::now();
    if (creates != saved && !detail::g_status.cold_forced) {
        // Signal the low-priority saver without blocking the present thread.
        s_save_requested.store(true, std::memory_order_release);
        s_worker_cv.notify_one();
    }

    // One metrics line per lull; the cold arm (BB_SHADER_COLD_RUN) prints it too -- that pair is
    // the A/B measurement. hits/valid need BB_PIPELINE_STATS=1 (creation feedback).
    const BbVkPipelineStats::Stats st = BbVkPipelineStats::GetStats();
    std::printf("[Shaders] VkPipelineCache: session: creates=%llu hits=%llu valid=%llu "
                "compile_time=%.0f ms longest=%.1f ms%s%s\n",
                static_cast<unsigned long long>(st.creates),
                static_cast<unsigned long long>(st.hits),
                static_cast<unsigned long long>(st.feedback_valid), st.compile_ns / 1000000.0,
                st.longest_ns / 1000000.0,
                detail::g_status.cold_forced ? " [cold run: not saved]" : "",
                BbVkPipelineStats::s_armed.load(std::memory_order_relaxed)
                    ? ""
                    : " [feedback off: hits/valid need BB_PIPELINE_STATS=1]");
    s_stats_line_creates = creates;
}

} // namespace BbVkPipelineCache
