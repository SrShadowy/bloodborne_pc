// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_overlay.h"
#include "bbport_overlay_internal.h"
#include "shim/bb_env.h"
#include "bbport_toggles.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>
#include <fmt/format.h>
#include "bbport_overlay_sections.h"
#include "extra/management/hud/bbport_fps_hud.h"
#include "bbport_discord_rpc.h"
#include "bbport_info_hud.h"
#include "rpc/bbport_game_data.h"
#include "bbport_strings.h"
#include "ui_manager.h"
#include "ui_strings.h"
#include "bbport_shader_tracker.h"
#include "extra/ui/keyboard/bbport_keyboard.h"
#include "bbport_guest_hooks.h"
#include "bbport_frame_rate.h"
#include "bbport_settings.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include "vulkan/instance.h"
#include "render/compile_log.h"
#include "render/scheduler.h"

#ifdef _WIN32
#define BB_FONT_SECTION ".section .rdata,\"dr\"\n"
#define BB_FONT_HIDDEN(name) ""
#define BB_FONT_END ".text\n"
#else
#define BB_FONT_SECTION ".section .rodata\n"
#define BB_FONT_HIDDEN(name) ".hidden " name "\n"
#define BB_FONT_END ".previous\n"
#endif
asm(BB_FONT_SECTION
    ".balign 16\n"
    BB_FONT_HIDDEN("bb_font_ttf")
    ".global bb_font_ttf\n"
    "bb_font_ttf:\n"
    ".incbin \"" BB_FONT_PATH "\"\n"
    BB_FONT_HIDDEN("bb_font_ttf_end")
    ".global bb_font_ttf_end\n"
    "bb_font_ttf_end:\n"
    BB_FONT_END);
extern "C" const unsigned char bb_font_ttf[];
extern "C" const unsigned char bb_font_ttf_end[];

extern "C" void runtime_restart(void);

