// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the pipeline-cache files share. bbport_vk_pipeline_cache.cpp owns the cache handle,
// the background saver and the create guard; bbport_vk_pipeline_cache_io.cpp reads and writes the
// driver's blob. Only this module needs this header.
#pragma once

#include <cstdint>
#include <filesystem>
#include <shared_mutex>

#include "bbport_vk_pipeline_cache.h"
#include "vulkan/vk_common.h"

namespace BbVkPipelineCache::detail {

// ---- Timings ----

/// A save is triggered this long after the newest pipeline creation (a lull, not a compile storm).
constexpr std::int64_t kLullNs = 3'000'000'000;
/// At most one save trigger per this window.
constexpr std::int64_t kBackoffNs = 15'000'000'000;

// ---- The blob's header ----

/// The driver's own cache header; its UUID and device ids tell whether a blob fits this GPU.
struct PipelineCacheHeader {
    std::uint32_t headerLength;
    std::uint32_t headerVersion;
    std::uint32_t vendorID;
    std::uint32_t deviceID;
    std::uint8_t pipelineCacheUUID[VK_UUID_SIZE];
};

// ---- State (bbport_vk_pipeline_cache.cpp) ----

/// What the last load/save did, and what the settings window shows.
extern DriverCacheStatus g_status;
/// Guards the cache handle against the saver's exclusive read (creations take it shared).
extern std::shared_mutex g_mutex;

/// Where the blob lives: `dir` when given, else the game serial's cache directory.
std::filesystem::path ResolveCacheDir(const std::filesystem::path& dir);

} // namespace BbVkPipelineCache::detail
