// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_settings.h"
#include "bbport_strings.h"
#include "shim/bb_env.h"
#include <cmath>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace BbSettings {

namespace {
/// anti_aliasing was in bbport.ini (Load).
bool anti_aliasing_seen = false;
}

namespace {

const char* Path() {
    const char* env = bb_env("BB_CONFIG");
    return env && env[0] ? env : "bbport.ini";
}

float Clamp(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

void Set(Values& v, const std::string& key, const std::string& value) {
    const float f = float(std::atof(value.c_str()));
    const int i = std::atoi(value.c_str());
    if (key == "menu_language") {
        if (value == "en" || value == "english" || value == "0") {
            v.menu_language = LangEnglish;
        } else if (value == "pt" || value == "pt_br" || value == "pt-br" || value == "portuguese" || value == "1") {
            v.menu_language = LangPortuguese;
        } else if (value == "ru" || value == "russian" || value == "2") {
            v.menu_language = LangRussian;
        } else {
            std::printf("Settings: unknown menu_language '%s' (expected en, pt or ru)\n", value.c_str());
        }
        v.menu_language_chosen = value == "en" || value == "english" || value == "0" || value == "pt" ||
                                 value == "pt_br" || value == "pt-br" || value == "portuguese" ||
                                 value == "1" || value == "ru" || value == "russian" || value == "2";
    } else if (key == "upscaler") {
        for (int u = 0; u < UpscalerCount; ++u) {
            if (value == UpscalerName(u)) {
                v.upscaler = u;
            }
        }
    } else if (key == "preset") {
        v.preset = std::clamp(i, 0, PresetCount - 1);
    } else if (key == "sharpen") {
        v.sharpen = i != 0;
    } else if (key == "sharpness") {
        v.sharpness = Clamp(f, 0.0f, 2.0f);
    } else if (key == "jitter") {
        v.jitter = i != 0;
    } else if (key == "reactive") {
        v.reactive = i != 0;
    } else if (key == "object_motion") {
        v.object_motion = i != 0;
    } else if (key == "reactive_scale") {
        v.reactive_scale = Clamp(f, 0.0f, 16.0f);
    } else if (key == "reactive_threshold") {
        v.reactive_threshold = Clamp(f, 0.0f, 1.0f);
    } else if (key == "reactive_max") {
        v.reactive_max = Clamp(f, 0.0f, 1.0f);
    } else if (key == "debug_view") {
        v.debug_view = std::clamp(i, 0, DebugViewCount - 1);
    } else if (key == "show_fps") {
        v.show_fps = i != 0;
    } else if (key == "fps_hud_show_fps") { // [extra]
        v.fps_hud_show_fps = i != 0; // [extra]
    } else if (key == "fps_hud_show_frametime") { // [extra]
        v.fps_hud_show_frametime = i != 0; // [extra]
    } else if (key == "fps_hud_show_cpu") { // [extra]
        v.fps_hud_show_cpu = i != 0; // [extra]
    } else if (key == "fps_hud_show_gpu") { // [extra]
        v.fps_hud_show_gpu = i != 0; // [extra]
    } else if (key == "fps_hud_show_ram") { // [extra]
        v.fps_hud_show_ram = i != 0; // [extra]
    } else if (key == "fps_hud_show_vram") { // [extra]
        v.fps_hud_show_vram = i != 0; // [extra]
    } else if (key == "fps_hud_show_graph") { // [extra]
        v.fps_hud_show_graph = i != 0; // [extra]
    } else if (key == "fps_hud_position") { // [extra]
        v.fps_hud_position = std::clamp(i, 0, 3); // [extra]
    } else if (key == "fps_hud_layout") { // [extra]
        v.fps_hud_layout = std::clamp(i, 0, 1); // [extra]
    } else if (key == "fps_hud_bg_opacity") { // [extra]
        v.fps_hud_bg_opacity = Clamp(f, 0.0f, 1.0f); // [extra]
    } else if (key == "fps_hud_scale") { // [extra]
        v.fps_hud_scale = Clamp(f, 0.5f, 3.0f); // [extra]
    } else if (key == "fps_hud_preset") { // [extra]
        v.fps_hud_preset = std::clamp(i, 0, 4); // [extra]
    } else if (key == "fps_hud_show_shaders") { // [extra]
        v.fps_hud_show_shaders = i != 0; // [extra]
    } else if (key == "fps_hud_graph_thickness") { // [extra]
        v.fps_hud_graph_thickness = Clamp(f, 0.5f, 5.0f); // [extra]
    } else if (key == "fps_hud_graph_height") { // [extra]
        v.fps_hud_graph_height = Clamp(f, 16.0f, 120.0f); // [extra]
    } else if (key == "fps_hud_graph_width") { // [extra]
        v.fps_hud_graph_width = Clamp(f, 100.0f, 600.0f); // [extra]
    } else if (key == "show_info_hud") {
        v.show_info_hud = i != 0;
    } else if (key == "info_hud_pos") {
        v.info_hud_pos = std::clamp(i, 0, 3);
    } else if (key == "info_hud_show_playtime") {
        v.info_hud_show_playtime = i != 0;
    } else if (key == "info_hud_show_map") {
        v.info_hud_show_map = i != 0;
    } else if (key == "info_hud_show_name") {
        v.info_hud_show_name = i != 0;
    } else if (key == "info_hud_show_coop") {
        v.info_hud_show_coop = i != 0;
    } else if (key == "info_hud_show_deaths") {
        v.info_hud_show_deaths = i != 0;
    } else if (key == "discord_rpc") {
        v.discord_rpc = i != 0;
    } else if (key == "discord_rpc_show_character") {
        v.discord_rpc_show_character = i != 0;
        v.discord_rpc_show_name = i != 0;
        v.discord_rpc_show_level = i != 0;
    } else if (key == "discord_rpc_show_name") {
        v.discord_rpc_show_name = i != 0;
    } else if (key == "discord_rpc_show_level") {
        v.discord_rpc_show_level = i != 0;
    } else if (key == "discord_rpc_show_deaths") {
        v.discord_rpc_show_deaths = i != 0;
    } else if (key == "discord_rpc_deaths_mode") {
        v.discord_rpc_deaths_mode = i;
        v.discord_rpc_show_deaths = i != 0;
    } else if (key == "discord_rpc_show_area") {
        v.discord_rpc_show_area = i != 0;
    } else if (key == "discord_rpc_show_time") {
        v.discord_rpc_show_time = i != 0;
    } else if (key == "discord_rpc_show_party") {
        v.discord_rpc_show_party = i != 0;
    } else if (key == "discord_rpc_show_boss") {
        v.discord_rpc_show_boss = i != 0;
    } else if (key == "discord_client_id") {
        if (!value.empty()) {
            v.discord_client_id = value;
        }
    } else if (key == "vbao") {
        v.vbao = i != 0;
    } else if (key == "camera_speed_x") {
        v.camera_speed[1] = Clamp(f, 0.25f, 2.0f);
    } else if (key == "camera_speed_y") {
        v.camera_speed[0] = Clamp(f, 0.25f, 2.0f);
    } else if (key == "menu_pos") {
        float x = -1.0f, y = -1.0f;
        if (std::sscanf(value.c_str(), "%f,%f", &x, &y) == 2 && x >= 0.0f && x <= 1.0f && y >= 0.0f &&
            y <= 1.0f) {
            v.menu_x = x;
            v.menu_y = y;
        }
    } else if (key == "fsr4_auto_exposure") {
        v.fsr4_auto_exposure = i != 0;
    } else if (key == "fsr4_invert_jitter") {
        v.fsr4_invert_jitter = i != 0;
    } else if (key == "puddle_reflections") {
        v.puddle_reflections = i != 0;
    } else if (key == "draw_pipe") {
        if (value == "off" || value == "0" || value == "false") {
            v.draw_pipe = DrawPipeOff;
        } else if (value == "on" || value == "1" || value == "true") {
            v.draw_pipe = DrawPipeOn;
        } else {
            v.draw_pipe = DrawPipeHybrid;
        }
    } else if (key == "model_lod") {
        v.model_lod = std::clamp(i, -2, 2);
    } else if (key == "light_grid_opt") {
        v.light_grid_opt = i != 0;
    } else if (key == "frame_generation") {
        // "1" and "0" from earlier versions and scripts: FSR 3.1 and off.
        v.frame_generation = i != 0 ? FrameGenFsr3 : FrameGenOff;
        for (int g = 0; g < FrameGenCount; ++g) {
            if (value == FrameGenName(g)) v.frame_generation = g;
        }
    } else if (key == "frame_generation_multiplier") {
        v.frame_generation_multiplier = std::clamp(i, 2, 4);
    } else if (key == "low_latency") {
        v.low_latency = std::clamp(i, 0, LowLatencyCount - 1);
        for (int m = 0; m < LowLatencyCount; ++m) {
            if (value == LowLatencyName(m)) v.low_latency = m;
        }
    } else if (key == "live_resolution") {
        v.live_resolution = value == "auto" ? -1 : std::clamp(i, 0, 1);
    } else if (key == "output_res") {
        int w = 0, h = 0;
        v.output_auto = value == "auto";
        if (std::sscanf(value.c_str(), "%dx%d", &w, &h) == 2 && w >= 320 && h >= 200 && w <= 16384 &&
            h <= 16384) {
            v.output_res = PackSize({w, h});
        }
    } else if (key == "fsr4_model") {
        for (int m = 0; m < Fsr4ModelCount; ++m) {
            if (value == Fsr4ModelName(m)) v.fsr4_model = m;
        }
    } else if (key == "texture_quality") {
        for (int q = 0; q < TqCount; ++q) {
            if (value == TextureQualityName(q)) v.texture_quality = q;
        }
    } else if (key == "anti_aliasing") {
        anti_aliasing_seen = true;
        for (int m = 0; m < AaCount; ++m) {
            if (value == AntiAliasingName(m)) v.anti_aliasing = m;
        }
    } else if (key == "aspect_ratio") {
        v.aspect = AspectAuto;
        for (int a = 0; a < AspectCount; ++a) {
            if (value == AspectKey(a)) v.aspect = a;
        }
    } else if (key == "mouse_sensitivity") {
        v.mouse_sensitivity = Clamp(f, 0.05f, 10.0f);
    } else if (key == "mouse_invert_y") {
        v.mouse_invert_y = i != 0;
    } else if (key == "mouse_invert_x") {
        v.mouse_invert_x = i != 0;
    } else if (key == "mouse_capture") {
        v.mouse_capture = i != 0;
    } else if (key == "raw_mouse_camera") {
        v.raw_mouse_camera = i != 0;
    } else if (key == "mouse_hold_auto_rotate") {
        v.mouse_hold_auto_rotate = i != 0;
    } else if (key == "disable_gyro") {
        v.disable_gyro = i != 0;
    } else if (key == "prompt_layout" || key == "button_prompts_layout") {
        if (value == "playstation" || value == "ps" || value == "ps4" || value == "ps5" || value == "1") {
            v.prompt_layout = PromptLayoutPlayStation;
        } else if (value == "xbox" || value == "xinput" || value == "2") {
            v.prompt_layout = PromptLayoutXbox;
        } else if (value == "nintendo" || value == "switch" || value == "3") {
            v.prompt_layout = PromptLayoutNintendo;
        } else if (value == "kbm" || value == "keyboard" || value == "4") {
            v.prompt_layout = PromptLayoutKbm;
        } else {
            v.prompt_layout = PromptLayoutAuto;
        }
    } else if (key == "bloom_scale") {
        v.bloom_scale = Clamp(f, 0.0f, 2.0f);
    } else if (key == "color_saturation") {
        v.color_saturation = Clamp(f, 0.0f, 2.0f);
    } else if (key == "vignette") {
        v.vignette = i != 0;
    } else if (key == "ao_strength") {
        v.ao_strength = Clamp(f, 0.0f, 2.0f);
    } else if (key == "shadow_distance") {
        v.shadow_distance = Clamp(f, 0.25f, 4.0f);
    } else if (key == "fog_opacity") {
        v.fog_opacity = Clamp(f, 0.0f, 2.0f);
    } else if (key == "fov_scale") {
        v.fov_scale = Clamp(f, 0.5f, 2.0f);
    } else if (key == "cam_distance") {
        v.cam_distance = Clamp(f, 0.5f, 2.0f);
    } else if (key == "cam_height") {
        v.cam_height = Clamp(f, 0.0f, 2.0f);
    } else if (key == "light_grid_opt") {
        v.light_grid_opt = i != 0;
    } else if (key == "hide_hud") {
        v.hide_hud = i != 0;
    } else if (key == "fps_limit") {
        v.fps_limit = std::max(0, i);
    } else {
        for (int e = 0; e < EffectCount; ++e) {
            if (key == Effects[e].key) {
                v.effects[e] = i != 0;
            }
        }
    }
}

} // namespace

namespace {
struct Group {
    const char* key;
    double ratio;
    std::array<OutputSize, 10> sizes;
    int count;
};
constexpr Group Groups[AspectCount] = {
    {"auto", 0.0, {}, 0},
    {"4:3", 4.0 / 3.0,
     {{{640, 480}, {800, 600}, {1024, 768}, {1280, 960}, {1440, 1080}, {1600, 1200}, {2048, 1536}, {2880, 2160}}}, 8},
    {"16:10", 16.0 / 10.0,
     {{{640, 400}, {960, 600}, {1280, 800}, {1440, 900}, {1680, 1050}, {1920, 1200}, {2560, 1600}, {3840, 2400}}}, 8},
    {"16:9", 16.0 / 9.0,
     {{{640, 360}, {854, 480}, {960, 540}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800},
       {3840, 2160}}}, 9},
    // 2560x1080 and 5120x2160 are 64:27, 3440x1440 43:18, 3840x1600 12:5: each output's own
    // ratio is the camera's.
    {"21:9", 64.0 / 27.0,
     {{{860, 360}, {1280, 540}, {1720, 720}, {2560, 1080}, {3440, 1440}, {3840, 1600}, {5120, 2160}}}, 7},
    {"32:9", 32.0 / 9.0, {{{1280, 360}, {1920, 540}, {2560, 720}, {3840, 1080}, {5120, 1440}, {7680, 2160}}}, 6},
};
std::atomic<std::uint32_t> monitor{0}; // PackSize, 0 until known
} // namespace

