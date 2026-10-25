// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: cinematic shader precompilation splash screen implementation.
#include "bbport_shader_splash.h"
#include "extra/core/shader_precompile/bbport_shader_discovery.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <vector>

namespace BbShaderSplash {

namespace {

constexpr int WIN_WIDTH = 1280;
constexpr int WIN_HEIGHT = 720;
constexpr float BAR_WIDTH = 640.0f;
constexpr float BAR_HEIGHT = 22.0f;
constexpr float BAR_Y = 65.0f;

SDL_Texture* LoadSplashTexture(SDL_Renderer* renderer, const std::string& preferred_path) {
    std::vector<std::filesystem::path> candidates;
    if (!preferred_path.empty()) {
        candidates.emplace_back(preferred_path);
    }

    const char* app_dir = std::getenv("APPDIR");
    const char* port_dir = std::getenv("BB_PORT_DIR");

    if (app_dir && *app_dir) {
        candidates.push_back(std::filesystem::path{app_dir} / "packaging" / "splash_bloodborne.png");
    }
    if (port_dir && *port_dir) {
        candidates.push_back(std::filesystem::path{port_dir} / "packaging" / "splash_bloodborne.png");
    }

    candidates.emplace_back("packaging/splash_bloodborne.png");
    candidates.emplace_back("../packaging/splash_bloodborne.png");

    std::error_code ec;
    for (const auto& p : candidates) {
        if (p.empty() || !std::filesystem::exists(p, ec)) continue;
        SDL_Surface* surf = SDL_LoadPNG(p.string().c_str());
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            const int w = surf->w;
            const int h = surf->h;
            SDL_DestroySurface(surf);
            if (tex) {
                std::printf("BbShaderSplash: Loaded splash artwork from %s (%dx%d)\n",
                            p.string().c_str(), w, h);
                return tex;
            }
        }
    }
    std::printf("BbShaderSplash: Warning: No splash background image found.\n");
    return nullptr;
}

void DrawMonsterHunterProgressBar(SDL_Renderer* renderer, float progress, uint32_t cur, uint32_t tot) {
    const float bar_x = (WIN_WIDTH - BAR_WIDTH) * 0.5f;

    // 1. Dark vignette bar backdrop
    SDL_FRect vignette{bar_x - 30.0f, BAR_Y - 45.0f, BAR_WIDTH + 60.0f, BAR_HEIGHT + 75.0f};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 185);
    SDL_RenderFillRect(renderer, &vignette);

    // 2. Bar Outer Frame (Antique Bronze / Dark Gold)
    SDL_FRect outer_frame{bar_x - 3.0f, BAR_Y - 3.0f, BAR_WIDTH + 6.0f, BAR_HEIGHT + 6.0f};
    SDL_SetRenderDrawColor(renderer, 112, 84, 35, 255);
    SDL_RenderFillRect(renderer, &outer_frame);

    // 3. Bar Track (Dark Charcoal)
    SDL_FRect track{bar_x, BAR_Y, BAR_WIDTH, BAR_HEIGHT};
    SDL_SetRenderDrawColor(renderer, 16, 16, 18, 255);
    SDL_RenderFillRect(renderer, &track);

    // 4. Progress Fill (Radiant Hunter Gold)
    const float clamped = std::clamp(progress, 0.0f, 1.0f);
    if (clamped > 0.001f) {
        SDL_FRect fill{bar_x, BAR_Y, BAR_WIDTH * clamped, BAR_HEIGHT};
        SDL_SetRenderDrawColor(renderer, 229, 184, 59, 255);
        SDL_RenderFillRect(renderer, &fill);

        // Top highlight shimmer line
        SDL_FRect shimmer{bar_x, BAR_Y, BAR_WIDTH * clamped, 3.0f};
        SDL_SetRenderDrawColor(renderer, 255, 225, 120, 255);
        SDL_RenderFillRect(renderer, &shimmer);
    }

