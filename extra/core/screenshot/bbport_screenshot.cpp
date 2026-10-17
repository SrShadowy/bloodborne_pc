// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_screenshot.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

#include <vk_mem_alloc.h>
#include <SDL3/SDL_surface.h>

#include "bbport_overlay.h"
#include "bbport_settings.h"
#include "common/path_util.h"
#include "render/scheduler.h"
#include "vulkan/instance.h"

namespace BbScreenshot {

bool RecordCapture(
    const Vulkan::Instance& instance,
    Vulkan::Scheduler& present_scheduler,
    vk::CommandBuffer cmdbuf,
    vk::Image target,
    vk::Extent2D extent,
    vk::Format surface_format,
    bool overlay,
    const vk::ImageSubresourceRange& color_range
) {
    if (!BbSettings::Get().request_screenshot.exchange(false, std::memory_order_relaxed)) {
        return false;
    }

    const vk::ImageMemoryBarrier2 to_src{
        .srcStageMask = vk::PipelineStageFlagBits2::eAllTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .image = target,
        .subresourceRange = color_range,
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &to_src});

    const VkDeviceSize buffer_size = VkDeviceSize(extent.width) * extent.height * 4;
    const VkBufferCreateInfo buffer_ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = buffer_size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    const VmaAllocationCreateInfo alloc_ci{
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
    };
    VkBuffer buffer{};
    VmaAllocation allocation{};
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(instance.GetAllocator(), &buffer_ci, &alloc_ci, &buffer, &allocation, &info) == VK_SUCCESS) {
        const vk::BufferImageCopy copy_region{
            .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            .imageExtent = {extent.width, extent.height, 1},
        };
        cmdbuf.copyImageToBuffer(target, vk::ImageLayout::eTransferSrcOptimal, buffer, copy_region);

        present_scheduler.DeferPriorityOperation(
            [allocator = instance.GetAllocator(), buffer, allocation, info, w = extent.width, h = extent.height,
             format = surface_format] {
                vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
                const auto dir = Common::FS::GetUserPath(Common::FS::PathType::ScreenshotsDir);
                const auto now = std::chrono::system_clock::now();
                const auto time = std::chrono::system_clock::to_time_t(now);
                std::tm tm{};
                localtime_r(&time, &tm);
                char name_buf[128];
                std::strftime(name_buf, sizeof(name_buf), "BBPort_%Y-%m-%d_%H-%M-%S.png", &tm);
                const std::string file_path = (dir / name_buf).string();

                const bool bgra = (format == vk::Format::eB8G8R8A8Unorm || format == vk::Format::eB8G8R8A8Srgb);
                const SDL_PixelFormat sdl_fmt = bgra ? SDL_PIXELFORMAT_BGRA32 : SDL_PIXELFORMAT_RGBA32;
                SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, sdl_fmt, info.pMappedData, w * 4);
                if (surface) {
                    if (SDL_SavePNG(surface, file_path.c_str())) {
                        std::printf("Photo Mode: screenshot saved to %s\n", file_path.c_str());
                        char toast[256];
                        std::snprintf(toast, sizeof(toast), "Screenshot salva: %s", name_buf);
                        BbOverlay::SetBanner(toast, 3.0f);
                    } else {
                        std::printf("Photo Mode: failed to save screenshot (%s)\n", SDL_GetError());
                    }
                    SDL_DestroySurface(surface);
                }
                vmaDestroyBuffer(allocator, buffer, allocation);
            });
    }

    const vk::ImageMemoryBarrier2 after_read{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
        .dstStageMask = overlay ? vk::PipelineStageFlagBits2::eColorAttachmentOutput
                                : vk::PipelineStageFlagBits2::eNone,
        .dstAccessMask = overlay ? vk::AccessFlagBits2::eColorAttachmentRead |
                                       vk::AccessFlagBits2::eColorAttachmentWrite
                                 : vk::AccessFlagBits2::eNone,
        .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
        .newLayout = overlay ? vk::ImageLayout::eColorAttachmentOptimal
                             : vk::ImageLayout::ePresentSrcKHR,
        .image = target,
        .subresourceRange = color_range,
    };
    cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &after_read});
    return true;
}

} // namespace BbScreenshot