int OutputList::IndexOf(OutputSize size) const {
    for (int i = 0; i < count; ++i) {
        if (sizes[i] == size) return i;
    }
    return -1;
}

const char* AspectKey(int aspect) {
    return Groups[std::clamp(aspect, 0, AspectCount - 1)].key;
}

OutputSize Monitor() {
    return UnpackSize(monitor.load(std::memory_order_relaxed));
}

int ResolvedAspect(int aspect) {
    if (aspect > AspectAuto && aspect < AspectCount) return aspect;
    const OutputSize m = Monitor();
    if (m.width <= 0 || m.height <= 0) return Aspect16x9;
    const double ratio = double(m.width) / m.height;
    int best = Aspect16x9;
    for (int a = AspectAuto + 1; a < AspectCount; ++a) {
        if (std::abs(std::log(ratio / Groups[a].ratio)) < std::abs(std::log(ratio / Groups[best].ratio))) {
            best = a;
        }
    }
    return best;
}

OutputList Outputs(int aspect) {
    const Group& g = Groups[ResolvedAspect(aspect)];
    OutputList list;
    for (int i = 0; i < g.count; ++i) list.sizes[list.count++] = g.sizes[i];
    // Auto: the monitor's own size too (another ratio than its group's sizes, 1366x768, ...).
    const OutputSize m = Monitor();
    if (aspect == AspectAuto && m.width >= 640 && m.height >= 360 && m.width <= 7680 &&
        m.height <= 4320 && list.IndexOf(m) < 0) {
        int at = 0;
        while (at < list.count && (list.sizes[at].height < m.height ||
                                   (list.sizes[at].height == m.height && list.sizes[at].width < m.width))) {
            ++at;
        }
        for (int i = list.count; i > at; --i) list.sizes[i] = list.sizes[i - 1];
        list.sizes[at] = m;
        ++list.count;
    }
    return list;
}

