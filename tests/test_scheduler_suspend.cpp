// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Headless GPU test for Scheduler VK_RENDERING_SUSPENDING_BIT / RESUMING_BIT chaining and
// automatic eStore+eLoad fallbacks across worker recording segments.
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "vulkan/vk_common.h"
#include <vk_mem_alloc.h>

#include "core/emulator_settings.h"
#include "render/scheduler.h"
#include "textures/image.h"
#include "vulkan/instance.h"

namespace {

using namespace Vulkan;

constexpr u32 kW = 256;
constexpr u32 kH = 256;

constexpr u32 kRed = 0xFF0000FFu;
constexpr u32 kGreen = 0xFF00FF00u;
constexpr u32 kBlue = 0xFFFF0000u;
constexpr u32 kYellow = 0xFF00FFFFu;
constexpr u32 kCyan = 0xFFFFFF00u;
constexpr u32 kMagenta = 0xFFFF00FFu;

vk::ClearValue MakeColor(u32 rgba) {
    vk::ClearValue cv{};
    cv.color.float32 = std::array{
        float(rgba & 0xFFu) / 255.0f,
        float((rgba >> 8) & 0xFFu) / 255.0f,
        float((rgba >> 16) & 0xFFu) / 255.0f,
        float((rgba >> 24) & 0xFFu) / 255.0f,
    };
    return cv;
}

std::array<u32, 4> MakeClearU32(u32 rgba) {
    return MakeColor(rgba).color.uint32;
}

struct TestTarget {
    VideoCore::HostImage image;
    vk::ImageView view{};
    vk::Device device{};
    const vk::detail::DispatchLoaderDynamic& dispatch;

    TestTarget(const Instance& instance, const vk::detail::DispatchLoaderDynamic& disp)
        : image{instance.GetDevice(), instance.GetAllocator()}, device{instance.GetDevice()},
          dispatch{disp} {
        image.Create({
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR8G8B8A8Unorm,
            .extent = {kW, kH, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eColorAttachment |
                     vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eUndefined,
        });
        const auto [res, v] = device.createImageView(
            {
                .image = vk::Image(image),
                .viewType = vk::ImageViewType::e2D,
                .format = vk::Format::eR8G8B8A8Unorm,
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            nullptr, dispatch);
        assert(res == vk::Result::eSuccess);
        view = v;
    }

    ~TestTarget() {
        if (view) {
            device.destroyImageView(view, nullptr, dispatch);
        }
    }

    TestTarget(const TestTarget&) = delete;
    TestTarget& operator=(const TestTarget&) = delete;
};

struct ReadbackBuffer {
    VmaAllocator allocator{};
    VkBuffer buffer{};
    VmaAllocation allocation{};
    VmaAllocationInfo info{};

    explicit ReadbackBuffer(const Instance& instance) : allocator{instance.GetAllocator()} {
        const VkBufferCreateInfo bi{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = 64,
            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        };
        const VmaAllocationCreateInfo ai{
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        };
        assert(vmaCreateBuffer(allocator, &bi, &ai, &buffer, &allocation, &info) == VK_SUCCESS);
    }

    ~ReadbackBuffer() {
        if (buffer) {
            vmaDestroyBuffer(allocator, buffer, allocation);
        }
    }

    ReadbackBuffer(const ReadbackBuffer&) = delete;
    ReadbackBuffer& operator=(const ReadbackBuffer&) = delete;
};

RenderState MakeRenderState(const TestTarget& target, u32 clear_rgba, bool is_clear) {
    RenderState s{};
    s.width = kW;
    s.height = kH;
    s.num_layers = 1;
    s.num_color_attachments = 1;
    s.color_attachments[0].image_view = target.view;
    s.color_attachments[0].image_layout = vk::ImageLayout::eGeneral;
    s.color_attachments[0].clear_value = is_clear ? MakeClearU32(clear_rgba) : std::array<u32, 4>{};
    s.color_attachments[0].is_clear = is_clear ? 1u : 0u;
    return s;
}

void TransitToGeneral(Scheduler& scheduler, vk::Image img,
                      const vk::detail::DispatchLoaderDynamic& dispatch) {
    scheduler.Record([img, &dispatch](vk::CommandBuffer cmd) {
        const vk::ImageMemoryBarrier2 barrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput |
                             vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentRead |
                             vk::AccessFlagBits2::eColorAttachmentWrite |
                             vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eGeneral,
            .image = img,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        cmd.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier},
                             dispatch);
    });
    scheduler.Finish();
}

void DrawQuadrant(Scheduler& scheduler, u32 quadrant, u32 rgba,
                  const vk::detail::DispatchLoaderDynamic& dispatch) {
    const s32 ox = (quadrant & 1u) ? s32(kW / 2) : 0;
    const s32 oy = (quadrant & 2u) ? s32(kH / 2) : 0;
    const vk::ClearAttachment clear_att{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .colorAttachment = 0,
        .clearValue = MakeColor(rgba),
    };
    const vk::ClearRect rect{
        .rect = {{ox, oy}, {kW / 2, kH / 2}},
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    scheduler.Record([clear_att, rect, &dispatch](vk::CommandBuffer cmd) {
        cmd.clearAttachments(clear_att, rect, dispatch);
    });
}

void PadSegmentInPass(Scheduler& scheduler, size_t target_bytes) {
    // 256-byte payload closures inside the active segment without ending the render pass.
    struct Pad {
        std::array<u32, 64> words{};
        void operator()(vk::CommandBuffer) const {}
    };
    const size_t count = (target_bytes + sizeof(Pad) - 1) / sizeof(Pad);
    for (size_t i = 0; i < count; ++i) {
        scheduler.Record(Pad{});
    }
    scheduler.KickRecording(false);
}

void RecordMemoryBarrier(Scheduler& scheduler, const vk::detail::DispatchLoaderDynamic& dispatch) {
    scheduler.Record([&dispatch](vk::CommandBuffer cmd) {
        const vk::MemoryBarrier2 mb{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask = vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .dstAccessMask =
                vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
        };
        cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &mb}, dispatch);
    });
}

