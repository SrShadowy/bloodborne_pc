// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: user settings changed at run time from the in-game menu (bbport_overlay.h) and kept
// in bbport.ini (BB_CONFIG overrides the path). Environment variables override the file at
// start. Readers load the atomics every frame; writers are the menu and Load().

#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace BbSettings {

enum Upscaler : int { UpscalerOff = 0, UpscalerFsr3 = 1, UpscalerFsr4 = 2, UpscalerFsr411 = 3,
                      UpscalerTaa = 4, UpscalerDlss = 5, UpscalerCount };
/// FSR 4 v07 or FSR 4.1.1: the same inputs, settings and placement in the frame.
inline bool IsFsr4(int upscaler) {
    return upscaler == UpscalerFsr4 || upscaler == UpscalerFsr411;
}
enum Preset : int { NativeAA = 0, Quality, Balanced, Performance, UltraPerformance, PresetCount };
enum DebugView : int { DebugNone = 0, DebugReactive = 1, DebugMotion = 2, DebugViewCount };
enum Language : int { LangEnglish = 0, LangPortuguese = 1, LangRussian = 2, LangCount = 3 };
enum class MenuLanguage : int { English = LangEnglish, Portuguese = LangPortuguese, Russian = LangRussian };

/// Game effects switched by the community patches at start (patches.py EFFECTS): ini key,
/// menu label, default (the game's own behaviour).
struct Effect {
    const char* key;
    const char* label;
    const char* label_ru;
    bool default_on;
};
inline constexpr Effect Effects[] = {
    {"effect_chromatic_aberration", "Chromatic aberration", "Хроматическая аберрация", true},
    {"effect_dof", "Depth of field (DoF)", "Глубина резкости (DoF)", true},
    {"effect_motion_blur", "Motion blur", "Размытие в движении", true},
    {"effect_ssao", "Ambient occlusion (SSAO)", "Затенение SSAO", true},
    {"effect_game_aa", "Game's own anti-aliasing", "Собственное сглаживание игры", true},
    {"effect_dynamic_shadows", "Shadows from dynamic lights", "Тени от динамических источников", true},
    {"effect_ssr", "Screen-space reflections (not in original game)", "Отражения SSR (не было в игре)", false},
    {"skip_intro", "Skip startup intros", "Пропуск заставок при запуске", false},
    {"debug_camera", "Free camera (Cross + L3)", "Свободная камера (Cross + L3)", false},
    {"debug_menu", "Debug menu (requires font files)", "Debug menu (нужны файлы шрифтов)", false},
};
inline constexpr int EffectCount = int(sizeof(Effects) / sizeof(Effects[0]));
/// Output aspect ratios, and the output resolutions of each: the upscaler's output and the UI's
/// targets, changed live. The game keeps its 1920x1080 frame; for another aspect ratio its camera,
/// HUD markers and UI placement follow the output at run time (bbport_guest_hooks, the UI
/// composition). Auto: the group of the monitor the game is on.
enum Aspect : int { AspectAuto = 0, Aspect4x3, Aspect16x10, Aspect16x9, Aspect21x9, Aspect32x9, AspectCount };
/// The command processor's path (bbport_draw_pipe): off (the game's own), on (the port's), or
/// hybrid (per draw).
enum DrawPipeMode : int { DrawPipeOff = 0, DrawPipeOn = 1, DrawPipeHybrid = 2, DrawPipeCount = 3 };
struct OutputSize {
    int width = 1920, height = 1080;
    bool operator==(const OutputSize&) const = default;
};
inline constexpr OutputSize OutputDefault{1920, 1080}; ///< the game's own size
constexpr std::uint32_t PackSize(OutputSize s) {
    return std::uint32_t(s.width) | std::uint32_t(s.height) << 16;
}
constexpr OutputSize UnpackSize(std::uint32_t v) {
    return {int(v & 0xffff), int(v >> 16)};
}
/// An aspect ratio's outputs, smallest first.
struct OutputList {
    std::array<OutputSize, 12> sizes{};
    int count = 0;
    const OutputSize* begin() const { return sizes.data(); }
    const OutputSize* end() const { return sizes.data() + count; }
    int IndexOf(OutputSize size) const; ///< -1: not in the list
};
/// bbport.ini value ("auto", "4:3", "16:10", "16:9", "21:9", "32:9").
const char* AspectKey(int aspect);
/// The monitor the game's window is on (window.cpp): Auto follows it. 0x0 until known.
void SetMonitor(int width, int height);
OutputSize Monitor();
/// Auto resolved to the group nearest the monitor's aspect ratio (16:9 until it is known).
int ResolvedAspect(int aspect);
/// The output resolutions of an aspect ratio (Auto: the monitor's group and its own size).
OutputList Outputs(int aspect);
/// The output of `aspect` nearest `size` in height: another aspect ratio keeps the height.
OutputSize NearestOutput(int aspect, OutputSize size);
/// The output in use: the chosen resolution in the chosen aspect ratio.
OutputSize CurrentOutput();
/// "2560x1080".
std::string SizeText(OutputSize size);