OutputSize NearestOutput(int aspect, OutputSize size) {
    const OutputList list = Outputs(aspect);
    if (list.IndexOf(size) >= 0) return size;
    OutputSize best = list.sizes[0];
    for (const OutputSize& s : list) {
        if (std::abs(s.height - size.height) < std::abs(best.height - size.height)) best = s;
    }
    return best;
}

OutputSize CurrentOutput() {
    const auto& v = Get();
    if (v.output_auto.load(std::memory_order_relaxed)) {
        // Auto: the monitor's resolution; before the window knows it, what run.sh found
        // (BB_MONITOR_SIZE: its startup patch was made for that size).
        OutputSize m = Monitor();
        static const OutputSize startup = [] {
            OutputSize s{};
            if (const char* env = bb_env("BB_MONITOR_SIZE")) {
                std::sscanf(env, "%dx%d", &s.width, &s.height);
            }
            return s;
        }();
        if (startup.width >= 640 && startup.height >= 360) m = startup;
        else if (!(m.width >= 640 && m.height >= 360)) m = OutputDefault;
        return {std::min(m.width, 7680), std::min(m.height, 4320)};
    }
    return NearestOutput(v.aspect.load(std::memory_order_relaxed),
                         UnpackSize(v.output_res.load(std::memory_order_relaxed)));
}