std::array<u32, 4> ReadQuadrantCenters(Scheduler& scheduler, const TestTarget& target,
                                       ReadbackBuffer& rb,
                                       const vk::detail::DispatchLoaderDynamic& dispatch) {
    scheduler.EndRendering();
    scheduler.Record([img = vk::Image(target.image), dst = vk::Buffer(rb.buffer),
                      &dispatch](vk::CommandBuffer cmd) {
        const vk::MemoryBarrier2 pre{
            .srcStageMask = vk::PipelineStageFlagBits2::eAllCommands,
            .srcAccessMask =
                vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask =
                vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eTransferWrite,
        };
        cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &pre}, dispatch);
        const std::array<vk::BufferImageCopy, 4> regions{{
            {0, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {64, 64, 0}, {1, 1, 1}},
            {4, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {192, 64, 0}, {1, 1, 1}},
            {8, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {64, 192, 0}, {1, 1, 1}},
            {12, 0, 0, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {192, 192, 0}, {1, 1, 1}},
        }};
        cmd.copyImageToBuffer(img, vk::ImageLayout::eGeneral, dst, regions, dispatch);
        const vk::MemoryBarrier2 post{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eHost |
                             vk::PipelineStageFlagBits2::eColorAttachmentOutput |
                             vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eHostRead |
                             vk::AccessFlagBits2::eColorAttachmentRead |
                             vk::AccessFlagBits2::eColorAttachmentWrite |
                             vk::AccessFlagBits2::eTransferWrite,
        };
        cmd.pipelineBarrier2({.memoryBarrierCount = 1, .pMemoryBarriers = &post}, dispatch);
    });
    scheduler.Finish();
    std::array<u32, 4> pixels{};
    std::memcpy(pixels.data(), rb.info.pMappedData, sizeof(pixels));
    return pixels;
}

void AssertPixels(const char* label, const std::array<u32, 4>& got,
                  const std::array<u32, 4>& expected) {
    if (got != expected) {
        std::fprintf(stderr,
                     "FAIL [%s]: got {%08x, %08x, %08x, %08x} expected {%08x, %08x, %08x, %08x}\n",
                     label, got[0], got[1], got[2], got[3], expected[0], expected[1], expected[2],
                     expected[3]);
        std::abort();
    }
}

struct MockCarriedScope final : Scheduler::CarriedScope {
    bool allow_resume = true;
    u64 suspend_in_pass = 0;
    u64 suspend_outside = 0;
    u64 resume_in_pass = 0;
    u64 resume_outside = 0;

