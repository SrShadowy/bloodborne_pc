// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "upscale/frame_gen_common.h"

namespace Vulkan {

bool FrameGeneration::EnsureDlss(u32 render_width, u32 render_height, u32 width, u32 height,
                                 vk::Format format) {
    const Dlss::FrameGenDesc desc{width, height, format, render_width, render_height};
    if (dlss_desc && *dlss_desc == desc) {
        return true;
    }
    Destroy(); // the FSR context or the previous feature (the GPU is done with them)
    dlss_desc = desc;
    dlss_create_pending = true; // made in the next recorded generation (it needs a command buffer)
    context_render_w = render_width;
    context_render_h = render_height;
    context_w = width;
    context_h = height;
    context_format = format;
    reset_next = true;
    std::printf("Frame generation: DLSS %ux%u from %ux%u scenes (%s), %ux\n", width, height,
                render_width, render_height, vk::to_string(format).c_str(), WantedCount() + 1);
    return true;
}

void FrameGeneration::RecordDlss(vk::Image color, u64 id, bool reset, u32 count) {
    (void)id; // DLSS keeps its own history and measures the time itself
    const Dlss::FrameGenDesc desc = *dlss_desc;
    const bool create = dlss_create_pending;
    dlss_create_pending = false;
    const vk::ImageView color_view_handle = ColorView(color, scene.color_format);
    // BB_DLSSG_HUDLESS=0: no hudless frame (diagnostics, as toggle bit 30).
    static const bool hudless_wanted = !bb_env_off("BB_DLSSG_HUDLESS");
    const bool with_hudless =
        hudless_wanted && hudless_ready && In().hudless && !BbToggle::Disabled(1u << 30);
    hudless_ready = false;
    const auto resource = [](vk::Image image, vk::ImageView view, vk::Format format,
                             vk::ImageAspectFlags aspect, u32 w, u32 h) {
        return Dlss::Resource{image, view, format, aspect, w, h};
    };
    Dlss::FrameGenFrame frame{
        .backbuffer = resource(color, color_view_handle, scene.color_format,
                               vk::ImageAspectFlagBits::eColor, scene.width, scene.height),
        .depth = resource(vk::Image(In().depth_copy), *In().depth_copy_view, scene.depth_format,
                          vk::ImageAspectFlagBits::eDepth, scene.render_width, scene.render_height),
        .motion = resource(scene.motion, scene.motion_view, vk::Format::eR16G16Sfloat,
                           vk::ImageAspectFlagBits::eColor, scene.render_width, scene.render_height),
        .output = {},
        .hudless = with_hudless ? resource(vk::Image(In().hudless), *In().hudless_view, scene.color_format,
                                           vk::ImageAspectFlagBits::eColor, scene.width, scene.height)
                                : Dlss::Resource{},
        .jitter_x = scene.jitter_x,
        .jitter_y = scene.jitter_y,
        .camera_near = scene.camera_near,
        .camera_far = scene.camera_far,
        .camera_fov = scene.vertical_fov,
        .camera_position = {scene.camera_position[0], scene.camera_position[1], scene.camera_position[2]},
        .camera_up = {scene.camera_up[0], scene.camera_up[1], scene.camera_up[2]},
        .camera_right = {scene.camera_right[0], scene.camera_right[1], scene.camera_right[2]},
        .camera_forward = {scene.camera_forward[0], scene.camera_forward[1], scene.camera_forward[2]},
        .depth_inverted = false, // the game's depth is 0 at the near plane (CameraMotion::Near)
        .has_matrices = scene.has_matrices,
        .view_to_clip = scene.view_to_clip,
        .clip_to_prev_clip = scene.clip_to_prev_clip,
        .prev_clip_to_clip = scene.prev_clip_to_clip,
        .reset = reset,
        .count = count,
        .index = 1,
    };
    std::array<Dlss::Resource, MaxGenerated> outputs{};
    for (u32 i = 0; i < count; ++i) {
        outputs[i] = resource(vk::Image(targets[i].image), *targets[i].view, scene.color_format,
                              vk::ImageAspectFlagBits::eColor, scene.width, scene.height);
    }
    const vk::Image hudless_image = with_hudless ? vk::Image(In().hudless) : vk::Image{};

    scheduler.EndRendering();
    scheduler.Record([this, desc, create, frame, outputs, count, color, hudless_image,
                      depth = vk::Image(In().depth_copy), motion = scene.motion, w = scene.width,
                      h = scene.height](vk::CommandBuffer cmdbuf) mutable {
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        const auto read = vk::AccessFlagBits2::eShaderRead;
        const auto compute = vk::PipelineStageFlagBits2::eComputeShader;
        // The UI drew into the frame; the display pass reads it after. The depth copy and the
        // motion vectors were written earlier in the stream. All in General layout, as NGX
        // takes them.
        ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryWrite, compute, read);
        if (hudless_image) {
            ImageBarrier(cmdbuf, hudless_image, all, vk::AccessFlagBits2::eMemoryWrite, compute, read);
        }
        ImageBarrier(cmdbuf, depth, all, vk::AccessFlagBits2::eMemoryWrite, compute, read,
                     vk::ImageLayout::eGeneral, DepthRange);
        ImageBarrier(cmdbuf, motion, all, vk::AccessFlagBits2::eMemoryWrite, compute, read);
        // The generated frames start as the real one: DLSS-G leaves its output untouched when it
        // skips a frame (a scene change it detected, its own pacing), and an output entered as
        // Undefined then went to the screen as garbage: black frames flickering in while the
        // camera faced the level, none while it faced the sky (RTX 5060, Windows, 2026-10-27).
        // BB_DLSSG_PREFILL=0: as before.
        static const bool prefill = !bb_env_off("BB_DLSSG_PREFILL");
        for (u32 i = 0; i < count; ++i) {
            if (prefill) {
                CopyFrame(cmdbuf, color, outputs[i].image, w, h);
                ImageBarrier(cmdbuf, outputs[i].image, all, vk::AccessFlagBits2::eMemoryRead,
                             compute, vk::AccessFlagBits2::eShaderStorageWrite);
            } else {
                ImageBarrier(cmdbuf, outputs[i].image, all, vk::AccessFlagBits2::eMemoryRead,
                             compute, vk::AccessFlagBits2::eShaderStorageWrite,
                             vk::ImageLayout::eUndefined);
            }
        }
        Dlss* dlss = Dlss::Get();
        bool ok = dlss != nullptr;
        if (ok && create) {
            // The previous feature was released by Destroy (the GPU done with it).
            ok = dlss->CreateFrameGen(cmdbuf, desc);
            if (!ok) {
                Fail(BbSettings::FrameGenDlss, "DLSS Frame Generation could not be created");
            }
        }
        for (u32 index = 1; ok && index <= count; ++index) {
            frame.output = outputs[index - 1];
            frame.index = index;
            ok = dlss->EvaluateFrameGen(cmdbuf, frame);
            if (!ok) {
                Fail(BbSettings::FrameGenDlss, "DLSS Frame Generation failed (see the log)");
            }
        }
        ImageBarrier(cmdbuf, color, compute, read, all,
                     vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        if (hudless_image) {
            ImageBarrier(cmdbuf, hudless_image, compute, read, all,
                         vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        }
        ImageBarrier(cmdbuf, depth, compute, read, all,
                     vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
                     vk::ImageLayout::eGeneral, DepthRange);
        ImageBarrier(cmdbuf, motion, compute, read, all,
                     vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        for (u32 i = 0; i < count; ++i) {
            ImageBarrier(cmdbuf, outputs[i].image, compute, vk::AccessFlagBits2::eShaderStorageWrite,
                         all, vk::AccessFlagBits2::eMemoryRead);
            if (!ok) {
                // Handed out as ready already: the real frame again, not garbage.
                CopyFrame(cmdbuf, color, outputs[i].image, w, h);
            }
        }
    });
}

} // namespace Vulkan