std::string SizeText(OutputSize size) {
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

void SetMonitor(int width, int height) {
    const OutputSize m{width, height};
    if (width <= 0 || height <= 0 || m == Monitor()) return;
    monitor.store(PackSize(m), std::memory_order_relaxed);
    auto& v = Get();
    // Auto follows the monitor: its output is kept (and saved: run.sh's startup patch reads it).
    const OutputSize fitted = CurrentOutput();
    if (v.aspect == AspectAuto && !v.output_auto && PackSize(fitted) != v.output_res.load()) {
        std::printf("Output: %s for the monitor (%dx%d, aspect ratio auto)\n", SizeText(fitted).c_str(),
                    width, height);
        v.output_res = PackSize(fitted);
        Save();
    }
}

Values& Get() {
    static Values values;
    return values;
}

static const char* PortugueseMenuText(const char* english) {
    if (!english) return "";
    static const std::unordered_map<std::string_view, const char*> s_pt = {
        // Output / Display
        {"Output", "Saída"},
        {"Aspect ratio", "Formato de tela"},
        {"Resolution", "Resolução"},
        {"Auto", "Automático"},
        {"Auto (monitor)", "Automático (monitor)"},
        {"Auto (by the GPU)", "Automático (pela GPU)"},
        {"Auto (the DLL's choice)", "Automático (escolha da DLL)"},
        {"Auto (the game's without an upscaler)", "Automático (sem upscaler)"},
        {"Size of the final frame and UI; applies at once. The preset sets the scene size "
         "relative to the output: 4K Performance = 1920x1080. Another aspect ratio widens the "
         "view (the UI keeps to the screen edges).",
         "Tamanho do quadro final e interface; aplica-se imediatamente. A predefinição define o tamanho "
         "da cena relativo à saída: 4K Desempenho = 1920x1080. Outro formato de tela amplia a visão "
         "(interface ajustada às bordas)."},
        {"Size of the final frame and UI. The preset sets the scene size relative to "
         "the output: 4K Performance = 1920x1080. Another aspect ratio widens the view (the "
         "UI keeps to the screen edges). Applies after restarting the game.",
         "Tamanho do quadro final e interface. A predefinição define o tamanho da cena relativo à saída: "
         "4K Desempenho = 1920x1080. Outro formato amplia a visão (interface mantida nas bordas). "
         "Aplica-se após reiniciar o jogo."},
        {"The final frame and UI size changes at the next frame boundary. "
         "The preset sets the scene size relative to the output: 4K Performance = 1920x1080. "
         "Another aspect ratio widens the view (the UI keeps to the screen edges). "
         "Changing the size resets the upscaler's history and may cause a brief pause.",
         "O tamanho do quadro final e interface muda na transição do próximo quadro. A predefinição "
         "define a resolução relativa à saída: 4K Desempenho = 1920x1080. Outro formato amplia a visão. "
         "Mudar a resolução redefine o histórico do upscaler e pode causar uma breve pausa."},

        // Upscaler
        {"Temporal upscaler", "Upscaler temporal"},
        {"Off", "Desligado"},
        {"On", "Ligado"},
        {"On + Boost", "Ligado + Boost"},
        {"TAA (native anti-aliasing)", "TAA (anti-aliasing nativo)"},
        {"FSR 1 (spatial, weak GPUs)", "FSR 1 (espacial, GPUs fracas)"},
        {"FSR 3.1 (experimental)", "FSR 3.1 (experimental)"},
        {"FSR 4 (AMD's DLL)", "FSR 4 (DLL da AMD)"},
        {"Upscaler", "Upscaler"},
        {"— AMD's DLLs not found", "— DLLs da AMD não encontradas"},
        {"— not supported by this GPU", "— incompatível com esta GPU"},
        {"— in development", "— em desenvolvimento"},
        {"FSR 4 model", "Modelo do FSR 4"},
        {"— not on this GPU", "— incompatível com esta GPU"},
        {"— not built (launcher)", "— não compilado (launcher)"},
        {"FSR 4 unavailable: %s", "FSR 4 indisponível: %s"},
        {"FSR 4: auto exposure", "FSR 4: autoexposição"},
        {"FSR 4: invert jitter sign", "FSR 4: inverter sinal de jitter"},
        {"Preset", "Predefinição"},
        {"%s (x%.1f, render %dx%d)", "%s (x%.1f, render %dx%d)"},
        {"Active scene render: %d x %d", "Renderização da cena ativa: %d x %d"},
        {"Startup preset: %s", "Predefinição de inicialização: %s"},
        {"Sharpening (RCAS)", "Nitidez (RCAS)"},
        {"Sharpness", "Nitidez"},
        {"Subpixel jitter", "Jitter subpixel"},
        {"Anti-aliasing", "Anti-aliasing"},
        {"FXAA2_GS (the game's)", "FXAA2_GS (do jogo)"},
        {"SMAA (without an upscaler)", "SMAA (sem upscaler)"},
        {"The game's own", "Padrão do jogo"},
        {"Game default", "Padrão do jogo"},

        // Frame Generation & Low Latency
        {"Frame generation", "Geração de quadros"},
        {"Multiplier", "Multiplicador"},
        {"Frame generation: %s", "Geração de quadros: %s"},
        {"— not available here", "— indisponível aqui"},
        {"— on with DLSS frame generation", "— ativado com geração de quadros DLSS"},
        {"Low latency (NVIDIA Reflex)", "Baixa latência (NVIDIA Reflex)"},
        {"Low latency (AMD Anti-Lag 2)", "Baixa latência (AMD Anti-Lag 2)"},

        // Reactive mask
        {"Reactive mask", "Máscara reativa"},
        {"Enable mask", "Ativar máscara"},
        {"Scale", "Escala"},
        {"Threshold", "Limite"},
        {"Maximum", "Máximo"},
        {"Motion debug view", "Depuração de movimento"},
        {"Show mask (debug)", "Exibir máscara (debug)"},
        {"Show motion vectors (debug)", "Exibir vetores de movimento (debug)"},
        {"Show AO only (debug)", "Exibir apenas AO (debug)"},
        {"Character motion vectors", "Vetores de movimento do personagem"},

        // Effects & Detail
        {"Game effects", "Efeitos do jogo"},
        {"Effects", "Efeitos"},
        {"Model detail", "Detalhe dos modelos"},
        {"Highest (-2)", "Mais alto (-2)"},
        {"Higher (-1)", "Alto (-1)"},
        {"Default (0)", "Padrão (0)"},
        {"Lower (1)", "Baixo (1)"},
        {"Lowest (2)", "Mais baixo (2)"},
        {"Coarse light grid", "Grade de iluminação aproximada"},
        {"GT-VBAO ambient occlusion", "Oclusão de ambiente GT-VBAO"},
        {"Chromatic aberration", "Aberração cromática"},
        {"Depth of field (DoF)", "Profundidade de campo (DoF)"},
        {"Motion blur", "Desfoque de movimento"},
        {"Ambient occlusion (SSAO)", "Oclusão de ambiente (SSAO)"},
        {"Game's own anti-aliasing", "Anti-aliasing próprio do jogo"},
        {"Shadows from dynamic lights", "Sombras de luzes dinâmicas"},
        {"Screen-space reflections (not in original game)", "Reflexos em espaço de tela (SSR)"},
        {"Bloom", "Bloom (Brilho)"},
        {"Skip startup intros", "Pular introduções ao iniciar"},
        {"Free camera (Cross + L3)", "Câmera livre (Cross + L3)"},
        {"Debug menu (requires font files)", "Menu de depuração (requer fontes)"},
        {"Hold Cross and press L3 (keyboard: F + Left Ctrl).", "Segure Cruz e aperte L3 (teclado: F + Ctrl Esquerdo)."},

        // Frame Rate Limit
        {"Frame rate limit", "Limite de taxa de quadros"},
        {"Frame rate limit (now)", "Limite de taxa de quadros (agora)"},
        {"Target FPS", "FPS Alvo"},
        {"Off (display refresh)", "Desligado (taxa do monitor)"},
        {"Off (match display)", "Desligado (taxa do monitor)"},
        {"Display (refresh rate)", "Monitor (taxa de atualização)"},
        {"Match display", "Acompanhar monitor"},
        {"Limit (F3: 30 FPS on and off)", "Limite (F3: 30 FPS liga/desliga)"},

        // System / Process
        {"Apply and restart game", "Aplicar e reiniciar jogo"},
        {"Changes apply after restarting the game", "Alterações entram em vigor após reiniciar o jogo"},

        // Shaders / Diagnostics
        {"Shaders (diagnostics)", "Shaders (diagnósticos)"},
        {"Compiled this session", "Compilados nesta sessão"},
        {"Made in the background", "Compilados em segundo plano"},
        {", recipes importing", ", importando receitas"},
        {"Nothing compiled yet", "Nada compilado ainda"},
        {"Ago", "Tempo"},
        {"What", "Tipo"},
        {"Hash", "Hash"},
        {"shader", "shader"},
        {"pipeline (waited)", "pipeline (aguardou)"},
        {"compute pipeline", "pipeline compute"},
        {"pipeline (background)", "pipeline (segundo plano)"},
    };

    auto it = s_pt.find(english);
    if (it != s_pt.end()) {
        return it->second;
    }
    return english;
}

const char* MenuText(const char* english, const char* portuguese, const char* russian) {
    const int lang = Get().menu_language.load();
    if (lang == LangPortuguese) return portuguese;
    if (lang == LangRussian) return russian;
    return english;
}

const char* MenuText(const char* english, const char* russian) {
    const int lang = Get().menu_language.load();
    if (lang == LangPortuguese) {
        return PortugueseMenuText(english);
    }
    return (lang == LangRussian) ? russian : english;
}

void Load() {
    auto& v = Get();
    for (int e = 0; e < EffectCount; ++e) {
        v.effects[e] = Effects[e].default_on;
    }
    // bbport: the menu's language until one is chosen in it (menu_language in bbport.ini):
    // first the game's selected language (BB_LANGUAGE), then the launcher's (BB_MENU_LANGUAGE),
    // else the system's locale.
    {
        const char* bb_lang = std::getenv("BB_LANGUAGE");
        if (bb_lang && *bb_lang) {
            int id = std::atoi(bb_lang);
            if (id == 17 || id == 7) {
                v.menu_language = LangPortuguese;
            } else if (id == 8) {
                v.menu_language = LangRussian;
            } else {
                v.menu_language = LangEnglish;
            }
        } else {
            const char* lang = bb_env("BB_MENU_LANGUAGE");
            if (!lang || !*lang) {
                for (const char* key : {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"}) {
                    if (const char* value = bb_env(key); value && *value) {
                        lang = value;
                        break;
                    }
                }
            }
            if (lang) {
                if (std::strstr(lang, "pt") || std::strstr(lang, "PT")) {
                    v.menu_language = LangPortuguese;
                } else if (std::strstr(lang, "ru") || std::strstr(lang, "RU")) {
                    v.menu_language = LangRussian;
                } else {
                    v.menu_language = LangEnglish;
                }
            } else {
                v.menu_language = LangPortuguese;
            }
        }
    }
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), file)) {
            std::string text{line};
            text.erase(text.find_last_not_of(" \t\r\n") + 1);
            const auto eq = text.find('=');
            if (text.empty() || text[0] == '#' || eq == std::string::npos) {
                continue;
            }
            Set(v, text.substr(0, eq), text.substr(eq + 1));
        }
        std::fclose(file);
        std::printf("Settings: %s\n", Path());
    }
    // Environment overrides (scripts, A/B tests).
    if (const char* env = bb_env("BB_UPSCALER")) {
        v.upscaler = UpscalerOff;
        for (int u = 0; u < UpscalerCount; ++u) {
            if (std::strcmp(env, UpscalerName(u)) == 0) v.upscaler = u;
        }
    }
    const std::pair<const char*, const char*> env_keys[] = {
        {"BB_FSR_SHARPNESS", "sharpness"},        {"BB_JITTER", "jitter"},
        {"BB_REACTIVE", "reactive"},              {"BB_REACTIVE_SCALE", "reactive_scale"},
        {"BB_REACTIVE_THRESHOLD", "reactive_threshold"}, {"BB_REACTIVE_MAX", "reactive_max"},
        {"BB_UPSCALE_PRESET", "preset"},            {"BB_OBJECT_MOTION", "object_motion"},
        {"BB_DRAW_PIPE", "draw_pipe"},              {"BB_FRAME_GEN", "frame_generation"},
        {"BB_FRAME_GEN_MULTIPLIER", "frame_generation_multiplier"}, {"BB_LOW_LATENCY", "low_latency"},
        {"BB_FPS_LIMIT", "fps_limit"},              {"BB_MODEL_LOD", "model_lod"},
        {"BB_SHADOW_DISTANCE", "shadow_distance"},  {"BB_LIGHT_GRID_OPT", "light_grid_opt"},
        {"BB_EFFECT_SSR", "effect_ssr"},            {"BB_EFFECT_SSAO", "effect_ssao"},
        {"BB_EFFECT_DOF", "effect_dof"},            {"BB_EFFECT_MOTION_BLUR", "effect_motion_blur"},
        {"BB_EFFECT_BLOOM", "effect_bloom"},        {"BB_EFFECT_DYNAMIC_SHADOWS", "effect_dynamic_shadows"},
    };
    for (const auto& [env, key] : env_keys) {
        if (const char* value = std::getenv(env)) {
            Set(v, key, value);
        }
    }
    v.startup_preset = v.preset;
    // Settings from before anti_aliasing: the game's AA switched off stays off.
    if (!anti_aliasing_seen && !v.effects[GameAaEffect]) {
        v.anti_aliasing = AaOff;
    }
    SyncAntiAliasing();
    v.startup_upscaler = v.upscaler;
    v.startup_object_motion = v.object_motion;
    for (int e = 0; e < EffectCount; ++e) {
        v.startup_effects[e] = v.effects[e];
    }
    v.startup_model_lod = v.model_lod;
    if (FixedRenderSession()) {
        int w = 0, h = 0;
        const char* output = bb_env("BB_OUTPUT_RES");
        if (output && std::sscanf(output, "%dx%d", &w, &h) == 2) v.startup_output = {w, h};
    }
    v.startup_live_resolution = v.live_resolution;
    v.startup_draw_pipe = v.draw_pipe.load();
    v.startup_frame_generation = v.frame_generation.load();
}

