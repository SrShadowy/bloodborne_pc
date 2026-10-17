#include "bbport_overlay_sections.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "bbport_discord_rpc.h"
#include "bbport_strings.h"
#include "common/path_util.h"

#include <imgui.h>

namespace BbOverlaySections {
namespace {

void Checkbox(const char* label, std::atomic<bool>& value, bool& dirty) {
    bool v = value.load(std::memory_order_relaxed);
    if (ImGui::Checkbox(label, &v)) {
        value.store(v, std::memory_order_relaxed);
        dirty = true;
    }
}

void Slider(const char* label, std::atomic<float>& value, float lo, float hi, bool& dirty) {
    float v = value.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat(label, &v, lo, hi, "%.2f")) {
        value.store(v, std::memory_order_relaxed);
        dirty = true;
    }
}

void Hint(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace

#define S(id) BbStrings::Get(BbStrings::StringId::id, lang)

void RenderPhotoModeSection(BbSettings::Values& s, int lang, bool& dirty) {
    const char* sec_title = (lang == 1) ? "Modo Foto (Photo Mode)" :
                            (lang == 2) ? "Фоторежим (Photo Mode)" :
                                          "Photo Mode";
    ImGui::SeparatorText(sec_title);

    // 1. Freeze Simulation / World
    const char* freeze_label = (lang == 1) ? "Pausar Jogo (Congelar IA / Mundo)" :
                               (lang == 2) ? "Пауза (Заморозить мир / ИИ)" :
                                             "Pause Game (Freeze World / AI)";
    const char* freeze_hint = (lang == 1) ? "Congela IA, animacoes, fisica e particulas mantendo a camera e renderizacao ativas." :
                              (lang == 2) ? "Останавливает анимации, физику и таймеры, сохраняя управление камерой." :
                                            "Freezes AI, animations, physics, and world timers while keeping camera and rendering active.";
    Checkbox(freeze_label, s.photo_pause, dirty);
    Hint(freeze_hint);

    // 2. Hide Game HUD
    const char* hud_label = (lang == 1) ? "Ocultar HUD do Jogo (F11)" :
                            (lang == 2) ? "Скрыть интерфейс игры (F11)" :
                                          "Hide Game HUD (F11)";
    const char* hud_hint = (lang == 1) ? "Oculta barras de vida, estamina, reticulo e legendas para enquadramento limpo." :
                           (lang == 2) ? "Скрывает полоски здоровья, прицел и диалоги для чистого снимка." :
                                         "Hides health bars, stamina, reticle, and subtitles for clean framing.";
    Checkbox(hud_label, s.hide_hud, dirty);
    Hint(hud_hint);

    ImGui::Spacing();

    // 3. Take Screenshot button
    const char* btn_shoot = (lang == 1) ? "  Tirar Foto (F12)  " :
                            (lang == 2) ? "  Сделать снимок (F12)  " :
                                          "  Take Photo (F12)  ";
    if (ImGui::Button(btn_shoot)) {
        s.request_screenshot.store(true, std::memory_order_relaxed);
    }
    ImGui::SameLine();

    // 4. Open Screenshots Folder button
    const char* btn_folder = (lang == 1) ? "Abrir Pasta de Fotos" :
                             (lang == 2) ? "Открыть папку со снимками" :
                                           "Open Screenshots Folder";
    if (ImGui::Button(btn_folder)) {
        const auto path = Common::FS::GetUserPath(Common::FS::PathType::ScreenshotsDir).string();
        const std::string cmd = "xdg-open \"" + path + "\" &";
        (void)std::system(cmd.c_str());
    }

    ImGui::Spacing();

    // 5. Quick Reset
    const char* btn_reset = (lang == 1) ? "Restaurar Padroes de Camera e Efeitos" :
                            (lang == 2) ? "Сбросить камеру и эффекты" :
                                          "Reset Camera and Effects";
    if (ImGui::SmallButton(btn_reset)) {
        s.fov_scale = 1.0f;
        s.cam_distance = 1.0f;
        s.cam_height = 1.0f;
        s.bloom_scale = 1.0f;
        s.color_saturation = 1.0f;
        s.vignette = true;
        s.ao_strength = 1.0f;
        s.fog_opacity = 1.0f;
        dirty = true;
    }
}

void RenderPostProcessingAndLightingSection(BbSettings::Values& s, int lang, bool& dirty) {
    ImGui::SeparatorText(S(SectionPostProcess));
    Slider(S(BloomIntensity), s.bloom_scale, 0.0f, 2.0f, dirty);
    Hint(S(HintBloom));
    Slider(S(ColorSaturation), s.color_saturation, 0.0f, 2.0f, dirty);
    Hint(S(HintSaturation));
    Checkbox(S(VignetteEnable), s.vignette, dirty);
    Hint(S(HintVignette));
    Slider(S(SsaoStrength), s.ao_strength, 0.0f, 2.0f, dirty);
    Hint(S(HintSsaoStrength));
    Slider(S(ShadowDistance), s.shadow_distance, 0.5f, 3.0f, dirty);
    Hint(S(HintShadowDistance));
    Slider(S(FogOpacity), s.fog_opacity, 0.0f, 2.0f, dirty);
    Hint(S(HintFogOpacity));
}

void RenderCameraSection(BbSettings::Values& s, int lang, bool& dirty) {
    ImGui::SeparatorText(S(SectionCamera));
    Slider(S(CameraFov), s.fov_scale, 0.80f, 1.40f, dirty);
    Hint(S(HintCameraFov));
    Slider(S(CameraDistance), s.cam_distance, 0.50f, 2.50f, dirty);
    Hint(S(HintCameraDistance));
    Slider(S(CameraHeight), s.cam_height, 0.50f, 2.00f, dirty);
    Hint(S(HintCameraHeight));
}

void RenderLightGridCheckbox(BbSettings::Values& s, int lang, bool& dirty) {
    Checkbox(S(LightGridOpt), s.light_grid_opt, dirty);
    Hint(S(HintLightGridOpt));
}

void RenderRawMouseCameraControls(BbSettings::Values& s, int lang, bool& dirty) {
    Checkbox(S(RawMouseCamera), s.raw_mouse_camera, dirty);
    Hint(S(HintRawMouseCamera));
    const bool raw_on = s.raw_mouse_camera.load(std::memory_order_relaxed);
    ImGui::BeginDisabled(!raw_on);
    Checkbox(S(MouseHoldAutoRotate), s.mouse_hold_auto_rotate, dirty);
    Hint(S(HintMouseHoldAutoRotate));
    ImGui::EndDisabled();
}

void RenderInfoHudSection(BbSettings::Values& s, int lang, bool& dirty) {
    const char* sec_title = (lang == 1) ? "HUD de Informações em Tela (Info OSD)" :
                            (lang == 2) ? "Информационный оверлей (Info OSD)" :
                                          "On-Screen Info HUD (OSD)";
    ImGui::SeparatorText(sec_title);

    const char* main_label = (lang == 1) ? "Ativar HUD de Informações" :
                             (lang == 2) ? "Включить оверлей информации" :
                                           "Enable Info HUD Overlay";
    const char* main_hint = (lang == 1) ? "Exibe informações do jogo em tempo real (horas de jogo, mapa atual, nome do caçador e co-op)." :
                            (lang == 2) ? "Показывает время игры, название локации, имя охотника и статус коопа." :
                                          "Displays real-time game information (playtime, current location, character name, and co-op status).";
    Checkbox(main_label, s.show_info_hud, dirty);
    Hint(main_hint);

    const bool hud_active = s.show_info_hud.load(std::memory_order_relaxed);
    ImGui::BeginDisabled(!hud_active);

    const char* pos_label = (lang == 1) ? "Posição na Tela" :
                            (lang == 2) ? "Позиция на экране" :
                                          "Screen Position";
    const char* pos_options_pt[] = {
        "Superior Esquerdo (Padrão)",
        "Superior Direito (Abaixo do FPS)",
        "Inferior Esquerdo",
        "Inferior Direito"
    };
    const char* pos_options_en[] = {
        "Top-Left (Default)",
        "Top-Right (Below FPS)",
        "Bottom-Left",
        "Bottom-Right"
    };
    const char* pos_options_ru[] = {
        "Сверху слева (По умолчанию)",
        "Сверху справа (Под FPS)",
        "Снизу слева",
        "Снизу справа"
    };

    const char** pos_options = (lang == 1) ? pos_options_pt : (lang == 2) ? pos_options_ru : pos_options_en;
    int current_pos = std::clamp(s.info_hud_pos.load(std::memory_order_relaxed), 0, 3);
    if (ImGui::BeginCombo(pos_label, pos_options[current_pos])) {
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(pos_options[i], i == current_pos)) {
                s.info_hud_pos.store(i, std::memory_order_relaxed);
                dirty = true;
            }
        }
        ImGui::EndCombo();
    }