struct Values {
    std::atomic<int> menu_language{LangPortuguese};
    /// Chosen in the menu (or already in bbport.ini): only then is it saved, so the language
    /// the menu merely started with keeps following the launcher's (#134).
    std::atomic<bool> menu_language_chosen{false};
    std::atomic<int> upscaler{UpscalerFsr3};
    std::atomic<int> preset{NativeAA};
    std::atomic<bool> sharpen{true};
    std::atomic<float> sharpness{0.3f};
    std::atomic<bool> jitter{true};
    std::atomic<bool> reactive{false};
    std::atomic<bool> object_motion{true};
    std::atomic<float> reactive_scale{1.0f};
    std::atomic<float> reactive_threshold{0.2f};
    std::atomic<float> reactive_max{0.9f};
    std::atomic<int> debug_view{DebugNone};
    std::atomic<bool> show_fps{false};
    /// The settings menu's position (fraction of the screen), -1 until it is moved.
    std::atomic<float> menu_x{-1.0f}, menu_y{-1.0f};
    // FSR 4 checks (menu): the provider's auto exposure, the jitter sign it is given.
    std::atomic<bool> fsr4_auto_exposure{true};
    std::atomic<bool> fsr4_invert_jitter{false};
    std::atomic<bool> puddle_reflections{false};
    std::atomic<int> draw_pipe{DrawPipeHybrid};
    std::atomic<int> active_render_width{1920}, active_render_height{1080};
    /// Applied at start (patches.py); the menu shows when a restart is needed.
    std::atomic<bool> effects[EffectCount]{};
    std::atomic<int> model_lod{0}; ///< -2 highest .. 2 lowest, 0 the game's
    std::atomic<int> aspect{AspectAuto};
    /// The chosen output resolution (PackSize; CurrentOutput fits it to the aspect ratio).
    std::atomic<std::uint32_t> output_res{PackSize(OutputDefault)};
    /// Live resolution and preset changes (run.sh): 0 off by default (startup patch, fastest
    /// on the Steam Deck and older GPUs), -1 auto (strong discrete GPUs), 1 on. On restart.
    std::atomic<int> live_resolution{0};
    /// FSR 3 frame generation (upscale/vk_frame_generation): a frame interpolated between each
    /// two of the game's, shown between them (needs the upscaler's scaled path). Live.
    std::atomic<bool> frame_generation{false};
    /// Whether the GPU can (set by the renderer), or why not.
    std::atomic<bool> frame_generation_supported{false};
    std::atomic<const char*> frame_generation_problem{nullptr};
    /// Why FSR 4 cannot run (assets, device features), or null. Set by the renderer.
    std::atomic<const char*> fsr4_problem{nullptr};
    std::atomic<bool> fsr4_supported{false}, fsr411_supported{false};
    std::atomic<bool> fsr411_fp8{false}, fsr411_fp8emu{false};
    /// DLSS (gpu/dlss_bridge, NVIDIA RTX) is ready, or why not (null before the device exists).
    std::atomic<bool> dlss_supported{false};
    std::atomic<const char*> dlss_problem{nullptr};

    /// Mouse look and button controls.
    std::atomic<float> mouse_sensitivity{1.0f};
    std::atomic<bool> mouse_invert_y{false};
    std::atomic<bool> mouse_invert_x{false};
    std::atomic<bool> mouse_capture{true};
    std::atomic<bool> raw_mouse_camera{true};
    std::atomic<bool> mouse_hold_auto_rotate{true};
    std::atomic<int> fps_limit{0}; ///< Target frame rate limit (0 = uncapped)