void ConfigureUpscalerSupport(bool fsr4, bool fsr411, bool fsr411_fp8, bool fsr411_fp8emu) {
    auto& v = Get();
    v.fsr4_supported = fsr4;
    v.fsr411_supported = fsr4 && fsr411;
    v.fsr411_fp8 = fsr411_fp8;
    v.fsr411_fp8emu = fsr411_fp8emu;
    const int requested = v.upscaler;
    if ((requested == UpscalerFsr4 && !v.fsr4_supported) ||
        (requested == UpscalerFsr411 && !v.fsr411_supported)) {
        v.fsr4_problem = "GPU does not support the selected FSR 4 shaders; using FSR 3.1";
        std::printf("Upscaler: %s unsupported on this GPU; falling back to FSR 3.1 before the first frame\n",
                    UpscalerName(requested));
        v.upscaler = UpscalerFsr3;
    }
}

void ConfigureFsrAmdSupport(bool available) {
    auto& v = Get();
    v.fsr_amd_supported = available;
    if (v.upscaler == UpscalerFsrAmd && !available) {
        std::printf("Upscaler: AMD's FSR DLLs not found (amd_fidelityfx_loader_vk.dll); "
                    "falling back to FSR 3.1\n");
        v.upscaler = UpscalerFsr3;
    }
}

