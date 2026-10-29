// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "upscale/frame_gen_common.h"
#include "upscale/vk_fsr_amd.h"
#include "render/vk_gpu_profiler.h"

namespace Vulkan {

namespace {

const char* ProviderName(int provider) {
    return provider == BbSettings::FrameGenDlss     ? "DLSS Frame Generation"
           : provider == BbSettings::FrameGenFsr3   ? "FSR 3.1 frame generation"
           : provider == BbSettings::FrameGenFsrAmd ? "AMD's frame generation DLL"
                                                    : "none";
}

std::atomic<u64> stat_captured{0}, stat_interpolated{0}, stat_ready{0}, stat_taken{0}, stat_missed{0};
void Stats() {
    static std::atomic<u64> calls{0};
    if (++calls % 2000 == 0) {
        std::printf("Frame generation stats: captured %llu interpolated %llu ready %llu taken %llu "
                    "missed %llu\n", (unsigned long long)stat_captured.load(),
                    (unsigned long long)stat_interpolated.load(), (unsigned long long)stat_ready.load(),
                    (unsigned long long)stat_taken.load(), (unsigned long long)stat_missed.load());
    }
}

} // namespace

FrameGeneration::FrameGeneration(const Instance& instance_, Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_} {
    auto& settings = BbSettings::Get();
    // FSR 3.1 frame interpolation: any GPU with its prerequisites.
    FfxVkPortableDeviceCapabilities caps{};
    caps.structSize = sizeof(caps);
    if (ffxVkPortableQueryDeviceCapabilities(instance.GetPhysicalDevice(), &caps) !=
        FFX_VK_PORTABLE_OK) {
        settings.frame_generation_problem = "the GPU's capabilities could not be read";
    } else if (!caps.fsr3FrameGenerationPrerequisites) {
        settings.frame_generation_problem = "the GPU lacks what FSR 3 frame generation needs";
    } else {
        supported = true;
    }
    settings.frame_generation_supported = supported;
    std::printf("Frame generation: FSR 3.1 %s%s%s\n", supported ? "available" : "unavailable",
                supported ? "" : ": ", supported ? "" : settings.frame_generation_problem.load());
    // DLSS Frame Generation: the bridge, NVIDIA's library, a GeForce RTX 40 or newer.
    const Dlss* dlss = Dlss::Get();
    dlss_max_count = dlss ? std::clamp(dlss->FrameGenMaxCount(), 0, int(MaxGenerated)) : 0;
    static std::string dlss_problem;
    dlss_problem = dlss ? dlss->FrameGenProblem()
                        : "the DLSS bridge and NVIDIA's DLSS libraries are not installed";
    if (dlss_max_count < 1 && dlss_problem.empty()) {
        dlss_problem = "DLSS Frame Generation is unavailable on this system";
    }
    settings.dlssg_max_multiplier = dlss_max_count + 1;
    settings.dlssg_problem = dlss_max_count > 0 ? nullptr : dlss_problem.c_str();
    if (dlss_max_count > 0) {
        std::printf("Frame generation: DLSS Frame Generation available, up to %dx\n",
                    dlss_max_count + 1);
    } else {
        std::printf("Frame generation: DLSS Frame Generation unavailable: %s\n",
                    dlss_problem.c_str());
    }
    // AMD's frame generation DLL (Windows, beside the FSR loader).
    amd = std::make_unique<FgAmd>(instance, scheduler);
    settings.fg_amd_supported = amd->Available();
    const int chosen = settings.frame_generation.load();
    if (chosen != BbSettings::FrameGenOff) {
        std::printf("Frame generation: %s chosen, %s\n", ProviderName(chosen),
                    Provider() == chosen ? "on" : "cannot run here: off");
    }
    // Asynchronous FSR 3.1 on the second compute queue (Instance::GetAsyncComputeQueue).
    if (supported && instance.GetAsyncComputeQueue()) {
        const auto device = instance.GetDevice();
        families = {instance.GetGraphicsQueueFamilyIndex(), instance.GetComputeQueueFamilyIndex()};
        vk::SemaphoreTypeCreateInfo timeline{.semaphoreType = vk::SemaphoreType::eTimeline,
                                             .initialValue = 0};
        compute_semaphore = Check(device.createSemaphoreUnique({.pNext = &timeline}));
        compute_pool = Check(device.createCommandPoolUnique({
            .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
            .queueFamilyIndex = instance.GetComputeQueueFamilyIndex(),
        }));
        const auto cmdbufs = Check(device.allocateCommandBuffers({
            .commandPool = *compute_pool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = u32(compute_cmdbufs.size()),
        }));
        std::ranges::copy(cmdbufs, compute_cmdbufs.begin());
        async = true;
        worker = std::jthread([this](std::stop_token stop) { WorkerLoop(stop); });
        std::printf("Frame generation: FSR 3.1 asynchronous (compute queue family %u)\n",
                    families[1]);
    }
}

FrameGeneration::~FrameGeneration() {
    if (worker.joinable()) {
        worker.request_stop();
        job_cv.notify_all();
        worker.join();
    }
    WaitAsyncIdle();
    Destroy();
}

vk::ImageCreateInfo FrameGeneration::Shared(vk::ImageCreateInfo info) const {
    if (async) {
        info.sharingMode = vk::SharingMode::eConcurrent;
        info.queueFamilyIndexCount = u32(families.size());
        info.pQueueFamilyIndices = families.data();
    }
    return info;
}

void FrameGeneration::WaitAsyncIdle() {
    if (!async) {
        return;
    }
    std::unique_lock lock{job_mutex};
    job_cv.wait(lock, [this] { return jobs.empty() && !worker_busy; });
    const vk::Semaphore compute = *compute_semaphore;
    const u64 value = compute_issued;
    if (value != 0) {
        (void)instance.GetDevice().waitSemaphores(
            {.semaphoreCount = 1, .pSemaphores = &compute, .pValues = &value}, UINT64_MAX);
    }
}

int FrameGeneration::Provider() const {
    const int chosen = BbSettings::Get().frame_generation.load(std::memory_order_relaxed);
    if (chosen == BbSettings::FrameGenDlss) {
        return dlss_max_count > 0 && !dlss_failed.load(std::memory_order_relaxed)
                   ? BbSettings::FrameGenDlss
                   : BbSettings::FrameGenOff;
    }
    if (chosen == BbSettings::FrameGenFsr3) {
        return supported && !fsr_failed.load(std::memory_order_relaxed) ? BbSettings::FrameGenFsr3
                                                                        : BbSettings::FrameGenOff;
    }
    if (chosen == BbSettings::FrameGenFsrAmd) {
        return amd && amd->Available() && !amd_failed.load(std::memory_order_relaxed)
                   ? BbSettings::FrameGenFsrAmd
                   : BbSettings::FrameGenOff;
    }
    return BbSettings::FrameGenOff;
}

bool FrameGeneration::Wanted() const {
    return Provider() != BbSettings::FrameGenOff;
}

u32 FrameGeneration::WantedCount() const {
    switch (Provider()) {
    case BbSettings::FrameGenDlss: {
        const int multiplier = BbSettings::Get().frame_generation_multiplier.load(std::memory_order_relaxed);
        return u32(std::clamp(multiplier - 1, 1, std::max(1, dlss_max_count)));
    }
    case BbSettings::FrameGenFsr3:
    case BbSettings::FrameGenFsrAmd:
        return 1;
    default:
        return 0;
    }
}

void FrameGeneration::Destroy() {
    if (context) {
        WaitAsyncIdle();
        scheduler.Finish();
        ffxVkPortableFrameGenerationContextDestroy(context);
        context = nullptr;
    }
    if (dlss_desc) {
        WaitAsyncIdle();
        scheduler.Finish(); // the recorded generations are done: the feature is free
        if (Dlss* dlss = Dlss::Get()) {
            dlss->ReleaseFrameGen();
        }
        dlss_desc.reset();
        dlss_create_pending = false;
    }
    if (amd) {
        amd->Destroy();
    }
}

void FrameGeneration::CaptureScene(const Scene& in) {
    scene_captured = false;
    const int provider = Provider();
    if (provider == BbSettings::FrameGenOff) {
        reset_next = true;
        return;
    }
    const bool ready = provider == BbSettings::FrameGenDlss
                           ? EnsureDlss(in.render_width, in.render_height, in.width, in.height,
                                        in.color_format)
                       : provider == BbSettings::FrameGenFsrAmd
                           ? EnsureAmd(in.render_width, in.render_height, in.width, in.height,
                                       in.color_format)
                           : EnsureContext(in.render_width, in.render_height, in.width, in.height,
                                           in.color_format);
    if (!ready) {
        reset_next = true;
        return;
    }
    if (provider != last_provider) {
        last_provider = provider; // the new provider has no history
        reset_next = true;
    }
    scene = in;
    scene_captured = true;
    scene_id = ++frame_id;
    hudless_ready = false;
    scene_reset = reset_next || in.reset;
    reset_next = false;
    ++stat_captured;
    // Asynchronous: the other set of inputs than the last frame's, once the interpolation that
    // read it (two frames ago, done by now as a rule) has finished.
    in_slot = async && provider == BbSettings::FrameGenFsr3 ? u32(scene_id & 1) : 0;
    if (const u64 value = In().compute_value; value != 0) {
        const vk::Semaphore semaphore = *compute_semaphore;
        (void)instance.GetDevice().waitSemaphores(
            {.semaphoreCount = 1, .pSemaphores = &semaphore, .pValues = &value}, UINT64_MAX);
    }
    // The frame is interpolated as shown, UI and all, with the frame before its UI beside it
    // when there is one (CaptureHudless). The generation runs at the display pass; the depth is
    // copied now, while it is this frame's (the game reuses its depth buffer).
    if (!In().depth_copy || In().depth_copy_format != in.depth_format || In().depth_copy_w != in.render_width ||
        In().depth_copy_h != in.render_height) {
        WaitAsyncIdle();
        scheduler.Finish();
        In().depth_copy_view.reset();
        In().depth_copy = VideoCore::HostImage(instance.GetDevice(), instance.GetAllocator());
        In().depth_copy.Create(Shared(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = in.depth_format,
            .extent = {in.render_width, in.render_height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eDepthStencilAttachment,
            .initialLayout = vk::ImageLayout::eUndefined,
        }));
        // DLSS samples the depth through a view of its depth aspect.
        In().depth_copy_view = Check<"create a frame generation depth view">(
            instance.GetDevice().createImageViewUnique({
                .image = vk::Image(In().depth_copy),
                .viewType = vk::ImageViewType::e2D,
                .format = in.depth_format,
                .subresourceRange = DepthRange,
            }));
        In().depth_copy_format = in.depth_format;
        In().depth_copy_w = in.render_width;
        In().depth_copy_h = in.render_height;
    }
    scheduler.Record([depth = in.depth, copy = vk::Image(In().depth_copy), w = in.render_width,
                      h = in.render_height](vk::CommandBuffer cmdbuf) {
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        const vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
        const std::array<vk::ImageMemoryBarrier2, 2> before{{
            {.srcStageMask = all, .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
             .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
             .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
             .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
             .image = depth, .subresourceRange = range},
            {.srcStageMask = all, .srcAccessMask = vk::AccessFlagBits2::eMemoryRead,
             .dstStageMask = vk::PipelineStageFlagBits2::eCopy,
             .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
             .oldLayout = vk::ImageLayout::eUndefined, .newLayout = vk::ImageLayout::eGeneral,
             .image = copy, .subresourceRange = range},
        }};
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = before.data()});
        const vk::ImageCopy region{
            .srcSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1},
            .dstSubresource = {vk::ImageAspectFlagBits::eDepth, 0, 0, 1},
            .extent = {w, h, 1},
        };
        cmdbuf.copyImage(depth, vk::ImageLayout::eGeneral, copy, vk::ImageLayout::eGeneral, region);
        const std::array<vk::ImageMemoryBarrier2, 2> after{{
            {.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
             .srcAccessMask = vk::AccessFlagBits2::eTransferRead, .dstStageMask = all,
             .dstAccessMask = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
             .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
             .image = depth, .subresourceRange = range},
            {.srcStageMask = vk::PipelineStageFlagBits2::eCopy,
             .srcAccessMask = vk::AccessFlagBits2::eTransferWrite, .dstStageMask = all,
             .dstAccessMask = vk::AccessFlagBits2::eMemoryRead,
             .oldLayout = vk::ImageLayout::eGeneral, .newLayout = vk::ImageLayout::eGeneral,
             .image = copy, .subresourceRange = range},
        }};
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 2, .pImageMemoryBarriers = after.data()});
    });
}