namespace BbOverlay {

namespace {

using namespace detail;

std::mutex imgui_mutex;
bool initialized = false;
std::atomic<bool> menu_open{false};
bool l3_down = false, r3_down = false;
constexpr long long OpenHoldMs = 800;
std::atomic<long long> l3r3_since{0};
std::atomic<bool> open_requested{false};

long long NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::mutex prompt_mutex;
std::atomic<bool> prompt_active{false};
std::string prompt_title, prompt_text;

std::mutex banner_mutex;
std::string banner_text;
std::atomic<bool> banner_active{false};
std::chrono::steady_clock::time_point banner_expire{};

std::chrono::steady_clock::time_point last_present_total{};
std::chrono::steady_clock::time_point last_present_orig{};
float total_ms_avg = 0.0f;
float orig_ms_avg = 0.0f;
float frame_ms_avg = 0.0f;
std::chrono::steady_clock::time_point last_generated_time{};

float PixelDensity(SDL_WindowID id);
std::atomic<u32> display_width{0}, display_height{0};

ImVec2 ToDisplay(SDL_WindowID id, float x, float y) {
    SDL_Window* window = SDL_GetWindowFromID(id);
    int w = 0, h = 0;
    const u32 dw = display_width.load(std::memory_order_relaxed);
    const u32 dh = display_height.load(std::memory_order_relaxed);
    if (window && dw && dh && SDL_GetWindowSize(window, &w, &h) && w > 0 && h > 0) {
        return {x * float(dw) / float(w), y * float(dh) / float(h)};
    }
    const float density = PixelDensity(id);
    return {x * density, y * density};
}

} // namespace

void SetOpen(bool value) {
    if (menu_open.exchange(value) == value) return;
    if (value) {
        if (SDL_Window* window = SDL_GetMouseFocus()) {
            float x = 0.0f, y = 0.0f;
            SDL_GetMouseState(&x, &y);
            const ImVec2 at = ToDisplay(SDL_GetWindowID(window), x, y);
            ImGui::GetIO().AddMousePosEvent(at.x, at.y);
        }
    }
    if (!value && detail::dirty) {
        detail::dirty = false;
        BbSettings::Save();
    }
}

namespace {

ImGuiKey KeyFromSdl(SDL_Keycode k) {
    switch (k) {
    case SDLK_TAB: return ImGuiKey_Tab; case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow; case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow; case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown; case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End; case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace; case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter; case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDLK_ESCAPE: return ImGuiKey_Escape; case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl; case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_RSHIFT: return ImGuiKey_RightShift; case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_RALT: return ImGuiKey_RightAlt; default: return ImGuiKey_None;
    }
}

ImGuiKey KeyFromGamepad(u8 b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceDown;
    case SDL_GAMEPAD_BUTTON_EAST: return ImGuiKey_GamepadFaceRight;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

float PixelDensity(SDL_WindowID id) {
    SDL_Window* window = SDL_GetWindowFromID(id);
    const float density = window ? SDL_GetWindowPixelDensity(window) : 1.0f;
    return density > 0.0f ? density : 1.0f;
}

std::string FpsText() {
    const auto& s = BbSettings::Get();
    const float shown = frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f;
    const int generated = s.frame_generation.load(std::memory_order_relaxed) != BbSettings::FrameGenOff
                              ? s.frame_gen_now.load(std::memory_order_relaxed) : 0;
    char text[192];
    if (generated > 0) {
        std::snprintf(text, sizeof(text),
                      BbSettings::MenuText("%.0f FPS  FG %dx  %.1f ms", "%.0f FPS  FG %dx  %.1f мс"),
                      shown, generated + 1, frame_ms_avg);
    } else {
        std::snprintf(text, sizeof(text), BbSettings::MenuText("%.0f FPS  %.1f ms", "%.0f FPS  %.1f мс"),
                      shown, frame_ms_avg);
    }
    if (const u32 lost = BbStats::vram_budget_lost_mb.load(std::memory_order_relaxed); lost >= 150) {
        const size_t used = std::strlen(text);
        std::snprintf(text + used, sizeof(text) - used,
                      BbSettings::MenuText("  VRAM -%u MB (other apps)", "  VRAM −%u МБ (другие программы)"), lost);
    }
    return text;
}

} // namespace

namespace detail {

bool OverlayTrace() {
    static const bool on = bb_env_on("BB_OVERLAY_TRACE");
    return on;
}

void Checkbox(const char* label, std::atomic<bool>& value) {
    bool v = value;
    Store(value, v, ImGui::Checkbox(label, &v));
}

void Slider(const char* label, std::atomic<float>& value, float lo, float hi) {
    float v = value;
    Store(value, v, ImGui::SliderFloat(label, &v, lo, hi, "%.2f"));
}

void Hint(const char* text) {
    ImGui::SameLine();
    ImGui::PushTextWrapPos(-1.0f);
    ImGui::TextDisabled("(?)");
    ImGui::PopTextWrapPos();
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace detail

namespace {

void FpsCounter() {
    BbFpsHud::Render(BbFpsHud::Stats{
        .orig_ms_avg = orig_ms_avg,
        .total_ms_avg = total_ms_avg,
        .last_generated_time = last_generated_time,
        .base_scale = base_scale,
    });
}

struct ShaderProgress {
    u32 done = 0, total = 0, recent = 0;
    bool importing = false;
};

ShaderProgress CurrentShaderProgress() {
    static const bool enabled = !bb_env_off("BB_SHADER_PROGRESS");
    ShaderProgress p;
    if (!enabled) return p;
    p.total = g_bb_background_pipelines_total.load(std::memory_order_relaxed);
    p.done = g_bb_background_pipelines_done.load(std::memory_order_relaxed);
    static const long long start = NowNs();
    p.importing = g_bb_shader_import_active.load(std::memory_order_relaxed) && NowNs() - start < 180'000'000'000ll;
    static u32 last_compiles = 0, window_start_compiles = 0;
    static long long last_change = 0;
    const u32 compiles = g_bb_compiles_total.load(std::memory_order_relaxed);
    const long long now = NowNs();
    if (compiles != last_compiles) {
        if (now - last_change > 1'500'000'000ll) window_start_compiles = last_compiles;
        last_compiles = compiles;
        last_change = now;
    }
    if (last_change != 0 && now - last_change <= 1'500'000'000ll) p.recent = compiles - window_start_compiles;
    return p;
}

bool ShaderProgressShown() {
    const ShaderProgress p = CurrentShaderProgress();
    return p.done < p.total || p.importing || p.recent != 0;
}

void ShaderProgressLine() {
    const ShaderProgress p = CurrentShaderProgress();
    std::string text;
    if (p.done < p.total) {
        text = fmt::format("{} {} / {}", BbSettings::MenuText("Compiling shaders", "Компиляция шейдеров"), p.done, p.total);
    } else if (p.importing) {
        text = BbSettings::MenuText("Preparing shaders...", "Подготовка шейдеров...");
    } else if (p.recent != 0) {
        text = fmt::format("{} ({})", BbSettings::MenuText("Compiling shaders", "Компиляция шейдеров"), p.recent);
    } else {
        return;
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + pad, vp->WorkPos.y + vp->WorkSize.y - pad), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##shader_progress", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted(text.c_str());
    ImGui::End();
}

void TextPrompt() {
    std::string title, text;
    {
        std::scoped_lock lock{prompt_mutex};
        title = prompt_title;
        text = prompt_text;
    }
    const auto& s = BbSettings::Get();
    const int lang = s.menu_language.load();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float display_scale = vp ? (vp->WorkSize.y / 1080.0f) : 1.0f;
    BbVirtualKeyboard::Render(title, text, lang, display_scale);
}

void ScriptedKeys() {
    static const char* path = bb_env("BB_MENU_KEYS_FILE");
    static std::chrono::steady_clock::time_point last_check{};
    static std::vector<std::string> queue;
    static bool release = false, mouse_release = false;
    static ImGuiKey held = ImGuiKey_None;
    if (!path) return;
    ImGuiIO& io = ImGui::GetIO();
    if (release) { io.AddKeyEvent(held, false); release = false; return; }
    if (mouse_release) { io.AddMouseButtonEvent(0, false); mouse_release = false; return; }
    const auto now = std::chrono::steady_clock::now();
    if (queue.empty() && now - last_check > std::chrono::milliseconds(100)) {
        last_check = now;
        if (FILE* f = std::fopen(path, "r")) {
            char tok[32];
            while (std::fscanf(f, "%31s", tok) == 1) queue.emplace_back(tok);
            std::fclose(f);
            std::remove(path);
        }
    }
    if (queue.empty()) return;
    const std::string token = queue.front();
    queue.erase(queue.begin());
    if (token == "toggle") { SetOpen(!menu_open); return; }
    if (token == "back") { SetOpen(false); return; }
    if (token == "click" && queue.size() >= 2) {
        const float x = std::strtof(queue[0].c_str(), nullptr), y = std::strtof(queue[1].c_str(), nullptr);
        queue.erase(queue.begin(), queue.begin() + 2);
        io.AddMousePosEvent(x, y);
        io.AddMouseButtonEvent(0, true);
        mouse_release = true;
        return;
    }
    held = token == "up" ? ImGuiKey_GamepadDpadUp : token == "down" ? ImGuiKey_GamepadDpadDown
         : token == "left" ? ImGuiKey_GamepadDpadLeft : token == "right" ? ImGuiKey_GamepadDpadRight
         : token == "enter" ? ImGuiKey_GamepadFaceDown : token == "l1" ? ImGuiKey_GamepadL1
         : token == "r1" ? ImGuiKey_GamepadR1 : token == "kdown" ? ImGuiKey_DownArrow
         : token == "kup" ? ImGuiKey_UpArrow : ImGuiKey_None;
    if (held != ImGuiKey_None) { io.AddKeyEvent(held, true); release = true; }
}

} // namespace

void SetTextPrompt(bool active, const std::string& prompt, const std::string& text,
                   const BbVirtualKeyboard::Callbacks& callbacks) {
    {
        std::scoped_lock lock{prompt_mutex};
        prompt_title = prompt;
        prompt_text = text;
    }
    if (active && !prompt_active) BbVirtualKeyboard::Reset();
    if (callbacks.on_append || callbacks.on_confirm) BbVirtualKeyboard::SetCallbacks(callbacks);
    prompt_active = active;
}

void RenderGraphicsSettings() {
    auto& s = BbSettings::Get();
    const bool taa = detail::UpscalerSection(s);
    detail::ReactiveMaskSection(s, taa);
    detail::OutputSection(s);
    detail::GameEffectsSection(s);
    detail::FrameRateLimitSection(s);
    BbOverlaySections::RenderPhotoModeSection(s, s.menu_language, detail::dirty);
    BbOverlaySections::RenderPostProcessingAndLightingSection(s, s.menu_language, detail::dirty);
    BbOverlaySections::RenderCameraSection(s, s.menu_language, detail::dirty);
    BbOverlaySections::RenderFpsOverlaySection(s, s.menu_language, detail::dirty);
    BbOverlaySections::RenderInfoHudSection(s, s.menu_language, detail::dirty);
    BbOverlaySections::RenderDiscordRpcSection(s, s.menu_language, detail::dirty);
    BbOverlaySections::RenderRawMouseCameraControls(s, s.menu_language, detail::dirty);
    BbOverlaySections::RenderMemoryToolsSection(detail::base_scale);
}

void Init(const Vulkan::Instance& instance, vk::Format format, u32 image_count) {
    std::scoped_lock lock{imgui_mutex};
    if (initialized) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "bbport";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.92f;

    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bb_font_ttf),
                                   int(bb_font_ttf_end - bb_font_ttf), 18.0f, &font_config);

    const vk::Instance vk_instance = instance.GetInstance();
    ImGui_ImplVulkan_LoadFunctions(
        instance.ApiVersion(),
        [](const char* name, void* user) {
            return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(
                *static_cast<const vk::Instance*>(user), name);
        },
        const_cast<vk::Instance*>(&vk_instance));

    const VkFormat color_format = static_cast<VkFormat>(format);
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = instance.ApiVersion();
    info.Instance = vk_instance;
    info.PhysicalDevice = instance.GetPhysicalDevice();
    info.Device = instance.GetDevice();
    info.QueueFamily = instance.GetGraphicsQueueFamilyIndex();
    info.Queue = instance.GetGraphicsQueue();
    info.DescriptorPoolSize = 16;
    info.MinImageCount = std::max(image_count, 2u);
    info.ImageCount = std::max(image_count, 2u);
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };
    if (!ImGui_ImplVulkan_Init(&info)) {
        std::printf("Overlay: ImGui Vulkan backend init failed\n");
        ImGui::DestroyContext();
        return;
    }
    initialized = true;
    std::printf("Overlay: menu ready (Insert, or hold L3+R3)\n");
    UI::UiManager::InitStyle();
    BbInfoHud::Init();
    BbDiscordRpc::Init();
}

void UpdateTextInput(SDL_Window* window) {
    bool want = false;
    {
        std::scoped_lock lock{imgui_mutex};
        want = initialized && menu_open && ImGui::GetIO().WantTextInput;
    }
    if (want != SDL_TextInputActive(window)) {
        if (want) SDL_StartTextInput(window);
        else SDL_StopTextInput(window);
    }
}

bool HandleEvent(const SDL_Event& event) {
    std::scoped_lock lock{imgui_mutex};
    if (!initialized) return false;
    ImGuiIO& io = ImGui::GetIO();
    const bool is_open = menu_open;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        if (down && !event.key.repeat && (event.key.key == SDLK_INSERT || (is_open && event.key.key == SDLK_ESCAPE))) {
            SetOpen(event.key.key == SDLK_INSERT ? !is_open : false);
            return true;
        }
        if (down && !event.key.repeat && (event.key.key == SDLK_F12 || event.key.key == SDLK_PRINTSCREEN)) {
            BbSettings::Get().request_screenshot.store(true, std::memory_order_relaxed);
            return true;
        }
        if (down && !event.key.repeat && event.key.key == SDLK_F11) {
            bool cur = BbSettings::Get().hide_hud.load(std::memory_order_relaxed);
            BbSettings::Get().hide_hud.store(!cur, std::memory_order_relaxed);
            SetBanner(!cur ? "HUD: Oculto (Hidden)" : "HUD: Visivel (Visible)", 2.0f);
            return true;
        }
        if (down && !event.key.repeat && (event.key.key == SDLK_F10 || event.key.key == SDLK_PAUSE)) {
            bool cur = BbSettings::Get().photo_pause.load(std::memory_order_relaxed);
            BbSettings::Get().photo_pause.store(!cur, std::memory_order_relaxed);
            SetBanner(!cur ? "Mundo: Pausado (Simulation Frozen)" : "Mundo: Despausado (Resumed)", 2.0f);
            return true;
        }
        if (!is_open) return false;
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.key.mod & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.key.mod & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.key.mod & SDL_KMOD_ALT) != 0);
        if (const ImGuiKey key = KeyFromSdl(event.key.key); key != ImGuiKey_None) io.AddKeyEvent(key, down);
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        if (prompt_active.load(std::memory_order_relaxed) && BbVirtualKeyboard::HandleGamepadEvent(event)) return true;
        const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const u8 button = event.gbutton.button;
        if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) l3_down = down;
        else if (button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) r3_down = down;
        if (l3_down && r3_down) {
            if (down && is_open) { SetOpen(false); return true; }
            if (down && l3r3_since.load() == 0) l3r3_since = NowNs();
        } else {
            l3r3_since = 0;
        }
        if (!is_open) return false;
        if (const ImGuiKey key = KeyFromGamepad(button); key != ImGuiKey_None) io.AddKeyEvent(key, down);
        return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        return prompt_active.load(std::memory_order_relaxed) && BbVirtualKeyboard::HandleGamepadEvent(event);
    case SDL_EVENT_TEXT_INPUT:
        if (!is_open) return false;
        io.AddInputCharactersUTF8(event.text.text);
        return true;
    case SDL_EVENT_MOUSE_MOTION: {
        if (!is_open) return false;
        const ImVec2 at = ToDisplay(event.motion.windowID, event.motion.x, event.motion.y);
        io.AddMousePosEvent(at.x, at.y);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!is_open) return false;
        const int button = event.button.button == SDL_BUTTON_LEFT ? 0
                         : event.button.button == SDL_BUTTON_RIGHT ? 1
                         : event.button.button == SDL_BUTTON_MIDDLE ? 2 : -1;
        if (button >= 0) io.AddMouseButtonEvent(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (!is_open) return false;
        io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
        return true;
    default:
        return false;
    }
}