    /// Live post-processing, lighting, and camera parameters (from bbhost).
    std::atomic<float> bloom_scale{1.0f};
    std::atomic<float> color_saturation{1.0f};
    std::atomic<bool> vignette{true};
    std::atomic<float> ao_strength{1.0f};
    std::atomic<float> shadow_distance{1.0f};
    std::atomic<float> fog_opacity{1.0f};
    std::atomic<float> fov_scale{1.0f};
    std::atomic<float> cam_distance{1.0f};
    std::atomic<float> cam_height{1.0f};
    std::atomic<bool> light_grid_opt{false};

    /// Photo mode
    std::atomic<bool> photo_pause{false};
    std::atomic<bool> hide_hud{false};
    std::atomic<bool> request_screenshot{false};

    /// Info HUD (OSD)
    std::atomic<bool> show_info_hud{false};
    std::atomic<int> info_hud_pos{0}; // 0 = Top-Left, 1 = Top-Right, 2 = Bottom-Left, 3 = Bottom-Right
    std::atomic<bool> info_hud_show_playtime{true};
    std::atomic<bool> info_hud_show_map{true};
    std::atomic<bool> info_hud_show_name{true};
    std::atomic<bool> info_hud_show_coop{true};
    std::atomic<bool> info_hud_show_deaths{true};

    /// Discord Rich Presence (RPC)
    std::atomic<bool> discord_rpc{true};
    std::atomic<bool> discord_rpc_show_character{true}; // legacy alias
    std::atomic<bool> discord_rpc_show_name{true};
    std::atomic<bool> discord_rpc_show_level{true};
    std::atomic<bool> discord_rpc_show_deaths{true};
    std::atomic<int>  discord_rpc_deaths_mode{1}; // legacy alias
    std::atomic<bool> discord_rpc_show_area{true};
    std::atomic<bool> discord_rpc_show_time{true};
    std::atomic<bool> discord_rpc_show_party{true};
    std::atomic<bool> discord_rpc_show_boss{true};
    std::atomic<bool> discord_rpc_window_open{false};
    std::string discord_client_id{"1139939140494971051"};

    /// Startup settings for the explicit BB_RENDER_RES compatibility patch only.
    int startup_preset = NativeAA;
    int startup_upscaler = UpscalerFsr3;
    bool startup_object_motion = true;
    bool startup_effects[EffectCount]{};
    int startup_model_lod = 0;
    /// The output the startup patch was made for (BB_OUTPUT_RES), else 0x0.
    OutputSize startup_output{0, 0};
    int startup_live_resolution = 0;
    int startup_draw_pipe = DrawPipeHybrid;
    bool startup_frame_generation = false;
};

Values& Get();
/// Localized overlay text; the Language selector itself stays in English.
const char* MenuText(const char* english, const char* russian);
/// The ini code ("en", "pt_br", "ru") and display name of a Language.
const char* LanguageCode(int lang);
const char* LanguageName(int lang);
/// The menu label of a game effect (Effects[effect_index]) in a Language.
const char* EffectLabel(int effect_index, int lang);

/// Reads the file, then the environment overrides. Called once at start.
void Load();
/// Checks the loaded choice before the first frame; unsupported FSR 4 uses FSR 3.1.
/// fsr411_fp8: the GPU runs FSR 4.1.1's FP8 shaders natively; fsr411_fp8emu: via the
/// cooperative-matrix path (bbport: the last two default off, as before 0.5's probes).
void ConfigureUpscalerSupport(bool fsr4, bool fsr411, bool fsr411_fp8 = false,
                              bool fsr411_fp8emu = false);
/// After device creation: DLSS availability; a DLSS setting falls back to FSR 3.1 without it.
void ConfigureDlssSupport(bool available, const char* problem);
/// Startup-patched scene dimensions cannot change until run.sh prepares a new image.
bool FixedRenderSession();
int RenderPreset();
bool ResolutionNeedsRestart();
/// Writes the file (menu changes).
void Save();

/// Render resolution divisor of a preset (1.0 native, 1.5 quality, ...).
float PresetScale(int preset);
const char* PresetName(int preset);
const char* UpscalerName(int upscaler);

} // namespace BbSettings
