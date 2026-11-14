// SPDX-License-Identifier: GPL-2.0-or-later
#include <array>
#include <cassert>
#include <cstdio>
#include "bbport_settings.h"
#include "render/scheduler.h"
#include "textures/image.h"
#include "upscale/vk_frame_generation.h"
#include "vulkan/instance.h"

int main() {
    std::printf("Testing FSR 3.1 Frame Generation...\n");
    auto& settings = BbSettings::Get();
    settings.frame_generation.store(BbSettings::FrameGenFsr3);

    Vulkan::Instance instance(0, false);
    static vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic d;
    d.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    d.init(instance.GetInstance());
    d.init(instance.GetDevice());
    const auto device = instance.GetDevice();
    const auto allocator = instance.GetAllocator();
    Vulkan::Scheduler scheduler(instance);

    constexpr u32 W = 1280, H = 720;
    const vk::Format disp_format = vk::Format::eR8G8B8A8Unorm;

    VideoCore::HostImage depth_img(device, allocator);
    depth_img.Create({
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eD32Sfloat,
        .extent = {W, H, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
                 vk::ImageUsageFlagBits::eDepthStencilAttachment,
        .initialLayout = vk::ImageLayout::eUndefined,
    });

    VideoCore::HostImage motion_img(device, allocator);
    motion_img.Create({
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16Sfloat,
        .extent = {W, H, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                 vk::ImageUsageFlagBits::eTransferSrc,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    auto motion_view = device.createImageViewUnique(
        {
            .image = vk::Image(motion_img),
            .viewType = vk::ImageViewType::e2D,
            .format = vk::Format::eR16G16Sfloat,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        },
        nullptr, d).value;

    VideoCore::HostImage color_img(device, allocator);
    color_img.Create({
        .flags = vk::ImageCreateFlagBits::eMutableFormat | vk::ImageCreateFlagBits::eExtendedUsage,
        .imageType = vk::ImageType::e2D,
        .format = disp_format,
        .extent = {W, H, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                 vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc |
                 vk::ImageUsageFlagBits::eTransferDst,
        .initialLayout = vk::ImageLayout::eUndefined,
    });

    {
        const auto cmd = scheduler.CommandBuffer();
        const std::array<vk::ImageMemoryBarrier2, 3> transitions{{
            {
                .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask =
                    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .image = vk::Image(depth_img),
                .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
            },
            {
                .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask =
                    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .image = vk::Image(motion_img),
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            {
                .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .dstAccessMask =
                    vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .image = vk::Image(color_img),
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
        }};
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = u32(transitions.size()),
                              .pImageMemoryBarriers = transitions.data()},
                             d);
        scheduler.Finish();
    }

    Vulkan::FrameGeneration fg(instance, scheduler);
    const char* problem = settings.frame_generation_problem.load();
    std::printf("FrameGen Wanted: %d Provider: %d (Problem: %s)\n", fg.Wanted(), fg.Provider(),
                problem ? problem : "none");
    if (!fg.Wanted()) {
        std::printf("Frame Generation not supported on this GPU, test skipped.\n");
        return 0;
    }
    assert(fg.Provider() == BbSettings::FrameGenFsr3);
    assert(fg.WantedCount() == 1);

    constexpr VAddr display_addr = 0x10000000ULL;
    for (int frame = 0; frame < 4; ++frame) {
        Vulkan::FrameGeneration::Scene scene{};
        scene.color = vk::Image(color_img);
        scene.color_format = disp_format;
        scene.width = W;
        scene.height = H;
        scene.depth = vk::Image(depth_img);
        scene.depth_format = vk::Format::eD32Sfloat;
        scene.motion = vk::Image(motion_img);
        scene.motion_view = *motion_view;
        scene.render_width = W;
        scene.render_height = H;
        scene.frame_ms = 16.6f;
        scene.camera_near = 0.1f;
        scene.camera_far = 3000.0f;
        scene.vertical_fov = 0.785f;
        scene.reset = (frame == 0);
        scene.camera_up[1] = 1.0f;
        scene.camera_right[0] = 1.0f;
        scene.camera_forward[2] = 1.0f;

        fg.CaptureScene(scene);
        if (frame % 2 == 0) {
            fg.CaptureHudless(vk::Image(color_img));
        }
        fg.Interpolate(vk::Image(color_img), display_addr);
        scheduler.Flush();
        scheduler.Finish();

        std::array<Vulkan::FrameGeneration::Output, Vulkan::FrameGeneration::MaxGenerated> out{};
        const u32 count = fg.TakeOutputs(display_addr, out);
        if (frame == 0) {
            assert(count == 0);
        } else {
            assert(count == 1);
            assert(out[0].image && out[0].view);
            assert(out[0].width == W && out[0].height == H && out[0].frame == u64(frame + 1));
            if (out[0].wait_semaphore) {
                const vk::SemaphoreWaitInfo wait_info{
                    .semaphoreCount = 1,
                    .pSemaphores = &out[0].wait_semaphore,
                    .pValues = &out[0].wait_value,
                };
                assert(device.waitSemaphores(wait_info, UINT64_MAX, d) == vk::Result::eSuccess);
                fg.ReleaseOutput(out[0].target, out[0].frame, {}, 0);
            }
            assert(fg.TakeOutputs(display_addr, out) == 0);
        }
    }

    std::puts("Frame Generation: capture, hudless, interpolate, take/release output PASS");
    return 0;
}