    // 5. Inner Edge Stroke
    SDL_SetRenderDrawColor(renderer, 160, 125, 60, 255);
    SDL_RenderRect(renderer, &track);

    // 6. Centered Title Text ("Compiling shaders...")
    const char* title_text = "Compiling shaders...";
    SDL_SetRenderScale(renderer, 1.75f, 1.75f);
    const float title_x = (WIN_WIDTH / 1.75f - 180.0f) * 0.5f;
    const float title_y = (BAR_Y - 26.0f) / 1.75f;
    SDL_SetRenderDrawColor(renderer, 245, 235, 215, 255);
    SDL_RenderDebugText(renderer, title_x, title_y, title_text);

    // 7. Centered Counter Text ("[ X / Y ]  ( % )")
    SDL_SetRenderScale(renderer, 1.35f, 1.35f);
    char counter_str[64];
    const uint32_t pct = static_cast<uint32_t>(clamped * 100.0f);
    std::snprintf(counter_str, sizeof(counter_str), "[ %u / %u ]  ( %u%% )", cur, tot, pct);
    const float count_x = (WIN_WIDTH / 1.35f - 160.0f) * 0.5f;
    const float count_y = (BAR_Y + BAR_HEIGHT + 8.0f) / 1.35f;
    SDL_SetRenderDrawColor(renderer, 215, 195, 150, 255);
    SDL_RenderDebugText(renderer, count_x, count_y, counter_str);

    SDL_SetRenderScale(renderer, 1.0f, 1.0f);
}

} // namespace

bool Run(const SplashOptions& options, std::function<bool()> compile_task) {
    if (options.headless || !SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("Running shader reconstruction in headless mode...\n");
        return compile_task ? compile_task() : false;
    }

    SDL_Window* window = SDL_CreateWindow("Bloodborne — Compilando Shaders", WIN_WIDTH, WIN_HEIGHT,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::printf("Failed to create SDL window. Falling back to headless mode.\n");
        return compile_task ? compile_task() : false;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_DestroyWindow(window);
        return compile_task ? compile_task() : false;
    }

    SDL_SetRenderLogicalPresentation(renderer, WIN_WIDTH, WIN_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    SDL_Texture* bg_tex = LoadSplashTexture(renderer, options.splash_image_path);

    std::atomic<bool> done{false};
    std::atomic<bool> success{false};
    std::jthread worker([&] {
        success = compile_task ? compile_task() : false;
        done = true;
    });

    float display_prog = 0.0f;
    uint32_t cur = 0, tot = options.target_pipelines;
    bool running = true;
    auto last_time = std::chrono::steady_clock::now();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        uint32_t live_cur = 0, live_tot = 0;
        BbShaderDiscovery::GetProgress(live_cur, live_tot);
        if (live_tot > 0) {
            tot = std::max(tot, live_tot);
            cur = std::max(cur, live_cur);
        }

        float target_prog = tot > 0 ? (static_cast<float>(cur) / static_cast<float>(tot)) : 0.0f;
        if (done) {
            cur = tot;
            target_prog = 1.0f;
        }

        display_prog += (target_prog - display_prog) * 0.15f;
        if (done && display_prog >= 0.99f) {
            display_prog = 1.0f;
            running = false;
        }

        // Render Frame
        SDL_SetRenderDrawColor(renderer, 10, 10, 14, 255);
        SDL_RenderClear(renderer);

        if (bg_tex) {
            SDL_RenderTexture(renderer, bg_tex, nullptr, nullptr);
        }

        DrawMonsterHunterProgressBar(renderer, display_prog, cur, tot);
        SDL_RenderPresent(renderer);

        // Cap to ~60 FPS
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        if (elapsed < 16) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16 - elapsed));
        }
        last_time = std::chrono::steady_clock::now();
    }

    if (done && display_prog >= 0.99f) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    if (bg_tex) SDL_DestroyTexture(bg_tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return success.load();
}

} // namespace BbShaderSplash