    void Suspend(bool inside_render_pass) override {
        ++(inside_render_pass ? suspend_in_pass : suspend_outside);
    }
    void Resume(bool inside_render_pass) override {
        ++(inside_render_pass ? resume_in_pass : resume_outside);
    }
    bool CanResumeInPass() const override {
        return allow_resume;
    }
};

void RunMultiSegmentAndFallbackSuite(const Instance& instance,
                                     const vk::detail::DispatchLoaderDynamic& dispatch,
                                     bool async_submit) {
    setenv("BB_VK_RECORD_THREADS", "2", 1);
    setenv("BB_VK_SEGMENT_KB", "1", 1);
    setenv("BB_VK_SUSPEND_RESUME", "1", 1);
    setenv("BB_ASYNC_SUBMIT", async_submit ? "1" : "0", 1);

    Scheduler scheduler(instance, true);
    assert(scheduler.IsSuspendResumeEnabled());

    TestTarget target_a(instance, dispatch);
    TestTarget target_b(instance, dispatch);
    ReadbackBuffer rb(instance);
    TransitToGeneral(scheduler, vk::Image(target_a.image), dispatch);
    TransitToGeneral(scheduler, vk::Image(target_b.image), dispatch);

    // 1. Multi-segment suspend/resume chain (S0 -> S1 -> S2 -> S3) on identical clear RenderState.
    // Even though `is_clear = 1` is passed on every BeginRendering(state_clear),
    // VK_RENDERING_RESUMING_BIT preserves S0's initial clear and Q0/Q1 draws across all segments.
    const RenderState state_a_clear = MakeRenderState(target_a, kRed, true);
    const RenderState state_a_load = MakeRenderState(target_a, kRed, false);
    {
        const auto s0 = scheduler.GetSuspendResumeStats();
        scheduler.BeginRendering(state_a_clear);
        DrawQuadrant(scheduler, 0, kBlue, dispatch);
        PadSegmentInPass(scheduler, 2560); // S0 -> S1

        scheduler.BeginRendering(state_a_clear);
        DrawQuadrant(scheduler, 1, kGreen, dispatch);
        PadSegmentInPass(scheduler, 2560); // S1 -> S2

        // Interleave a non-command RecordHostCopy between MaybeSplit and BeginRendering:
        // host copies must NOT break pending pass suspension.
        std::atomic<bool> copy_ran{false};
        scheduler.BeginRendering(state_a_clear); // S2 resumes from S1
        PadSegmentInPass(scheduler, 2560);       // S2 -> S3
        scheduler.RecordHostCopy([&copy_ran] { copy_ran.store(true, std::memory_order_release); });

        scheduler.BeginRendering(state_a_clear); // S3 resumes from S2
        DrawQuadrant(scheduler, 2, kYellow, dispatch);
        scheduler.EndRendering();
        scheduler.WaitHostCopies();
        assert(copy_ran.load(std::memory_order_acquire));

        const auto s1 = scheduler.GetSuspendResumeStats();
        assert(s1.resumed - s0.resumed >= 3);
        const auto px = ReadQuadrantCenters(scheduler, target_a, rb, dispatch);
        AssertPixels(async_submit ? "chain_async" : "chain_sync", px,
                     {kBlue, kGreen, kYellow, kRed});
    }

    // 2. Fallback on intervening Record (pipelineBarrier2) and RecordData.
    {
        const auto s0 = scheduler.GetSuspendResumeStats();
        scheduler.BeginRendering(state_a_clear);
        DrawQuadrant(scheduler, 0, kBlue, dispatch);
        PadSegmentInPass(scheduler, 2560); // S0 -> S1 (pending_suspend active)
        RecordMemoryBarrier(scheduler, dispatch);
        scheduler.BeginRendering(state_a_clear); // falls back to eStore+eLoad (must not re-clear!)
        DrawQuadrant(scheduler, 1, kGreen, dispatch);
        PadSegmentInPass(scheduler, 2560); // S1 -> S2 (pending_suspend active)
        std::array<u32, 4> dummy{1, 2, 3, 4};
        (void)scheduler.RecordData<u32>(dummy);
        scheduler.BeginRendering(state_a_clear);
        DrawQuadrant(scheduler, 2, kYellow, dispatch);
        scheduler.EndRendering();

        const auto s1 = scheduler.GetSuspendResumeStats();
        assert(s1.fallback_barrier_or_cmd - s0.fallback_barrier_or_cmd >= 2);
        const auto px = ReadQuadrantCenters(scheduler, target_a, rb, dispatch);
        AssertPixels(async_submit ? "barrier_fallback_async" : "barrier_fallback_sync", px,
                     {kBlue, kGreen, kYellow, kRed});
    }

    // 3. Fallback on RenderState mismatch.
    {
        const RenderState state_b_clear = MakeRenderState(target_b, kCyan, true);
        const auto s0 = scheduler.GetSuspendResumeStats();
        scheduler.BeginRendering(state_a_clear);
        DrawQuadrant(scheduler, 0, kBlue, dispatch);
        PadSegmentInPass(scheduler, 2560); // S0 -> S1 (pending_suspend active on target_a)
        scheduler.BeginRendering(state_b_clear); // target changed -> fallback_state
        DrawQuadrant(scheduler, 0, kMagenta, dispatch);
        scheduler.EndRendering();

        const auto s1 = scheduler.GetSuspendResumeStats();
        assert(s1.fallback_state - s0.fallback_state >= 1);
        AssertPixels("state_fallback_A", ReadQuadrantCenters(scheduler, target_a, rb, dispatch),
                     {kBlue, kRed, kRed, kRed});
        AssertPixels("state_fallback_B", ReadQuadrantCenters(scheduler, target_b, rb, dispatch),
                     {kMagenta, kCyan, kCyan, kCyan});
    }

    // 4. Fallback on Flush() and CommandBuffer() (EnterDirectMode).
    {
        const auto s0 = scheduler.GetSuspendResumeStats();
        scheduler.BeginRendering(state_a_clear);
        DrawQuadrant(scheduler, 0, kBlue, dispatch);
        PadSegmentInPass(scheduler, 2560); // S0 -> S1 (pending_suspend active)
        scheduler.Flush();

        scheduler.BeginRendering(state_a_load);
        DrawQuadrant(scheduler, 1, kGreen, dispatch);
        PadSegmentInPass(scheduler, 2560); // S0 -> S1 (pending_suspend active)
        (void)scheduler.CommandBuffer(); // EnterDirectMode fallback
        scheduler.BeginRendering(state_a_load);
        DrawQuadrant(scheduler, 2, kYellow, dispatch);
        scheduler.EndRendering();
        scheduler.KickRecording();

        const auto s1 = scheduler.GetSuspendResumeStats();
        assert(s1.fallback_submit_or_direct - s0.fallback_submit_or_direct >= 2);
        AssertPixels(async_submit ? "submit_direct_async" : "submit_direct_sync",
                     ReadQuadrantCenters(scheduler, target_a, rb, dispatch),
                     {kBlue, kGreen, kYellow, kRed});
    }

    // 5. CarriedScope interaction: skip Suspend(false)/Resume(false) on clean resume, invoke on
    // !CanResumeInPass() fallback.
    {
        MockCarriedScope scope{};
        scheduler.SetCarriedScope(&scope);
        const auto s0 = scheduler.GetSuspendResumeStats();

        scheduler.BeginRendering(state_a_clear);
        DrawQuadrant(scheduler, 0, kBlue, dispatch);
        scope.allow_resume = true;
        PadSegmentInPass(scheduler, 2560); // clean suspend -> resume_outside stays 0
        scheduler.BeginRendering(state_a_clear);
        DrawQuadrant(scheduler, 1, kGreen, dispatch);
        assert(scope.suspend_outside == 1); // only from initial BeginRendering(false)
        assert(scope.resume_outside == 0);  // skipped across clean suspend/resume!

        scope.allow_resume = false;
        PadSegmentInPass(scheduler, 2560); // forces fallback_query_reset on next BeginRendering
        scheduler.BeginRendering(state_a_clear);
        DrawQuadrant(scheduler, 2, kYellow, dispatch);
        scheduler.EndRendering();

        const auto s1 = scheduler.GetSuspendResumeStats();
        assert(s1.resumed - s0.resumed >= 1);
        assert(s1.fallback_query_reset - s0.fallback_query_reset >= 1);
        assert(scope.suspend_outside >= 2); // ran outside-pass maintenance before eLoad reopen
        scheduler.SetCarriedScope(nullptr);

        AssertPixels(async_submit ? "scope_async" : "scope_sync",
                     ReadQuadrantCenters(scheduler, target_a, rb, dispatch),
                     {kBlue, kGreen, kYellow, kRed});
    }

    std::printf("scheduler-suspend-test: async_submit=%d multi-segment & fallback PASS\n",
                async_submit ? 1 : 0);
}