    const char* lbl_name = (lang == 1) ? "Nome e Nível do Personagem" :
                           (lang == 2) ? "Имя и уровень охотника" :
                                         "Character Name & Level";
    Checkbox(lbl_name, s.info_hud_show_name, dirty);

    const char* lbl_map = (lang == 1) ? "Nome do Mapa / Área" :
                          (lang == 2) ? "Название локации" :
                                        "Map / Area Name";
    Checkbox(lbl_map, s.info_hud_show_map, dirty);

    const char* lbl_time = (lang == 1) ? "Horas de Jogo (Total e Sessão)" :
                           (lang == 2) ? "Время игры (Всего и за сессию)" :
                                         "Playtime (Total & Session)";
    Checkbox(lbl_time, s.info_hud_show_playtime, dirty);

    const char* lbl_coop = (lang == 1) ? "Status de Co-op (Online)" :
                           (lang == 2) ? "Статус коопа (Сеть)" :
                                         "Co-op Status (Online)";
    Checkbox(lbl_coop, s.info_hud_show_coop, dirty);

    ImGui::EndDisabled();
}

void RenderDiscordRpcSection(BbSettings::Values& s, int lang, bool& dirty) {
    const char* sec_title = "Discord Rich Presence (RPC)";
    ImGui::SeparatorText(sec_title);

    // Connection status indicator
    const bool is_connected = BbDiscordRpc::IsConnected();
    const bool rpc_enabled = s.discord_rpc.load(std::memory_order_relaxed);
    const char* status_str = BbDiscordRpc::GetStatusText(lang);

    ImVec4 status_col;
    if (!rpc_enabled) {
        status_col = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Gray
    } else if (is_connected) {
        status_col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f); // Bright Green
    } else {
        status_col = ImVec4(1.0f, 0.75f, 0.2f, 1.0f); // Warm Yellow/Orange
    }

    ImGui::TextColored(status_col, "%s", status_str);

    // Main Toggle
    const char* main_label = (lang == 1) ? "Ativar Discord Rich Presence" :
                             (lang == 2) ? "Включить Discord Rich Presence" :
                                           "Enable Discord Rich Presence";
    const char* main_hint = (lang == 1) ? "Mostra no seu perfil do Discord o mapa atual, cacador, nivel, tempo de jogo e modo co-op/solo." :
                            (lang == 2) ? "Показывает в Discord текущую локацию, уровень, имя охотника и статус игры." :
                                          "Displays real-time map location, character name, level, playtime, and co-op status on your Discord profile.";
    Checkbox(main_label, s.discord_rpc, dirty);
    Hint(main_hint);

    ImGui::BeginDisabled(!rpc_enabled);

    const char* lbl_char = (lang == 1) ? "Exibir Nome e Nível do Personagem" :
                           (lang == 2) ? "Показывать имя и уровень охотника" :
                                         "Show Character Name & Level";
    Checkbox(lbl_char, s.discord_rpc_show_character, dirty);

    const char* lbl_area = (lang == 1) ? "Exibir Mapa / Área Atual" :
                           (lang == 2) ? "Показывать локацию / босса" :
                                         "Show Current Map / Area";
    Checkbox(lbl_area, s.discord_rpc_show_area, dirty);

    const char* lbl_time = (lang == 1) ? "Exibir Tempo de Sessão" :
                           (lang == 2) ? "Показывать время сессии" :
                                         "Show Elapsed Session Time";
    Checkbox(lbl_time, s.discord_rpc_show_time, dirty);

    ImGui::EndDisabled();
}

#undef S

} // namespace BbOverlaySections
