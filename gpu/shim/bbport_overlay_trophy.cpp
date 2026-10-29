// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_overlay.h"
#include "bbport_overlay_internal.h"
#include "shim/bb_env.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h"

namespace BbOverlay {

namespace {

struct TrophyToast {
    char grade;
    std::string name;
    float elapsed = 0.0f;
    std::filesystem::path icon;
};

std::unordered_map<std::string, ImTextureData*> trophy_icons;
std::mutex trophy_mutex;
std::deque<TrophyToast> trophy_queue;
std::atomic<bool> trophy_active{false};
constexpr float TrophySeconds = 6.0f, TrophySlide = 0.4f;

ImTextureData* TrophyIcon(const std::filesystem::path& path) {
    if (path.empty()) return nullptr;
    const auto [it, added] = trophy_icons.try_emplace(path.string(), nullptr);
    if (!added) return it->second;
#if SDL_VERSION_ATLEAST(3, 4, 0)
    const std::u8string utf8 = path.u8string();
    const char* name = reinterpret_cast<const char*>(utf8.c_str());
    SDL_Surface* loaded = SDL_LoadPNG(name);
    SDL_Surface* rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
    SDL_DestroySurface(loaded);
    if (!rgba) return nullptr;
    auto* tex = IM_NEW(ImTextureData)();
    tex->Create(ImTextureFormat_RGBA32, rgba->w, rgba->h);
    for (int y = 0; y < rgba->h; ++y) {
        std::memcpy(tex->GetPixelsAt(0, y), static_cast<const unsigned char*>(rgba->pixels) + y * rgba->pitch,
                    std::size_t(rgba->w) * 4);
    }
    SDL_DestroySurface(rgba);
    ImGui::RegisterUserTexture(tex);
    it->second = tex;
#endif
    return it->second;
}

} // namespace

void ShowTrophy(char grade, const std::string& name, const std::filesystem::path& icon, float delay) {
    std::scoped_lock lock{trophy_mutex};
    trophy_queue.push_back({grade, name, -delay, icon});
    trophy_active = true;
}

bool TrophyActive() {
    return trophy_active.load(std::memory_order_relaxed);
}

void TrophyWindow(float dt) {
    TrophyToast toast;
    {
        std::scoped_lock lock{trophy_mutex};
        if (trophy_queue.empty()) {
            trophy_active = false;
            return;
        }
        auto& front = trophy_queue.front();
        front.elapsed += dt;
        if (front.elapsed > TrophySeconds) {
            trophy_queue.pop_front();
            trophy_active = !trophy_queue.empty();
            return;
        }
        toast = front;
    }
    if (toast.elapsed < 0.0f) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float s = detail::base_scale;
    const ImVec2 size{380.0f * s, 76.0f * s};
    const float slide = std::min(toast.elapsed / TrophySlide, 1.0f);
    const float eased = 1.0f - (1.0f - slide) * (1.0f - slide);
    const float x = vp->WorkPos.x + (vp->WorkSize.x - size.x) * 0.5f;
    const float y = vp->WorkPos.y - size.y + (size.y + 24.0f * s) * eased;
    const float alpha = std::clamp(std::min(slide, TrophySeconds - toast.elapsed), 0.0f, 1.0f);
    auto* draw = ImGui::GetForegroundDrawList();
    const auto a = [alpha](int r, int g, int b, int base) { return IM_COL32(r, g, b, int(base * alpha)); };

    draw->AddRectFilled({x, y}, {x + size.x, y + size.y}, a(12, 10, 8, 220), 8.0f * s);
    draw->AddRect({x, y}, {x + size.x, y + size.y}, a(150, 130, 100, 200), 8.0f * s, 0, 1.5f * s);

    const ImU32 badge = toast.grade == 'P' ? a(200, 215, 235, 255)
                      : toast.grade == 'G' ? a(222, 180, 60, 255)
                      : toast.grade == 'S' ? a(185, 190, 196, 255)
                      : toast.grade == 'B' ? a(176, 112, 64, 255) : a(120, 120, 120, 255);
    const ImVec2 centre{x + 38.0f * s, y + size.y * 0.5f};
    const char letter[2] = {toast.grade == '?' ? 'T' : toast.grade, 0};
    const ImVec2 letter_size = ImGui::CalcTextSize(letter);

    if (ImTextureData* icon = TrophyIcon(toast.icon)) {
        const float half = 26.0f * s;
        draw->AddImageRounded(icon->GetTexRef(), {centre.x - half, centre.y - half},
                              {centre.x + half, centre.y + half}, {0, 0}, {1, 1},
                              a(255, 255, 255, 255), 4.0f * s);
        const ImVec2 dot{centre.x + half - 2.0f * s, centre.y + half - 2.0f * s};
        draw->AddCircleFilled(dot, 9.0f * s, badge);
        draw->AddCircle(dot, 9.0f * s, a(30, 25, 20, 255), 0, 1.5f * s);
    } else {
        draw->AddCircleFilled(centre, 24.0f * s, badge);
        draw->AddCircle(centre, 24.0f * s, a(30, 25, 20, 255), 0, 2.0f * s);
        draw->AddText({centre.x - letter_size.x * 0.5f, centre.y - letter_size.y * 0.5f},
                      a(30, 25, 20, 255), letter);
    }

    const float text_x = x + 76.0f * s, wrap = size.x - 88.0f * s;
    static const bool russian = [] {
        const char* env = bb_env("BB_LANGUAGE");
        return env && std::atoi(env) == 8;
    }();
    draw->AddText({text_x, y + 12.0f * s}, a(200, 185, 150, 255), russian ? "Получен трофей" : "Trophy earned");
    draw->AddText(nullptr, 0.0f, {text_x, y + 12.0f * s + ImGui::GetTextLineHeightWithSpacing()},
                  a(240, 232, 215, 255), toast.name.c_str(), nullptr, wrap);
}

} // namespace BbOverlay
