// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: persistent VkPipelineCache driver serialization implementation.
#include "bbport_vk_pipeline_cache.h"
#include "bbport_shader_pack.h"
#include "common/assert.h"
#include "common/elf_info.h"
#include "common/path_util.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <shared_mutex>

namespace BbVkPipelineCache {

namespace {

static DriverCacheStatus s_status{};

struct PipelineCacheHeader {
    uint32_t headerLength;
    uint32_t headerVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint8_t pipelineCacheUUID[VK_UUID_SIZE];
};

std::filesystem::path ResolveCacheDir(const std::filesystem::path& dir) {
    if (!dir.empty()) {
        return dir;
    }
    const auto serial = Common::ElfInfo::Instance().GameSerial();
    const std::string s = serial.empty() ? "CUSA03173" : std::string(serial);
    return Common::FS::GetUserPath(Common::FS::PathType::CacheDir) / s;
}

using Clock = std::chrono::steady_clock;

// The flush state: the cache handle is created once (Create) and never destroyed before the
// process ends (the game exits via _exit, no teardown), so no unregister is needed.
std::shared_mutex s_mutex;             // external sync for s_cache (guards vs creates)
vk::Device s_device{};                 // registered by Create
vk::PipelineCache s_cache{};           // registered by Create
std::atomic<std::uint64_t> s_creates{0}; // pipeline creations since boot (the dirty signal)
std::uint64_t s_saved_creates = 0;     // creations covered by the last successful save
std::atomic<std::int64_t> s_last_create_ns{0}; // steady-clock ns of the newest create
Clock::time_point s_last_save{};       // last save attempt (present thread only)
std::uint64_t s_stats_line_creates = 0; // creations covered by the last metrics line (present thread only)
// shortcut: heuristic lull/backoff for the periodic save; the mutex is what makes it safe
constexpr std::int64_t kLullNs = 2'000'000'000;     // save only 2 s after the newest create
constexpr std::int64_t kBackoffNs = 5'000'000'000;  // and at most one save per 5 s

} // namespace

DriverCacheStatus GetDriverCacheStatus() {
    DriverCacheStatus st = s_status;
    const BbVkPipelineStats::Stats stats = BbVkPipelineStats::GetStats();
    st.feedback_armed = BbVkPipelineStats::s_armed.load(std::memory_order_relaxed);
    st.creates = stats.creates;
    st.hits = stats.hits;
    st.feedback_valid = stats.feedback_valid;
    st.compile_ns = stats.compile_ns;
    st.longest_ns = stats.longest_ns;
    return st;
}

std::vector<uint8_t> LoadDriverCache(vk::PhysicalDevice physical_device, const std::filesystem::path& cache_dir) {
    const auto props = physical_device.getProperties();
    s_status.vendor_id = props.vendorID;
    s_status.device_id = props.deviceID;
    s_status.device_name = props.deviceName.data();
    s_status.loaded = false;
    s_status.size_bytes = 0;

    const char* cold_env = std::getenv("BB_SHADER_COLD_RUN");
    if (cold_env && cold_env[0] != '0') {
        s_status.cold_forced = true;
        std::printf("[Shaders] VkPipelineCache: Cold run requested (BB_SHADER_COLD_RUN); skipping persistent driver cache.\n");
        return {};
    }

    const auto final_dir = ResolveCacheDir(cache_dir);
    const auto p = final_dir / DRIVER_CACHE_NAME;
    if (!std::filesystem::exists(p)) {
        return {};
    }

    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        return {};
    }

    const auto file_size = f.tellg();
    if (file_size < static_cast<std::streampos>(sizeof(PipelineCacheHeader))) {
        return {};
    }

    f.seekg(0, std::ios::beg);
    PipelineCacheHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f.good() || hdr.headerLength < sizeof(PipelineCacheHeader) || hdr.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) {
        return {};
    }

    if (hdr.vendorID != props.vendorID || hdr.deviceID != props.deviceID ||
        std::memcmp(hdr.pipelineCacheUUID, props.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
        std::printf("[Shaders] VkPipelineCache: Driver cache UUID mismatch; discarding old driver cache.\n");
        return {};
    }

    std::vector<uint8_t> data(static_cast<size_t>(file_size));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(data.data()), file_size);
    if (!f.good()) {
        return {};
    }

    s_status.loaded = true;
    s_status.size_bytes = data.size();
    std::printf("[Shaders] VkPipelineCache: Loaded persistent driver cache (%zu bytes) for device 0x%04x:0x%04x (%s)\n",
                data.size(), props.vendorID, props.deviceID, props.deviceName.data());
    return data;
}