void FrameGeneration::CaptureHudless(vk::Image color) {
    if (!scene_captured || color != scene.color || hudless_ready) {
        return;
    }
    if (!In().hudless || In().hudless.image_ci.format != context_format ||
        In().hudless.image_ci.extent.width != context_w || In().hudless.image_ci.extent.height != context_h) {
        WaitAsyncIdle();
        scheduler.Finish();
        In().hudless_view.reset();
        In().hudless = VideoCore::HostImage(instance.GetDevice(), instance.GetAllocator());
        In().hudless.Create(Shared(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D,
            .format = context_format,
            .extent = {context_w, context_h, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = ColorUsage,
            .initialLayout = vk::ImageLayout::eUndefined,
        }));
        In().hudless_view = Check<"create a frame generation HUD-less view">(
            instance.GetDevice().createImageViewUnique({
                .image = vk::Image(In().hudless),
                .viewType = vk::ImageViewType::e2D,
                .format = context_format,
                .subresourceRange = ColorRange,
            }));
    }
    hudless_ready = true;
    scheduler.EndRendering();
    scheduler.Record([color, copy = vk::Image(In().hudless), w = scene.width,
                      h = scene.height](vk::CommandBuffer cmdbuf) {
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryWrite,
                     vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead);
        ImageBarrier(cmdbuf, copy, all, vk::AccessFlagBits2::eMemoryRead,
                     vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite,
                     vk::ImageLayout::eUndefined);
        const vk::ImageCopy region{
            .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .extent = {w, h, 1},
        };
        cmdbuf.copyImage(color, vk::ImageLayout::eGeneral, copy, vk::ImageLayout::eGeneral, region);
        ImageBarrier(cmdbuf, color, vk::PipelineStageFlagBits2::eCopy,
                     vk::AccessFlagBits2::eTransferRead, all,
                     vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        ImageBarrier(cmdbuf, copy, vk::PipelineStageFlagBits2::eCopy,
                     vk::AccessFlagBits2::eTransferWrite, all, vk::AccessFlagBits2::eMemoryRead);
    });
}

