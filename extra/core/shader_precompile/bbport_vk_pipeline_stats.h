// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: pipeline-creation metrics — wall time of every vkCreate*Pipelines call, plus the
// driver's per-pipeline cache-hit flag and duration (VK_(KHR|EXT)_pipeline_creation_feedback).
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include "vulkan/vk_common.h"

namespace BbVkPipelineStats {

// Session counters since boot; atomic so the creating threads never block on the flush log.
struct Stats {
    std::uint64_t creates = 0;        // vkCreate*Pipelines calls
    std::uint64_t hits = 0;           // served from the application cache (the seeded blob)
    std::uint64_t feedback_valid = 0; // with a driver-reported duration (extension armed)
    std::uint64_t compile_ns = 0;     // time inside those calls (driver time, wall fallback)
    std::uint64_t longest_ns = 0;     // slowest single create
};

// Set at device creation: the feedback extension is enabled only when BB_PIPELINE_STATS=1 asks
// for it, so the default device configuration stays identical to a run without this metric.
inline std::atomic<bool> s_armed{false};

// Device-creation hook (Instance::CreateDevice's has()/enable() lambdas): arms the metric when
// the driver offers the extension. Prefers the KHR promotion, falls back to the EXT original.
template <typename Has, typename Enable>
inline void HookDeviceExtension(const Has& has, const Enable& enable) {
    s_armed.store(false, std::memory_order_relaxed);
    const char* env = std::getenv("BB_PIPELINE_STATS");
    if (!env || env[0] == '0') {
        return;
    }
    // The bundled Vulkan headers predate the KHR promotion (only the EXT macro exists there),
    // so the KHR alias is spelled out; drivers advertise the EXT name as well either way.
    constexpr const char* kKhrName = "VK_KHR_pipeline_creation_feedback";
    if (has(kKhrName)) {
        enable(kKhrName);
        s_armed.store(true, std::memory_order_relaxed);
    } else if (has(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME)) {
        enable(VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME);
        s_armed.store(true, std::memory_order_relaxed);
    }
}

// Chains the feedback struct into the create-info's pNext (whatever it already carried —
// rendering info, subgroup sizes, ... — stays chained below it, never clobbered) and wires
// the data struct the driver reports into. No-op unless armed: an unarmed pNext would be a
// VUID violation the driver never enabled.
template <typename PipelineCI>
inline bool AttachForCreate(PipelineCI& ci, vk::PipelineCreationFeedbackCreateInfoEXT& fb,
                            vk::PipelineCreationFeedback& data) {
    if (!s_armed.load(std::memory_order_relaxed)) {
        return false;
    }
    fb.sType = vk::StructureType::ePipelineCreationFeedbackCreateInfoEXT;
    fb.pPipelineCreationFeedback = &data;
    fb.pNext = ci.pNext;
    ci.pNext = &fb;
    return true;
}

// Records one finished create: the driver's hit flag and duration when the feedback struct was
// filled (eValid); the guard's wall time is the fallback (extension unarmed or driver silent).
void RecordCreate(const vk::PipelineCreationFeedbackCreateInfoEXT& fb, std::uint64_t wall_ns);

Stats GetStats();

} // namespace BbVkPipelineStats