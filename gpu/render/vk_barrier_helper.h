// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "vulkan/vk_common.h"

namespace Vulkan {

/// Orders transfer writes/clears before compute shader SSBO reads/writes.
inline void BarrierCopyToCompute(
    vk::CommandBuffer cmdbuf,
    vk::PipelineStageFlags2 extra_src_stages = vk::PipelineStageFlagBits2::eNone,
    vk::AccessFlags2 dst_access = vk::AccessFlagBits2::eShaderStorageRead) {
    const vk::MemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eCopy | extra_src_stages,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = dst_access,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    });
}

/// Makes compute shader SSBO writes visible to later GPU passes, transfers, and host reads.
inline void BarrierComputeToUsers(vk::CommandBuffer cmdbuf) {
    const vk::MemoryBarrier2 barrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllGraphics |
                        vk::PipelineStageFlagBits2::eComputeShader |
                        vk::PipelineStageFlagBits2::eTransfer |
                        vk::PipelineStageFlagBits2::eHost,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
    };
    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier,
    });
}

} // namespace Vulkan
