// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/video_backend/bbport_video_backend.h"

#include <cstdio>
#include <cstdlib>
#include <SDL3/SDL.h>

namespace BbVideoBackend {

namespace {
// A zero-extent surface is reported once per stretch (see the header).
bool told_zero = false;
} // namespace

void ApplyHint() {
    const char* backend = std::getenv("BB_VIDEO_BACKEND");
    if (backend && (*backend == 'x' || *backend == 'w')) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, backend);
    }
}

void TraceRequest() {
    static bool printed = false;
    if (printed) {
        return;
    }
    printed = true;
    const char* backend = std::getenv("BB_VIDEO_BACKEND");
    const char* driver = std::getenv("SDL_VIDEODRIVER");
    std::printf("Window: backend %s requested (SDL_VIDEODRIVER=%s)\n",
                backend ? backend : "auto", driver ? driver : "(auto)");
    std::fflush(stdout);
}

void TraceCreated(int width, int height) {
    const char* driver = SDL_GetCurrentVideoDriver();
    const char* backend = std::getenv("BB_VIDEO_BACKEND");
    std::printf("Window %dx%d on %s (BB_VIDEO_BACKEND=%s)\n", width, height,
                driver ? driver : "?", backend ? backend : "auto");
    std::fflush(stdout);
}

void SurfaceZeroExtent(bool zero) {
    if (!zero) {
        told_zero = false;
        return;
    }
    if (told_zero) {
        return;
    }
    told_zero = true;
    std::printf("Swapchain: surface 0x0 — the window is not visible (minimised, a hidden "
                "special workspace or not mapped by the compositor)\n");
    std::fflush(stdout);
}

} // namespace BbVideoBackend
