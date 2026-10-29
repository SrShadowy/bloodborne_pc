// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "upscale/frame_gen_common.h"
#include "upscale/fg_profile.h"
#include "upscale/ffx_shaders.h"
#include "common/thread.h"

#include <chrono>

namespace Vulkan {

bool FrameGeneration::EnsureContext(u32 render_width, u32 render_height, u32 width, u32 height,
                                    vk::Format format) {
    if (context && context_render_w == render_width && context_render_h == render_height &&
        context_w == width && context_h == height && context_format == format) {
        return true;
    }
    Destroy();
    const auto device = instance.GetDevice();
    FfxVkPortableDeviceInfo device_info{};
    device_info.structSize = sizeof(device_info);
    device_info.instance = instance.GetInstance();
    device_info.physicalDevice = instance.GetPhysicalDevice();
    device_info.device = device;
    device_info.getDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
    device_info.queue = instance.GetGraphicsQueue();
    device_info.queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex();
    device_info.shaderFloat16Enabled = instance.IsShaderFloat16Enabled();
    device_info.subgroupSizeControlEnabled = instance.IsSubgroupSizeControlEnabled();
    device_info.synchronization2Enabled = VK_TRUE;
    device_info.shaderStorageImageWriteWithoutFormatEnabled = VK_TRUE;

    FfxVkPortableFrameGenerationCreateInfo create{};
    create.structSize = sizeof(create);
    create.maxRenderSize = {render_width, render_height};
    create.displaySize = {width, height};
    create.interpolationSourceFormat = static_cast<VkFormat>(format);
    create.outputFormat = static_cast<VkFormat>(format);
    if (const u64 issues = ffxVkPortableValidateFrameGenerationCreateInfo(&create)) {
        std::printf("Frame generation: invalid context (%s)\n", ffxVkPortableValidationIssueName(issues));
        Fail(BbSettings::FrameGenFsr3, "FSR 3 frame generation: invalid context");
        return false;
    }
    FfxShaders::Install(); // passes built in the port, before the pipelines are made
    if (ffxVkPortableFrameGenerationContextCreate(&device_info, &create, &context) !=
        FFX_VK_PORTABLE_OK) {
        context = nullptr;
        Fail(BbSettings::FrameGenFsr3, "FSR 3 frame generation: the context could not be made");
        return false;
    }
    context_render_w = render_width;
    context_render_h = render_height;
    context_w = width;
    context_h = height;
    context_format = format;
    reset_next = true;
    std::printf("Frame generation: FSR 3.1 %ux%u from %ux%u scenes (%s)\n", width, height,
                render_width, render_height, vk::to_string(format).c_str());
    return true;
}

FrameGeneration::Job FrameGeneration::BuildFsr(vk::Image color, vk::Image motion, u64 id,
                                               bool reset, Target& target) {
    Job job{};
    auto& dispatch = job.dispatch;
    dispatch.structSize = sizeof(dispatch);
    dispatch.currentColor = Describe(color, scene.color_format, scene.width, scene.height,
                                     ColorUsage | vk::ImageUsageFlagBits::eColorAttachment,
                                     vk::ImageAspectFlagBits::eColor,
                                     FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    // The UI-less frame (CaptureHudless): FSR interpolates it and lays the UI over unmoved. Without
    // one (no UI drawn this frame) the whole frame is interpolated.
    if (hudless_ready && !BbToggle::Disabled(1u << 30)) {
        dispatch.hudlessColor = Describe(vk::Image(In().hudless), scene.color_format, scene.width,
                                         scene.height, ColorUsage, vk::ImageAspectFlagBits::eColor,
                                         FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    } else {
        dispatch.hudlessColor.structSize = sizeof(dispatch.hudlessColor);
        dispatch.hudlessColor.image = VK_NULL_HANDLE;
    }
    hudless_ready = false;
    dispatch.distortionField.structSize = sizeof(dispatch.distortionField); // absent: described, null
    dispatch.output = Describe(vk::Image(target.image), scene.color_format, scene.width,
                               scene.height, ColorUsage, vk::ImageAspectFlagBits::eColor,
                               FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.displaySize = {scene.width, scene.height};
    dispatch.interpolationRect = {0, 0, scene.width, scene.height};
    dispatch.frameTimeMilliseconds = scene.frame_ms;
    dispatch.cameraNear = scene.camera_near;
    dispatch.cameraFar = scene.camera_far;
    dispatch.cameraVerticalFovRadians = scene.vertical_fov;
    dispatch.viewSpaceToMeters = 1.0f;
    dispatch.minLuminance = 0.0f;
    dispatch.maxLuminance = 1.0f;
    dispatch.transferFunction = FFX_VK_PORTABLE_TRANSFER_FUNCTION_SRGB;
    dispatch.reset = reset ? VK_TRUE : VK_FALSE;
    dispatch.frameId = id;

    {
        // Checked once with a placeholder command buffer (the real one is the recording thread's).
        static bool checked = false;
        if (!checked) {
            checked = true;
            FfxVkPortableFrameGenerationCreateInfo create{};
            create.structSize = sizeof(create);
            create.maxRenderSize = {context_render_w, context_render_h};
            create.displaySize = {context_w, context_h};
            create.interpolationSourceFormat = static_cast<VkFormat>(context_format);
            create.outputFormat = static_cast<VkFormat>(context_format);
            auto d = dispatch;
            d.commandBuffer = reinterpret_cast<VkCommandBuffer>(uintptr_t{1});
            if (const u64 issues = ffxVkPortableValidateFrameGenerationDispatchInfo(&create, &d)) {
                std::printf("Frame generation: dispatch: %s\n", ffxVkPortableValidationIssueName(issues));
            }
        }
    }
    auto& prepare = job.prepare;
    prepare.structSize = sizeof(prepare);
    prepare.depth = Describe(vk::Image(In().depth_copy), scene.depth_format, scene.render_width,
                             scene.render_height, vk::ImageUsageFlagBits::eSampled,
                             vk::ImageAspectFlagBits::eDepth, FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    prepare.motionVectors = Describe(motion, vk::Format::eR16G16Sfloat, scene.render_width,
                                     scene.render_height,
                                     vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                                     vk::ImageAspectFlagBits::eColor,
                                     FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
    prepare.renderSize = {scene.render_width, scene.render_height};
    prepare.jitterOffset = {scene.jitter_x, scene.jitter_y};
    prepare.motionVectorScale = {1.0f, 1.0f};
    prepare.frameTimeMilliseconds = scene.frame_ms;
    prepare.cameraNear = scene.camera_near;
    prepare.cameraFar = scene.camera_far;
    prepare.cameraVerticalFovRadians = scene.vertical_fov;
    prepare.viewSpaceToMeters = 1.0f;
    prepare.minLuminance = 0.0f;
    prepare.maxLuminance = 1.0f;
    prepare.transferFunction = FFX_VK_PORTABLE_TRANSFER_FUNCTION_SRGB;
    prepare.cameraPosition = {scene.camera_position[0], scene.camera_position[1], scene.camera_position[2]};
    prepare.cameraUp = {scene.camera_up[0], scene.camera_up[1], scene.camera_up[2]};
    prepare.cameraRight = {scene.camera_right[0], scene.camera_right[1], scene.camera_right[2]};
    prepare.cameraForward = {scene.camera_forward[0], scene.camera_forward[1], scene.camera_forward[2]};
    prepare.reset = reset ? VK_TRUE : VK_FALSE;
    prepare.frameId = id;
    job.source =
        dispatch.hudlessColor.image != VK_NULL_HANDLE ? dispatch.hudlessColor : dispatch.currentColor;
    job.color = color;
    job.out = vk::Image(target.image);
    job.width = target.width;
    job.height = target.height;
    return job;
}

void FrameGeneration::RecordJob(vk::CommandBuffer cmdbuf, Job& job) {
    const vk::Image color = job.color, out = job.out;
    const auto all = vk::PipelineStageFlagBits2::eAllCommands;
    // The UI drew into it; the display pass reads it after.
    ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryWrite,
                 vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
    ImageBarrier(cmdbuf, out, all, vk::AccessFlagBits2::eMemoryRead,
                 vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderStorageWrite, vk::ImageLayout::eUndefined);
    job.prepare.commandBuffer = cmdbuf;
    job.dispatch.commandBuffer = cmdbuf;
    FgProfile::Begin(instance, cmdbuf);
    const auto prepared = ffxVkPortableFrameGenerationContextPrepare(context, &job.prepare, &job.source);
    const auto dispatched = prepared == FFX_VK_PORTABLE_OK
                                ? ffxVkPortableFrameGenerationContextRecordDispatch(context, &job.dispatch)
                                : prepared;
    FgProfile::End(cmdbuf);
    const bool ok = dispatched == FFX_VK_PORTABLE_OK;
    if (!ok) {
        static int told = 0;
        if (told++ < 4) {
            std::printf("Frame generation: the interpolation could not be recorded (%s %d)\n",
                        prepared != FFX_VK_PORTABLE_OK ? "prepare" : "dispatch", int(dispatched));
        }
    }
    ImageBarrier(cmdbuf, color, vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderRead, all,
                 vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
    ImageBarrier(cmdbuf, out, vk::PipelineStageFlagBits2::eComputeShader,
                 vk::AccessFlagBits2::eShaderStorageWrite, all, vk::AccessFlagBits2::eMemoryRead);
    if (!ok) {
        // Shown anyway (it was handed out as ready): the real frame twice, not garbage.
        CopyFrame(cmdbuf, color, out, job.width, job.height);
    }
}

void FrameGeneration::RecordFsr(vk::Image color, u64 id, bool reset, Target& target) {
    Job job = BuildFsr(color, scene.motion, id, reset, target);
    scheduler.EndRendering();
    scheduler.Record([this, job](vk::CommandBuffer cmdbuf) mutable { RecordJob(cmdbuf, job); });
}

void FrameGeneration::InterpolateAsync(vk::Image color, u64 id, bool reset) {
    Inputs& in = In();
    // Copies of the frame and its motion vectors (both reused by the next frame), in this set.
    const auto ensure = [&](VideoCore::HostImage& image, vk::Format format, u32 w, u32 h,
                            vk::ImageUsageFlags usage, vk::ImageCreateFlags flags) {
        if (image && image.image_ci.format == format && image.image_ci.extent.width == w &&
            image.image_ci.extent.height == h) {
            return;
        }
        WaitAsyncIdle();
        scheduler.Finish();
        image = VideoCore::HostImage(instance.GetDevice(), instance.GetAllocator());
        image.Create(Shared(vk::ImageCreateInfo{
            .flags = flags,
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = {w, h, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = usage,
            .initialLayout = vk::ImageLayout::eUndefined,
        }));
    };
    ensure(in.color_copy, scene.color_format, scene.width, scene.height,
           ColorUsage | vk::ImageUsageFlagBits::eColorAttachment, {});
    ensure(in.motion_copy, vk::Format::eR16G16Sfloat, scene.render_width, scene.render_height,
           vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
               vk::ImageUsageFlagBits::eTransferDst,
           {});
    const u32 index = next_target++ % u32(targets.size());
    Target* target = &targets[index];
    if (!target->image || target->format != scene.color_format || target->width != scene.width ||
        target->height != scene.height) {
        WaitAsyncIdle(); // before the lock: the worker takes it
    }
    {
        std::scoped_lock lock{targets_mutex};
        target = &EnsureTarget(index);
    }
    Job job = BuildFsr(vk::Image(in.color_copy), vk::Image(in.motion_copy), id, reset, *target);
    job.target = index;
    {
        std::scoped_lock lock{targets_mutex};
        job.wait_handed = target->handed;
    }
    scheduler.EndRendering();
    scheduler.Record([color, motion = scene.motion, color_copy = vk::Image(in.color_copy),
                      motion_copy = vk::Image(in.motion_copy), w = scene.width, h = scene.height,
                      rw = scene.render_width, rh = scene.render_height](vk::CommandBuffer cmdbuf) {
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        const auto copy = [&](vk::Image from, vk::Image to, u32 width, u32 height) {
            ImageBarrier(cmdbuf, from, all, vk::AccessFlagBits2::eMemoryWrite,
                         vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
            ImageBarrier(cmdbuf, to, all, vk::AccessFlagBits2::eNone,
                         vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                         vk::ImageLayout::eUndefined);
            cmdbuf.copyImage(from, vk::ImageLayout::eGeneral, to, vk::ImageLayout::eGeneral,
                             vk::ImageCopy{.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                           .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                                           .extent = {width, height, 1}});
            ImageBarrier(cmdbuf, from, vk::PipelineStageFlagBits2::eCopy,
                         vk::AccessFlagBits2::eTransferRead, all,
                         vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
            ImageBarrier(cmdbuf, to, vk::PipelineStageFlagBits2::eCopy,
                         vk::AccessFlagBits2::eTransferWrite, all, vk::AccessFlagBits2::eMemoryRead);
        };
        copy(color, color_copy, w, h);
        copy(motion, motion_copy, rw, rh);
    });
    // The frame goes out now; the interpolation waits for it on the compute queue.
    job.graphics_tick = scheduler.CurrentTick();
    scheduler.Flush();
    {
        std::scoped_lock lock{job_mutex};
        job.value = ++compute_issued;
        in.compute_value = job.value;
        jobs.push_back(job);
    }
    job_cv.notify_all();
    {
        std::scoped_lock lock{targets_mutex};
        target->frame = id;
        target->compute_value = job.value;
        target->ready = !reset;
    }
}

void FrameGeneration::WorkerLoop(std::stop_token stop) {
    Common::SetCurrentThreadName("bb:FrameGen");
    const auto device = instance.GetDevice();
    const vk::Semaphore compute = *compute_semaphore;
    const vk::Semaphore graphics = scheduler.GetWorkSemaphore()->Handle();
    while (true) {
        Job job;
        {
            std::unique_lock lock{job_mutex};
            worker_busy = false;
            job_cv.notify_all();
            if (!job_cv.wait(lock, stop, [this] { return !jobs.empty(); })) {
                return;
            }
            job = jobs.front();
            jobs.pop_front();
            worker_busy = true;
        }
        // Its target's last reader (the presenter's encode of a frame shown before).
        vk::Semaphore release_semaphore{};
        u64 release_value = 0;
        {
            std::unique_lock lock{targets_mutex};
            Target& target = targets[job.target];
            const auto given_back = [&] { return target.released >= job.wait_handed; };
            if (!given_back() &&
                !job_cv.wait_for(lock, stop, std::chrono::seconds(1), given_back)) {
                if (stop.stop_requested()) {
                    return;
                }
                std::printf("Frame generation: a generated frame was not given back; reused\n");
                target.released = job.wait_handed;
            }
            release_semaphore = target.release_semaphore;
            release_value = target.release_value;
        }
        // The command buffer of three jobs ago is done with.
        const vk::CommandBuffer cmdbuf = compute_cmdbufs[job.value % compute_cmdbufs.size()];
        if (job.value > compute_cmdbufs.size()) {
            const u64 done = job.value - compute_cmdbufs.size();
            (void)device.waitSemaphores({.semaphoreCount = 1, .pSemaphores = &compute, .pValues = &done},
                                        UINT64_MAX);
        }
        Check(cmdbuf.reset());
        Check(cmdbuf.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit}));
        RecordJob(cmdbuf, job);
        Check(cmdbuf.end());
        // The frame's submission first in its queue (a timeline wait may come before its signal,
        // but not on every driver path).
        scheduler.WaitSubmitted(job.graphics_tick);
        std::array<vk::SemaphoreSubmitInfo, 2> waits{{
            {.semaphore = graphics, .value = job.graphics_tick,
             .stageMask = vk::PipelineStageFlagBits2::eAllCommands},
            {.semaphore = release_semaphore, .value = release_value,
             .stageMask = vk::PipelineStageFlagBits2::eAllCommands},
        }};
        const vk::SemaphoreSubmitInfo signal{
            .semaphore = compute, .value = job.value,
            .stageMask = vk::PipelineStageFlagBits2::eAllCommands};
        const vk::CommandBufferSubmitInfo cmd_info{.commandBuffer = cmdbuf};
        const vk::SubmitInfo2 submit{
            .waitSemaphoreInfoCount = release_semaphore ? 2u : 1u,
            .pWaitSemaphoreInfos = waits.data(),
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cmd_info,
            .signalSemaphoreInfoCount = 1,
            .pSignalSemaphoreInfos = &signal,
        };
        {
            std::scoped_lock lock{instance.AsyncComputeMutex(), Scheduler::submit_mutex};
            Check<"submit the frame generation">(instance.GetAsyncComputeQueue().submit2(submit));
        }
    }
}

} // namespace Vulkan