void ConfigureDlssSupport(bool available, const char* problem) {
    auto& v = Get();
    v.dlss_supported = available;
    static std::string kept;
    kept = problem ? problem : "";
    v.dlss_problem = available || kept.empty() ? nullptr : kept.c_str();
    if (v.upscaler == UpscalerDlss && !available) {
        std::printf("Upscaler: DLSS unavailable (%s); falling back to FSR 3.1\n",
                    kept.empty() ? "the DLSS bridge or NVIDIA's DLSS library is missing" : kept.c_str());
        v.upscaler = UpscalerFsr3;
    }
}

bool FixedRenderSession() {
    const char* size = bb_env("BB_RENDER_RES");
    return size && size[0];
}

const char* Fsr4ModelName(int model) {
    static constexpr const char* names[Fsr4ModelCount] = {"auto", "fp8", "int8", "int8_rdna2", "int8_lite"};
    return names[std::clamp(model, 0, Fsr4ModelCount - 1)];
}

const char* TextureQualityName(int quality) {
    static constexpr const char* names[TqCount] = {"auto", "full", "half", "quarter"};
    return names[std::clamp(quality, 0, TqCount - 1)];
}

const char* AntiAliasingName(int mode) {
    static constexpr const char* names[AaCount] = {"auto", "game", "fxaa", "off", "smaa"};
    return names[std::clamp(mode, 0, AaCount - 1)];
}

void SyncAntiAliasing() {
    auto& v = Get();
    const int mode = v.anti_aliasing.load(std::memory_order_relaxed);
    const bool upscaling = v.upscaler.load(std::memory_order_relaxed) != UpscalerOff;
    // FXAA2_GS runs in the game's AA step: the step stays on.
    v.effects[GameAaEffect] = mode == AaGame || mode == AaFxaa || (mode == AaAuto && !upscaling);
    v.fxaa_forced = mode == AaFxaa;
}

bool HybridSession() {
    return FixedRenderSession() && bb_env_on("BB_LIVE_HYBRID");
}

int RenderPreset() {
    const auto& v = Get();
    return FixedRenderSession() && !HybridSession() ? v.startup_preset :
        v.upscaler == UpscalerTaa ? NativeAA : v.preset.load();
}

bool ResolutionNeedsRestart() {
    const auto& v = Get();
    // A GPU that cannot reduce the scene live (scene_proxies): presets and the output at the next
    // start, where run.sh patches the game's targets for them.
    if (!v.scene_proxies.load(std::memory_order_relaxed) &&
        (v.preset != v.startup_preset ||
         (v.startup_output.width != 0 && PackSize(CurrentOutput()) != PackSize(v.startup_output)))) {
        return true;
    }
    // TAA needs the live path (native guest targets): run.sh selects it on restart.
    // Over the startup patch (HybridSession) presets and the output apply at once (proxies,
    // larger than the game's targets too); the upscaler's kind needs the next start.
    const bool preset_restart = !HybridSession() && v.preset != v.startup_preset;
    // Hybrid: another output applies at once too (the scene within the game's targets).
    return FixedRenderSession() &&
        (preset_restart || (!HybridSession() && CurrentOutput() != v.startup_output) ||
         (v.upscaler == UpscalerOff) != (v.startup_upscaler == UpscalerOff) ||
         (v.upscaler == UpscalerTaa) != (v.startup_upscaler == UpscalerTaa));
}

