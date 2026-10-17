// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "vulkan/vk_common.h"

namespace Vulkan {
class Instance;
class Scheduler;
}

namespace BbScreenshot {

// Checks if a screenshot was requested and records the transfer copy + async file save.
// Returns true if a screenshot was captured (and the post-read barrier recorded), or false if skipped.
bool RecordCapture(
    const Vulkan::Instance& instance,
    Vulkan::Scheduler& present_scheduler,
    vk::CommandBuffer cmdbuf,
    vk::Image target,
    vk::Extent2D extent,
    vk::Format surface_format,
    bool overlay,
    const vk::ImageSubresourceRange& color_range
);

} // namespace BbScreenshot