FrameGeneration::Target& FrameGeneration::EnsureTarget(u32 index) {
    Target& target = targets[index];
    if (!target.image || target.format != scene.color_format || target.width != scene.width ||
        target.height != scene.height) {
        WaitAsyncIdle();
        scheduler.Finish(); // its last encode may still read it
        const auto device = instance.GetDevice();
        target.view.reset();
        target.image = VideoCore::HostImage(device, instance.GetAllocator());
        target.image.Create(Shared(vk::ImageCreateInfo{
            // The presenter may view it as sRGB (as it views the display buffer).
            .flags = vk::ImageCreateFlagBits::eMutableFormat |
                     vk::ImageCreateFlagBits::eExtendedUsage,
            .imageType = vk::ImageType::e2D,
            .format = scene.color_format,
            .extent = {scene.width, scene.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = ColorUsage,
            .initialLayout = vk::ImageLayout::eUndefined,
        }));
        target.view = Check<"create a frame generation view">(device.createImageViewUnique({
            .image = vk::Image(target.image),
            .viewType = vk::ImageViewType::e2D,
            .format = scene.color_format,
            .subresourceRange = ColorRange,
        }));
        target.format = scene.color_format;
        target.width = scene.width;
        target.height = scene.height;
    }
    return target;
}

vk::ImageView FrameGeneration::ColorView(vk::Image image, vk::Format format) {
    if (color_view && color_viewed == image && color_view_format == format) {
        return *color_view;
    }
    WaitAsyncIdle();
    scheduler.Finish(); // the last generation may still read through the old view
    color_view = Check<"create a frame generation colour view">(
        instance.GetDevice().createImageViewUnique({
            .image = image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .subresourceRange = ColorRange,
        }));
    color_viewed = image;
    color_view_format = format;
    return *color_view;
}

void FrameGeneration::Interpolate(vk::Image color, VAddr display) {
    Stats();
    (void)display; // one set of outputs for all display buffers: the interpolation, the display
                   // pass and the presenter's encode are all in the draw stream's order
    if (!scene_captured || color != scene.color) {
        return;
    }
    const int provider = last_provider;
    if ((provider == BbSettings::FrameGenFsr3 && !context) ||
        (provider == BbSettings::FrameGenDlss && !dlss_desc) ||
        (provider == BbSettings::FrameGenFsrAmd && !amd->Ready())) {
        return;
    }
    scene_captured = false;
    const u32 count = provider == BbSettings::FrameGenDlss
                          ? std::min(WantedCount(), MaxGenerated)
                          : (Wanted() ? 1u : 0u);
    if (count == 0) {
        return; // switched off between the capture and the display pass
    }
    ++stat_interpolated;
    const u64 id = scene_id;
    const bool reset = scene_reset;
    if (async && provider == BbSettings::FrameGenFsr3) {
        if (!reset) ++stat_ready;
        InterpolateAsync(color, id, reset);
        return;
    }
    {
        std::scoped_lock lock{targets_mutex};
        for (u32 i = 0; i < MaxGenerated; ++i) {
            Target& target = i < count ? EnsureTarget(i) : targets[i];
            // Ready as recorded (the presenter's encode comes later in the same stream). The
            // first frame after a reset has nothing before it to interpolate from.
            target.ready = i < count && !reset;
            target.frame = id;
        }
        if (!reset) ++stat_ready;
    }
    // Its own mark: the GPU profile otherwise counted it to the game's last dispatch.
    if (auto* profiler = GpuProfiler::Get()) {
        const char* label = provider == BbSettings::FrameGenDlss ? "frame generation (DLSS)"
                            : provider == BbSettings::FrameGenFsrAmd
                                ? "frame generation (AMD's DLL)"
                                : "frame generation (FSR 3.1)";
        profiler->Mark(0xF6A0'0000ull ^ std::hash<std::string_view>{}(label),
                       [label] { return std::string{label}; });
    }
    if (provider == BbSettings::FrameGenDlss) {
        RecordDlss(color, id, reset, count);
    } else if (provider == BbSettings::FrameGenFsrAmd) {
        RecordAmd(color, id, reset, targets[0]);
    } else {
        RecordFsr(color, id, reset, targets[0]);
    }
}

void FrameGeneration::ReleaseOutput(u32 target, u64 frame, vk::Semaphore semaphore, u64 value) {
    {
        std::scoped_lock lock{targets_mutex};
        if (target >= targets.size()) {
            return;
        }
        Target& t = targets[target];
        t.released = std::max(t.released, frame);
        if (value != 0) {
            t.release_semaphore = semaphore; // later readers of one queue: the last one covers all
            t.release_value = value;
        }
    }
    job_cv.notify_all();
}

u32 FrameGeneration::TakeOutputs(VAddr display, std::array<Output, MaxGenerated>& out) {
    (void)display;
    if (!Wanted()) {
        return 0;
    }
    if (async && last_provider == BbSettings::FrameGenFsr3) {
        // The newest generated frame, ready once the compute queue is done with it; with the
        // display LUT this frame noted (the presenter applies it when it encodes the frame).
        vk::ImageView view{};
        vk::Sampler sampler{};
        vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        {
            std::scoped_lock lut_lock{lut_mutex};
            if (lut_frame == BbStats::frame_number.load(std::memory_order_relaxed)) {
                view = lut_view;
                sampler = lut_sampler;
                layout = lut_layout;
            }
        }
        std::scoped_lock lock{targets_mutex};
        Target* newest = nullptr;
        u32 index = 0;
        for (u32 i = 0; i < u32(targets.size()); ++i) {
            if (targets[i].ready && (!newest || targets[i].frame > newest->frame)) {
                newest = &targets[i];
                index = i;
            }
        }
        for (auto& t : targets) {
            t.ready = false; // an older one not shown: never handed out, nothing to give back
        }
        if (!newest) {
            ++stat_missed;
            return 0;
        }
        newest->handed = newest->frame;
        out[0] = {vk::Image(newest->image), *newest->view, newest->format, newest->width,
                  newest->height, newest->frame};
        out[0].wait_semaphore = *compute_semaphore;
        out[0].wait_value = newest->compute_value;
        out[0].target = index;
        out[0].lut_view = view;
        out[0].lut_sampler = sampler;
        out[0].lut_layout = layout;
        ++stat_taken;
        return 1;
    }
    std::scoped_lock lock{targets_mutex};
    u32 count = 0;
    for (u32 i = 0; i < MaxGenerated; ++i) {
        Target& t = targets[i];
        if (!t.ready || t.frame != targets[0].frame) {
            break;
        }
        out[count++] = {vk::Image(t.image), *t.view, t.format, t.width, t.height, t.frame};
        t.ready = false;
    }
    if (count == 0) {
        ++stat_missed;
    } else {
        ++stat_taken;
    }
    return count;
}

void FrameGeneration::Fail(int provider, const char* reason) {
    auto& settings = BbSettings::Get();
    std::printf("Frame generation: %s; %s off\n", reason, ProviderName(provider));
    if (provider == BbSettings::FrameGenDlss) {
        dlss_failed = true;
        settings.dlssg_problem = reason;
    } else if (provider == BbSettings::FrameGenFsrAmd) {
        amd_failed = true;
        settings.fg_amd_problem = reason;
    } else {
        fsr_failed = true;
        settings.frame_generation_problem = reason;
    }
}

} // namespace Vulkan