void Save() {
    const auto& v = Get();
    // The keys the menu writes, in this order; the rest of the file stays as it is: the
    // launcher's controls (key.* / pad.*) and its other keys, comments.
    std::vector<std::pair<std::string, std::string>> keys;
    const auto put = [&](const char* key, std::string value) { keys.emplace_back(key, std::move(value)); };
    const auto fixed = [](float value, int digits) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.*f", digits, value);
        return std::string(text);
    };
    const auto flag = [](bool value) { return std::string(value ? "1" : "0"); };
    if (v.menu_language_chosen) {
        put("menu_language", LanguageCode(v.menu_language.load()));
    }
    put("upscaler", UpscalerName(v.upscaler));
    put("preset", std::to_string(v.preset.load()));
    put("sharpen", flag(v.sharpen));
    put("sharpness", fixed(v.sharpness, 2));
    put("jitter", flag(v.jitter));
    put("reactive", flag(v.reactive));
    put("object_motion", flag(v.object_motion));
    put("reactive_scale", fixed(v.reactive_scale, 2));
    put("reactive_threshold", fixed(v.reactive_threshold, 2));
    put("reactive_max", fixed(v.reactive_max, 2));
    put("debug_view", std::to_string(v.debug_view.load()));
    put("show_fps", flag(v.show_fps));
    put("fps_hud_show_fps", flag(v.fps_hud_show_fps)); // [extra]
    put("fps_hud_show_frametime", flag(v.fps_hud_show_frametime)); // [extra]
    put("fps_hud_show_cpu", flag(v.fps_hud_show_cpu)); // [extra]
    put("fps_hud_show_gpu", flag(v.fps_hud_show_gpu)); // [extra]
    put("fps_hud_show_ram", flag(v.fps_hud_show_ram)); // [extra]
    put("fps_hud_show_vram", flag(v.fps_hud_show_vram)); // [extra]
    put("fps_hud_show_graph", flag(v.fps_hud_show_graph)); // [extra]
    put("fps_hud_position", std::to_string(v.fps_hud_position.load())); // [extra]
    put("fps_hud_layout", std::to_string(v.fps_hud_layout.load())); // [extra]
    put("fps_hud_bg_opacity", fixed(v.fps_hud_bg_opacity, 2)); // [extra]
    put("fps_hud_scale", fixed(v.fps_hud_scale, 2)); // [extra]
    put("fps_hud_preset", std::to_string(v.fps_hud_preset.load())); // [extra]
    put("fps_hud_show_shaders", flag(v.fps_hud_show_shaders)); // [extra]
    put("fps_hud_graph_thickness", fixed(v.fps_hud_graph_thickness, 2)); // [extra]
    put("fps_hud_graph_height", fixed(v.fps_hud_graph_height, 1)); // [extra]
    put("fps_hud_graph_width", fixed(v.fps_hud_graph_width, 1)); // [extra]
    put("show_info_hud", flag(v.show_info_hud));
    put("info_hud_pos", std::to_string(v.info_hud_pos.load()));
    put("info_hud_show_playtime", flag(v.info_hud_show_playtime));
    put("info_hud_show_map", flag(v.info_hud_show_map));
    put("info_hud_show_name", flag(v.info_hud_show_name));
    put("info_hud_show_coop", flag(v.info_hud_show_coop));
    put("info_hud_show_deaths", flag(v.info_hud_show_deaths));
    put("discord_rpc", flag(v.discord_rpc));
    put("discord_rpc_show_character", flag(v.discord_rpc_show_name && v.discord_rpc_show_level));
    put("discord_rpc_show_name", flag(v.discord_rpc_show_name));
    put("discord_rpc_show_level", flag(v.discord_rpc_show_level));
    put("discord_rpc_show_deaths", flag(v.discord_rpc_show_deaths));
    put("discord_rpc_show_area", flag(v.discord_rpc_show_area));
    put("discord_rpc_show_time", flag(v.discord_rpc_show_time));
    put("discord_rpc_show_party", flag(v.discord_rpc_show_party));
    put("discord_rpc_show_boss", flag(v.discord_rpc_show_boss));
    put("discord_client_id", v.discord_client_id);
    put("vbao", flag(v.vbao));
    put("camera_speed_x", fixed(v.camera_speed[1], 2));
    put("camera_speed_y", fixed(v.camera_speed[0], 2));
    put("fsr4_auto_exposure", flag(v.fsr4_auto_exposure));
    put("fsr4_invert_jitter", flag(v.fsr4_invert_jitter));
    put("puddle_reflections", flag(v.puddle_reflections.load()));
    put("draw_pipe", std::to_string(int(v.draw_pipe.load())));
    // Read by patches.py at start.
    for (int e = 0; e < EffectCount; ++e) {
        put(Effects[e].key, flag(v.effects[e]));
    }
    put("model_lod", std::to_string(v.model_lod.load()));
    put("light_grid_opt", flag(v.light_grid_opt));
    put("anti_aliasing", AntiAliasingName(v.anti_aliasing));
    put("texture_quality", TextureQualityName(v.texture_quality));
    put("fsr4_model", Fsr4ModelName(v.fsr4_model));
    put("aspect_ratio", AspectKey(v.aspect));
    put("output_res", v.output_auto ? std::string{"auto"} : SizeText(CurrentOutput()));
    // Read by run.sh at start.
    put("live_resolution", v.live_resolution < 0 ? "auto" : flag(v.live_resolution != 0));
    put("frame_generation", FrameGenName(v.frame_generation));
    put("frame_generation_multiplier", std::to_string(v.frame_generation_multiplier.load()));
    put("low_latency", LowLatencyName(v.low_latency));
    put("mouse_sensitivity", fixed(v.mouse_sensitivity.load(), 2));
    put("mouse_invert_y", flag(v.mouse_invert_y.load()));
    put("mouse_invert_x", flag(v.mouse_invert_x.load()));
    put("mouse_capture", flag(v.mouse_capture.load()));
    put("raw_mouse_camera", flag(v.raw_mouse_camera.load()));
    put("mouse_hold_auto_rotate", flag(v.mouse_hold_auto_rotate.load()));
    put("disable_gyro", flag(v.disable_gyro.load()));
    {
        const int pl = v.prompt_layout.load();
        const char* layout_str = pl == PromptLayoutPlayStation ? "playstation" :
                                 pl == PromptLayoutXbox ? "xbox" :
                                 pl == PromptLayoutNintendo ? "nintendo" :
                                 pl == PromptLayoutKbm ? "keyboard" : "auto";
        put("prompt_layout", layout_str);
    }
    put("bloom_scale", fixed(v.bloom_scale.load(), 2));
    put("color_saturation", fixed(v.color_saturation.load(), 2));
    put("vignette", flag(v.vignette.load()));
    put("ao_strength", fixed(v.ao_strength.load(), 2));
    put("shadow_distance", fixed(v.shadow_distance.load(), 2));
    put("fog_opacity", fixed(v.fog_opacity.load(), 2));
    put("fov_scale", fixed(v.fov_scale.load(), 2));
    put("cam_distance", fixed(v.cam_distance.load(), 2));
    put("cam_height", fixed(v.cam_height.load(), 2));
    put("light_grid_opt", flag(v.light_grid_opt.load()));
    put("hide_hud", flag(v.hide_hud.load()));
    if (v.menu_x >= 0.0f && v.menu_y >= 0.0f) {
        put("menu_pos", fixed(v.menu_x, 4) + "," + fixed(v.menu_y, 4));
    }

    std::string out;
    bool had_lines = false;
    std::vector<bool> written(keys.size());
    if (FILE* file = std::fopen(Path(), "r")) {
        char line[512];
        while (std::fgets(line, sizeof(line), file)) {
            had_lines = true;
            std::string text{line};
            text.erase(text.find_last_not_of("\r\n") + 1);
            const auto eq = text.find('=');
            if (!text.empty() && text[0] != '#' && eq != std::string::npos) {
                std::string key = text.substr(0, eq);
                key.erase(key.find_last_not_of(" \t") + 1);
                key.erase(0, key.find_first_not_of(" \t"));
                const auto it = std::find_if(keys.begin(), keys.end(),
                                             [&](const auto& k) { return k.first == key; });
                if (it != keys.end()) {
                    const size_t idx = size_t(it - keys.begin());
                    if (!written[idx]) {
                        out += it->first + "=" + it->second + "\n";
                        written[idx] = true;
                    }
                    continue;
                }
            }
            out += text + "\n";
        }
        std::fclose(file);
    }
    if (!had_lines) {
        out = "# bbport settings (in-game menu: Insert / L3+R3)\n";
    }
    for (size_t k = 0; k < keys.size(); ++k) {
        if (!written[k]) {
            out += keys[k].first + "=" + keys[k].second + "\n";
        }
    }
    // Replaced whole: a crash while writing leaves the old file. filesystem::rename replaces an
    // existing file on Windows too (std::rename does not there: the menu's changes were lost).
    const std::string temporary = std::string(Path()) + ".tmp";
    FILE* file = std::fopen(temporary.c_str(), "w");
    bool ok = file && std::fwrite(out.data(), 1, out.size(), file) == out.size();
    ok = file && std::fclose(file) == 0 && ok;
    std::error_code error;
    if (ok) {
        std::filesystem::rename(temporary, Path(), error);
    }
    if (!ok || error) {
        std::printf("Settings: cannot write %s\n", Path());
    }
}