void SetBanner(const char* text, float duration_seconds) {
    {
        std::scoped_lock lock{banner_mutex};
        banner_text = text ? text : "";
        if (duration_seconds > 0.0f) {
            banner_expire = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(int(duration_seconds * 1000.0f));
        } else {
            banner_expire = {};
        }
    }
    banner_active = text != nullptr && text[0] != 0;
}

bool Visible() {
    if (banner_active.load(std::memory_order_relaxed)) {
        std::scoped_lock lock{banner_mutex};
        if (banner_expire.time_since_epoch().count() != 0 && std::chrono::steady_clock::now() >= banner_expire) {
            banner_active = false;
        }
    }
    static bool open_at_start = bb_env_on("BB_OVERLAY_OPEN");
    if (open_at_start && initialized) {
        open_at_start = false;
        open_requested = true;
    }
    if (const long long since = l3r3_since.load();
        since != 0 && !menu_open && NowNs() - since >= OpenHoldMs * 1'000'000) {
        l3r3_since = 0;
        open_requested = true;
    }
    return initialized && (menu_open || open_requested || prompt_active || banner_active ||
                           BbOverlay::TrophyActive() || BbSettings::Get().show_fps || ShaderProgressShown() ||
                           UI::UiManager::IsOpen() || BbSettings::Get().show_info_hud);
}

