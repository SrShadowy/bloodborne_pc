#include "bbport_overlay_sections.h"

#include <cstdlib>
#include <string>

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

#undef S

} // namespace BbOverlaySections
