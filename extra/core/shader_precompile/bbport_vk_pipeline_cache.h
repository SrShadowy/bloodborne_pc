// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: persistent VkPipelineCache driver serialization.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "vulkan/vk_common.h"

namespace BbVkPipelineCache {

constexpr const char* DRIVER_CACHE_NAME = "pipeline_driver.cache";

struct DriverCacheStatus {
    bool loaded = false;
    size_t size_bytes = 0;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    std::string device_name;
    bool cold_forced = false;
};

// Reads saved driver cache if valid for this physical device
std::vector<uint8_t> LoadDriverCache(vk::PhysicalDevice physical_device, const std::filesystem::path& cache_dir = {});

// Queries pipeline cache data from Vulkan device and writes atomically to disk
bool SaveDriverCache(vk::Device device, vk::PipelineCache cache, const std::filesystem::path& cache_dir = {});

// Returns current status of driver cache
DriverCacheStatus GetDriverCacheStatus();

} // namespace BbVkPipelineCache