void RunChunkCapSuite(const Instance& instance,
                      const vk::detail::DispatchLoaderDynamic& dispatch) {
    // Set split_bytes = 2 MiB (2 * split_bytes = 4 MiB) so an open pass retires > MaxHeldChunks (4
    // chunks = 512 KiB) without hitting MaybeSplit(), forcing chunk-cap release.
    setenv("BB_VK_RECORD_THREADS", "2", 1);
    setenv("BB_VK_SEGMENT_KB", "2048", 1);
    setenv("BB_VK_SUSPEND_RESUME", "1", 1);
    setenv("BB_ASYNC_SUBMIT", "1", 1);

    Scheduler scheduler(instance, true);
    TestTarget target(instance, dispatch);
    ReadbackBuffer rb(instance);
    TransitToGeneral(scheduler, vk::Image(target.image), dispatch);

    const RenderState state_clear = MakeRenderState(target, kRed, true);
    scheduler.BeginRendering(state_clear);
    DrawQuadrant(scheduler, 0, kBlue, dispatch);

    struct BigPad {
        std::array<u32, 8192> words{}; // 32 KiB (< 128 KiB RecordChunk::Capacity)
        void operator()(vk::CommandBuffer) const {}
    };
    for (u32 i = 0; i < 18; ++i) {
        scheduler.Record(BigPad{});
    }
    DrawQuadrant(scheduler, 1, kGreen, dispatch);
    scheduler.EndRendering();

    const auto stats = scheduler.GetSuspendResumeStats();
    assert(stats.fallback_chunk_cap >= 1);
    AssertPixels("chunk_cap", ReadQuadrantCenters(scheduler, target, rb, dispatch),
                 {kBlue, kGreen, kRed, kRed});
    std::printf("scheduler-suspend-test: MaxHeldChunks (%llu caps) PASS\n",
                (unsigned long long)stats.fallback_chunk_cap);
}