float PresetScale(int preset) {
    static constexpr float scales[PresetCount] = {1.0f, 1.5f, 1.7f, 2.0f, 3.0f};
    return scales[std::clamp(preset, 0, PresetCount - 1)];
}

const char* PresetName(int preset) {
    static constexpr const char* names[PresetCount] = {"Native AA", "Quality", "Balanced",
                                                       "Performance", "Ultra Performance"};
    return names[std::clamp(preset, 0, PresetCount - 1)];
}

const char* UpscalerName(int upscaler) {
    static constexpr const char* names[UpscalerCount] = {"off", "fsr3", "fsr4", "fsr411", "taa", "dlss", "fsr1",
                                                                "fsr_amd"};
    return names[std::clamp(upscaler, 0, UpscalerCount - 1)];
}

const char* LanguageCode(int lang) {
    static constexpr const char* codes[LangCount] = {"en", "pt_br", "ru"};
    return codes[std::clamp(lang, 0, LangCount - 1)];
}

const char* LanguageName(int lang) {
    static constexpr const char* names[LangCount] = {"English", "Português (Brasil)", "Русский"};
    return names[std::clamp(lang, 0, LangCount - 1)];
}

const char* EffectLabel(int effect_index, int lang) {
    return BbStrings::EffectLabel(effect_index, lang);
}

const char* LowLatencyName(int mode) {
    static constexpr const char* names[LowLatencyCount] = {"off", "on", "boost"};
    return names[std::clamp(mode, 0, LowLatencyCount - 1)];
}

int EffectiveLowLatency() {
    const auto& v = Get();
    const int mode = std::clamp(v.low_latency.load(std::memory_order_relaxed), 0, LowLatencyCount - 1);
    const bool dlss_frame_gen = v.frame_generation.load(std::memory_order_relaxed) == FrameGenDlss &&
                                v.dlssg_max_multiplier.load(std::memory_order_relaxed) > 1;
    return dlss_frame_gen && mode == LowLatencyOff ? int(LowLatencyOn) : mode;
}

const char* FrameGenName(int frame_gen) {
    static constexpr const char* names[FrameGenCount] = {"off", "fsr3", "dlss", "fsr_amd"};
    return names[std::clamp(frame_gen, 0, FrameGenCount - 1)];
}

} // namespace BbSettings