bool SaveDriverCache(vk::Device device, vk::PipelineCache cache, const std::filesystem::path& cache_dir) {
    if (!cache || !device) {
        return false;
    }
    if (s_status.cold_forced) {
        // A cold run compiles from scratch; saving its empty cache would clobber a real one.
        return false;
    }

    size_t data_size = 0;
    const auto res1 = device.getPipelineCacheData(cache, &data_size, nullptr);
    if (res1 != vk::Result::eSuccess || data_size <= sizeof(PipelineCacheHeader)) {
        return false;
    }

    std::vector<uint8_t> data(data_size);
    const auto res2 = device.getPipelineCacheData(cache, &data_size, data.data());
    if (res2 != vk::Result::eSuccess) {
        return false;
    }

    const auto final_dir = ResolveCacheDir(cache_dir);
    const auto tmp_p = final_dir / (std::string(DRIVER_CACHE_NAME) + ".tmp");
    const auto final_p = final_dir / DRIVER_CACHE_NAME;

    std::ofstream f(tmp_p, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        return false;
    }
    f.write(reinterpret_cast<const char*>(data.data()), data.size());
    f.close();
    if (!f.good()) {
        std::filesystem::remove(tmp_p);
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(tmp_p, final_p, ec);
    if (!ec) {
        std::printf("[Shaders] VkPipelineCache: Saved %zu bytes to %s\n", data.size(), final_p.string().c_str());
        return true;
    }
    return false;
}

vk::UniquePipelineCache Create(vk::Device device, vk::PhysicalDevice physical_device) {
    // Grimoire migration (blood_echoes.meta -> .bmeta): keeps the ~400 KB archive blob out of
    // BlobStore's *.meta glob; runs here so it always precedes the preload's meta scans.
    BbShaderPack::MigrateArchiveNames();

    const std::vector<uint8_t> initial = LoadDriverCache(physical_device);
    vk::PipelineCacheCreateInfo info{};
    if (!initial.empty()) {
        info.initialDataSize = initial.size();
        info.pInitialData = initial.data();
    }
    auto [result, cache] = device.createPipelineCacheUnique(info);
    if (result != vk::Result::eSuccess && !initial.empty()) {
        // A corrupt/foreign blob must never break the boot: retry without initial data.
        std::printf("[Shaders] VkPipelineCache: driver rejected the saved cache (%s); starting empty\n",
                    vk::to_string(result).c_str());
        auto [retry_result, retry_cache] = device.createPipelineCacheUnique({});
        result = retry_result;
        cache = std::move(retry_cache);
    }
    ASSERT_MSG(result == vk::Result::eSuccess, "Failed to create pipeline cache: {}",
               vk::to_string(result));
    s_device = device;
    s_cache = cache.get();
    return std::move(cache);
}

PipelineCreateGuard::PipelineCreateGuard(vk::ComputePipelineCreateInfo& ci)
    : lock_{s_mutex}, begin_{Clock::now()} {
    BbVkPipelineStats::AttachForCreate(ci, feedback_, feedback_data_);
    MarkCreate();
}

PipelineCreateGuard::PipelineCreateGuard(vk::GraphicsPipelineCreateInfo& ci)
    : lock_{s_mutex}, begin_{Clock::now()} {
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
    // Inside the shared lock: a flush holding the unique lock sees every increment already
    // backed by a finished create (the guard outlives its vkCreate*Pipelines call).
    s_last_create_ns.store(Clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    s_creates.fetch_add(1, std::memory_order_release);
}

void FlushTick() {
    if (!s_cache) {
        return;
    }
    const std::uint64_t creates = s_creates.load(std::memory_order_acquire);
    if (creates == s_saved_creates && creates == s_stats_line_creates) {
        return; // nothing new to save nor to report since the last time
    }
    const auto now = Clock::now().time_since_epoch().count();
    if (now - s_last_save.time_since_epoch().count() < kBackoffNs) {
        return;
    }
    if (now - s_last_create_ns.load(std::memory_order_relaxed) < kLullNs) {
        return; // still in a compile storm (boot): wait for the lull
    }
    // Exclusive: every PipelineCreateGuard (vkCreate*Pipelines) is done for the duration of
    // the query — vkGetPipelineCacheData requires external synchronization against creates.
    std::lock_guard lock{s_mutex};
    s_last_save = Clock::now();
    if (creates != s_saved_creates && !s_status.cold_forced && SaveDriverCache(s_device, s_cache)) {
        s_saved_creates = s_creates.load(std::memory_order_relaxed);
    }
    // On failure (and cold runs) keep s_saved_creates: retried after the backoff.
    // One metrics line per lull; the cold arm (BB_SHADER_COLD_RUN) prints it too — that pair
    // is the A/B measurement. hits/valid need BB_PIPELINE_STATS=1 (creation feedback).
    const BbVkPipelineStats::Stats st = BbVkPipelineStats::GetStats();
    std::printf("[Shaders] VkPipelineCache: session: creates=%llu hits=%llu valid=%llu compile_time=%.0f ms longest=%.1f ms%s%s\n",
                static_cast<unsigned long long>(st.creates),
                static_cast<unsigned long long>(st.hits),
                static_cast<unsigned long long>(st.feedback_valid),
                st.compile_ns / 1000000.0, st.longest_ns / 1000000.0,
                s_status.cold_forced ? " [cold run: not saved]" : "",
                BbVkPipelineStats::s_armed.load(std::memory_order_relaxed)
                    ? ""
                    : " [feedback off: hits/valid need BB_PIPELINE_STATS=1]");
    s_stats_line_creates = creates;
}

} // namespace BbVkPipelineCache
