// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "upscale/frame_gen_common.h"
#include "upscale/fg_profile.h"
#include "upscale/vk_fsr_amd.h"

namespace Vulkan {

bool FrameGeneration::EnsureAmd(u32 render_width, u32 render_height, u32 width, u32 height,
                                vk::Format format) {
    if (context || dlss_desc) {
        Destroy(); // the other provider's (the GPU is done with it)
    }
    bool created = false;
    static std::string problem; // read by the menu
    std::string why;
    if (!amd->Ensure(render_width, render_height, width, height, format, created, why)) {
        problem = "AMD's DLL: " + why;
        Fail(BbSettings::FrameGenFsrAmd, problem.c_str());
        return false;
    }
    context_render_w = render_width;
    context_render_h = render_height;
    context_w = width;
    context_h = height;
    context_format = format;
    if (created) {
        reset_next = true;
    }
    return true;
}

void FrameGeneration::RecordAmd(vk::Image color, u64 id, bool reset, Target& target) {
    FgAmd::Frame f{};
    using Image = Fsr4Upscaler::Image;
    f.color = Image{.image = color, .width = scene.width, .height = scene.height,
                    .format = scene.color_format};
    if (hudless_ready && !BbToggle::Disabled(1u << 30)) {
        f.hudless = Image{.image = vk::Image(In().hudless), .width = scene.width,
                          .height = scene.height, .format = scene.color_format};
    }
    hudless_ready = false;
    f.depth = Image{.image = vk::Image(In().depth_copy), .width = scene.render_width,
                    .height = scene.render_height, .format = scene.depth_format};
    f.motion = Image{.image = scene.motion, .width = scene.render_width,
                     .height = scene.render_height, .format = vk::Format::eR16G16Sfloat};
    f.output = Image{.image = vk::Image(target.image), .width = target.width,
                     .height = target.height, .format = target.format};
    f.jitter[0] = scene.jitter_x;
    f.jitter[1] = scene.jitter_y;
    f.frame_ms = scene.frame_ms;
    f.near_plane = scene.camera_near;
    f.far_plane = scene.camera_far;
    f.vertical_fov = scene.vertical_fov;
    std::copy_n(scene.camera_position, 3, f.camera_position);
    std::copy_n(scene.camera_up, 3, f.camera_up);
    std::copy_n(scene.camera_right, 3, f.camera_right);
    std::copy_n(scene.camera_forward, 3, f.camera_forward);
    f.reset = reset;
    f.frame_id = id;
    scheduler.EndRendering();
    scheduler.Record([this, f](vk::CommandBuffer cmdbuf) mutable {
        const vk::Image color = f.color.image, out = f.output.image;
        const auto all = vk::PipelineStageFlagBits2::eAllCommands;
        ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryWrite,
                     vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
        ImageBarrier(cmdbuf, out, all, vk::AccessFlagBits2::eMemoryRead,
                     vk::PipelineStageFlagBits2::eComputeShader,
                     vk::AccessFlagBits2::eShaderStorageWrite, vk::ImageLayout::eUndefined);
        f.cmdbuf = cmdbuf;
        FgProfile::Begin(instance, cmdbuf);
        std::string problem;
        const bool ok = amd->Record(f, problem);
        FgProfile::End(cmdbuf);
        if (!ok) {
            static int told = 0;
            if (told++ < 4) {
                std::printf("Frame generation: AMD's DLL: %s\n", problem.c_str());
            }
        }
        // AMD's passes leave their own barriers behind; everything after waits for them.
        ImageBarrier(cmdbuf, color, all, vk::AccessFlagBits2::eMemoryRead, all,
                     vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite);
        ImageBarrier(cmdbuf, out, all, vk::AccessFlagBits2::eMemoryWrite, all,
                     vk::AccessFlagBits2::eMemoryRead);
        if (!ok) {
            CopyFrame(cmdbuf, color, out, f.output.width, f.output.height);
        }
    });
}

} // namespace Vulkan