bool MenuOpen() {
    return menu_open;
}

bool CapturesInput() {
    return menu_open || prompt_active;
}

void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent, bool is_generated) {
    const auto now = std::chrono::steady_clock::now();
    if (last_present_total.time_since_epoch().count() != 0) {
        const float ms = std::chrono::duration<float, std::milli>(now - last_present_total).count();
        if (ms > 0.0f && ms < 1000.0f) total_ms_avg = total_ms_avg == 0.0f ? ms : total_ms_avg * 0.95f + ms * 0.05f;
    }
    last_present_total = now;

    if (is_generated) {
        last_generated_time = now;
    } else {
        if (last_present_orig.time_since_epoch().count() != 0) {
            const float ms = std::chrono::duration<float, std::milli>(now - last_present_orig).count();
            if (ms > 0.0f && ms < 1000.0f) orig_ms_avg = orig_ms_avg == 0.0f ? ms : orig_ms_avg * 0.95f + ms * 0.05f;
        }
        last_present_orig = now;
    }
    if (total_ms_avg == 0.0f) total_ms_avg = orig_ms_avg;
    frame_ms_avg = total_ms_avg;

    static const bool fps_print = bb_env_on("BB_FPS_PRINT");
    if (fps_print) {
        static auto window_start = now;
        static std::uint64_t presents = 0;
        ++presents;
        if (const double s = std::chrono::duration<double>(now - window_start).count(); s >= 5.0) {
            std::printf("Present rate: %.1f FPS over %.1f s\n", presents / s, s);
            std::fflush(stdout);
            window_start = now;
            presents = 0;
        }
    }
    if (bb_env("BB_MENU_KEYS_FILE") && initialized) {
        std::scoped_lock lock{imgui_mutex};
        ScriptedKeys();
    }
    UI::UiManager::Tick();
    if (!Visible()) return;

    std::scoped_lock lock{imgui_mutex};
    if (open_requested.exchange(false)) SetOpen(true);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(extent.width), float(extent.height));
    display_width.store(extent.width, std::memory_order_relaxed);
    display_height.store(extent.height, std::memory_order_relaxed);
    const float dt_ms = total_ms_avg > 0.0f ? total_ms_avg : 16.666f;
    io.DeltaTime = dt_ms > 0.0f && dt_ms < 1000.0f ? dt_ms / 1000.0f : 1.0f / 60.0f;

    const float scale = std::max(float(extent.height) / 1080.0f, 0.75f);
    if (std::abs(scale - base_scale) > 0.01f) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(scale / base_scale);
        style.FontScaleMain = scale;
        base_scale = scale;
    }

    BbGameData::Tick();

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();

    if (menu_open) detail::Menu();
    if ((BbSettings::Get().show_fps || BbShaderTracker::IsCompiling()) && !menu_open) FpsCounter();
    if (BbSettings::Get().show_info_hud && !menu_open && !BbSettings::Get().hide_hud) BbInfoHud::Render(base_scale);
    if (prompt_active && !menu_open) TextPrompt();
    if (!menu_open) ShaderProgressLine();
    if (BbOverlay::TrophyActive()) BbOverlay::TrophyWindow(io.DeltaTime);
    if (banner_active) {
        std::string text;
        {
            std::scoped_lock banner_lock{banner_mutex};
            text = banner_text;
        }
        const ImVec2 size = ImGui::CalcTextSize(text.c_str());
        auto* draw = ImGui::GetForegroundDrawList();
        const ImVec2 at{(io.DisplaySize.x - size.x) * 0.5f, io.DisplaySize.y * 0.82f};
        draw->AddRectFilled({at.x - 24.0f, at.y - 12.0f}, {at.x + size.x + 24.0f, at.y + size.y + 12.0f},
                            IM_COL32(0, 0, 0, 170), 6.0f);
        draw->AddText(at, IM_COL32(230, 220, 200, 255), text.c_str());
    }
    ImGui::Render();

    const vk::RenderingAttachmentInfo attachment{
        .imageView = view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    });
    {
        std::scoped_lock submit_lock{Vulkan::Scheduler::submit_mutex};
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdbuf);
    }
    cmdbuf.endRendering();
}

} // namespace BbOverlay

extern "C" void bbgpu_overlay_banner(const char* text) {
    BbOverlay::SetBanner(text);
}