void RunDisabledOptOutSuite(const Instance& instance,
                            const vk::detail::DispatchLoaderDynamic& dispatch) {
    setenv("BB_VK_RECORD_THREADS", "2", 1);
    setenv("BB_VK_SEGMENT_KB", "1", 1);
    setenv("BB_VK_SUSPEND_RESUME", "0", 1);
    setenv("BB_ASYNC_SUBMIT", "1", 1);

    Scheduler scheduler(instance, true);
    assert(!scheduler.IsSuspendResumeEnabled());
    TestTarget target(instance, dispatch);
    ReadbackBuffer rb(instance);
    TransitToGeneral(scheduler, vk::Image(target.image), dispatch);

    const RenderState state_clear = MakeRenderState(target, kRed, true);
    scheduler.BeginRendering(state_clear);
    DrawQuadrant(scheduler, 0, kBlue, dispatch);
    PadSegmentInPass(scheduler, 2560);
    scheduler.BeginRendering(state_clear);
    DrawQuadrant(scheduler, 1, kGreen, dispatch);
    scheduler.EndRendering();

    const auto stats = scheduler.GetSuspendResumeStats();
    assert(stats.resumed == 0);
    AssertPixels("opt_out_disabled", ReadQuadrantCenters(scheduler, target, rb, dispatch),
                 {kBlue, kGreen, kRed, kRed});
    std::printf("scheduler-suspend-test: BB_VK_SUSPEND_RESUME=0 opt-out PASS\n");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    Instance instance(0, EmulatorSettings.IsVkValidationEnabled());
    static vk::detail::DynamicLoader loader;
    vk::detail::DispatchLoaderDynamic dispatch;
    dispatch.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
    dispatch.init(instance.GetInstance());
    dispatch.init(instance.GetDevice());

    RunMultiSegmentAndFallbackSuite(instance, dispatch, true);
    RunMultiSegmentAndFallbackSuite(instance, dispatch, false);
    RunChunkCapSuite(instance, dispatch);
    RunDisabledOptOutSuite(instance, dispatch);

    std::puts("ALL scheduler-suspend-test CHECKS PASSED.");
    return 0;
}
