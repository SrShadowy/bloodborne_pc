// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: persistent VkPipelineCache driver serialization.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <vector>
#include "vulkan/vk_common.h"
#include "bbport_vk_pipeline_stats.h"

namespace BbVkPipelineCache {

constexpr const char* DRIVER_CACHE_NAME = "pipeline_driver.cache";

struct DriverCacheStatus {
    bool loaded = false;
    size_t size_bytes = 0;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    std::string device_name;
    bool cold_forced = false;
    // Pipeline-creation metrics: creates/hits prove the blob's hit-rate, compile_time is the
    // A/B number against a cold run; hits need BB_PIPELINE_STATS=1 (feedback extension).
    bool feedback_armed = false;
    uint64_t creates = 0;
    uint64_t hits = 0;
    uint64_t feedback_valid = 0;
    uint64_t compile_ns = 0;
    uint64_t longest_ns = 0;
};

// Reads saved driver cache if valid for this physical device
std::vector<uint8_t> LoadDriverCache(vk::PhysicalDevice physical_device, const std::filesystem::path& cache_dir = {});

// Queries pipeline cache data from Vulkan device and writes atomically to disk
bool SaveDriverCache(vk::Device device, vk::PipelineCache cache, const std::filesystem::path& cache_dir = {});

// Returns current status of driver cache (snapshot incl. the creation metrics)
DriverCacheStatus GetDriverCacheStatus();

// Creates the pipeline cache seeded with the persistent blob (UUID-checked) and registers it
// for FlushTick. If the driver rejects the saved data, falls back to an empty cache (no boot
// regression from a corrupt file); aborts only if an empty cache cannot be created.
vk::UniquePipelineCache Create(vk::Device device, vk::PhysicalDevice physical_device);

// Held across every vkCreate*Pipelines call: shared (creations stay parallel), while the
// periodic FlushTick save takes it exclusively (vkGetPipelineCacheData needs external sync
// against concurrent creates). One overload per call site's create-info type; also times the
// call and chains the creation-feedback struct (hit flag + duration, when armed).
class PipelineCreateGuard {
public:
    PipelineCreateGuard(const PipelineCreateGuard&) = delete;
    PipelineCreateGuard& operator=(const PipelineCreateGuard&) = delete;
    explicit PipelineCreateGuard(vk::ComputePipelineCreateInfo& ci);
    explicit PipelineCreateGuard(vk::GraphicsPipelineCreateInfo& ci);
    ~PipelineCreateGuard(); // records the create's time and cache-hit flag

private:
    void MarkCreate();

    std::shared_lock<std::shared_mutex> lock_;
    vk::PipelineCreationFeedbackCreateInfoEXT feedback_{}; // sType set when attached
    vk::PipelineCreationFeedback feedback_data_{};        // the report the driver fills in
    std::chrono::steady_clock::time_point begin_{};
};

// Called once per presented frame: persists the driver cache after a compile lull, and prints
// the session's creation metrics (the cold arm prints too — that pair is the A/B measurement).
void FlushTick();

} // namespace BbVkPipelineCache