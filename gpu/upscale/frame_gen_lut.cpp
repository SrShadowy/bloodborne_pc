// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "upscale/frame_gen_common.h"
#include "bbgpu/shaders/fg_display_lut_comp.h"
#include "vulkan/shader_module.h"

namespace Vulkan {

void FrameGeneration::NoteDisplayLut(vk::ImageView view, vk::Sampler sampler,
                                     vk::ImageLayout layout) {
    std::scoped_lock lock{lut_mutex};
    lut_view = view;
    lut_sampler = sampler;
    if (layout != lut_layout || lut_frame == ~0ull) {
        std::printf("Frame generation: the display LUT is sampled in layout %s\n",
                    vk::to_string(layout).c_str());
    }
    lut_layout = layout;
    lut_frame = BbStats::frame_number.load(std::memory_order_relaxed);
}

void FrameGeneration::ApplyDisplayLut(const Output& output) {
    // BB_FG_LUT=0 (diagnostics): the generated frames as FSR made them.
    static const bool off = bb_env_off("BB_FG_LUT");
    if (off) {
        return;
    }
    vk::ImageView view{};
    vk::Sampler sampler{};
    vk::ImageLayout layout{};
    {
        std::scoped_lock lock{lut_mutex};
        // Only this frame's: a view noted frames ago may belong to an image freed since.
        if (lut_frame != BbStats::frame_number.load(std::memory_order_relaxed)) {
            return;
        }
        view = lut_view;
        sampler = lut_sampler;
        layout = lut_layout;
    }
    if (!view || !sampler || !output.view) {
        return;
    }
    if (!EnsureLutPipeline()) {
        return;
    }
    scheduler.Record([this, image = output.image, target = output.view, view, sampler, layout,
                      w = output.width, h = output.height](vk::CommandBuffer cmdbuf) {
        RecordLut(cmdbuf, image, target, w, h, view, sampler, layout);
    });
}

void FrameGeneration::RecordDisplayLut(vk::CommandBuffer cmdbuf, const Output& output) {
    static const bool off = bb_env_off("BB_FG_LUT");
    if (off || !output.lut_view || !output.lut_sampler || !output.view || !EnsureLutPipeline()) {
        return;
    }
    RecordLut(cmdbuf, output.image, output.view, output.width, output.height, output.lut_view,
              output.lut_sampler, output.lut_layout);
}

bool FrameGeneration::EnsureLutPipeline() {
    static std::mutex mutex; // the draw stream (ApplyDisplayLut) or the presenter (asynchronous)
    std::scoped_lock lock{mutex};
    if (lut_pipeline) {
        return true;
    }
    const auto device = instance.GetDevice();
    const std::array<vk::DescriptorSetLayoutBinding, 3> bindings{{
        {.binding = 0, .descriptorType = vk::DescriptorType::eStorageImage,
         .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute},
        {.binding = 1, .descriptorType = vk::DescriptorType::eSampledImage,
         .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute},
        {.binding = 2, .descriptorType = vk::DescriptorType::eSampler,
         .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute},
    }};
    lut_desc_layout = Check(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = u32(bindings.size()), .pBindings = bindings.data()}));
    lut_pipeline_layout = Check(device.createPipelineLayoutUnique(
        {.setLayoutCount = 1, .pSetLayouts = &*lut_desc_layout}));
    const auto module = CompileSPV(FG_DISPLAY_LUT_COMP, device);
    lut_pipeline = Check(device.createComputePipelineUnique(
        {}, vk::ComputePipelineCreateInfo{
                .stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = module, .pName = "main"},
                .layout = *lut_pipeline_layout}));
    device.destroyShaderModule(module);
    std::printf("Frame generation: generated frames get the game's display LUT\n");
    return bool(lut_pipeline);
}

void FrameGeneration::RecordLut(vk::CommandBuffer cmdbuf, vk::Image image, vk::ImageView target,
                                u32 w, u32 h, vk::ImageView view, vk::Sampler sampler,
                                vk::ImageLayout layout) {
    const vk::ImageMemoryBarrier2 before{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &before});
    const vk::DescriptorImageInfo frame_info{.imageView = target, .imageLayout = vk::ImageLayout::eGeneral};
    // In the layout the game's display pass sampled it in (its descriptor's, noted with it):
    // read-only optimal was assumed, while the texture cache keeps many images General; AMD reads
    // either, NVIDIA read a black LUT, and every generated frame came out black (DLSS FG,
    // RTX 5060, Windows, 2026-10-27).
    const vk::DescriptorImageInfo lut_info{.imageView = view, .imageLayout = layout};
    const vk::DescriptorImageInfo sampler_info{.sampler = sampler};
    const std::array<vk::WriteDescriptorSet, 3> writes{{
        {.dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage,
         .pImageInfo = &frame_info},
        {.dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampledImage,
         .pImageInfo = &lut_info},
        {.dstBinding = 2, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eSampler,
         .pImageInfo = &sampler_info},
    }};
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *lut_pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *lut_pipeline_layout, 0, writes);
    cmdbuf.dispatch((w + 7) / 8, (h + 7) / 8, 1);
    const vk::ImageMemoryBarrier2 after{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
        .dstAccessMask = vk::AccessFlagBits2::eMemoryRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &after});
}

} // namespace Vulkan
