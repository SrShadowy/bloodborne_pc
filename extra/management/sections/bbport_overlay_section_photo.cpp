// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Photo Mode controls and the live visual/camera settings (post-processing, camera,
// light grid, mouse camera).
#include "bbport_overlay_sections.h"

#include <cstdlib>
#include <string>

#include "bbport_overlay_ui.h"
#include "bbport_strings.h"
#include "common/path_util.h"

#include <imgui.h>

namespace BbOverlaySections {
namespace {

/// The Photo Mode sliders' neutral value: the game's own look (1 = as shipped).
constexpr float kNeutralScale = 1.0f;

/// Opens the screenshots folder in the desktop's file manager.
void OpenScreenshotsFolder() {
    const auto path = Common::FS::GetUserPath(Common::FS::PathType::ScreenshotsDir).string();
    const std::string command = "xdg-open \"" + path + "\" &";
    (void)std::system(command.c_str());
}

const char* Str(BbStrings::StringId id, int lang) {
    return BbStrings::Get(id, lang);
}

} // namespace

void RenderPhotoModeSection(BbSettings::Values& s, int lang, bool& dirty) {
    ImGui::SeparatorText(Tr(lang, "Photo Mode", "Modo Foto (Photo Mode)", "Фоторежим (Photo Mode)"));

    // Freeze the world: AI, animation and physics stop, camera and rendering keep running.
    Checkbox(Tr(lang, "Pause Game (Freeze World / AI) (F10)", "Pausar Jogo (Congelar IA / Mundo) (F10)",
                 "Пауза (Заморозить мир / ИИ) (F10)"),
             s.photo_pause, dirty);
    Hint(Tr(lang, "Freezes AI, animations, physics, and world timers while keeping camera and rendering active.",
             "Congela IA, animacoes, fisica e particulas mantendo a camera e renderizacao ativas.",
             "Останавливает анимации, физику и таймеры, сохраняя управление камерой."));

    Checkbox(Tr(lang, "Hide Game HUD (F11)", "Ocultar HUD do Jogo (F11)", "Скрыть интерфейс игры (F11)"),
             s.hide_hud, dirty);
    Hint(Tr(lang, "Hides health bars, stamina, reticle, and subtitles for clean framing.",
             "Oculta barras de vida, estamina, reticulo e legendas para enquadramento limpo.",
             "Скрывает полоски здоровья, прицел и диалоги для чистого снимка."));

    ImGui::Spacing();

    if (ImGui::Button(Tr(lang, "  Take Photo (F12)  ", "  Tirar Foto (F12)  ", "  Сделать снимок (F12)  "))) {
        s.request_screenshot.store(true, std::memory_order_relaxed);
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr(lang, "Open Screenshots Folder", "Abrir Pasta de Fotos",
                         "Открыть папку со снимками"))) {
        OpenScreenshotsFolder();
    }

    ImGui::Spacing();

    if (ImGui::SmallButton(Tr(lang, "Reset Camera and Effects", "Restaurar Padroes de Camera e Efeitos",
                              "Сбросить камеру и эффекты"))) {
        s.fov_scale = kNeutralScale;
        s.cam_distance = kNeutralScale;
        s.cam_height = kNeutralScale;
        s.bloom_scale = kNeutralScale;
        s.color_saturation = kNeutralScale;
        s.vignette = true;
        s.ao_strength = kNeutralScale;
        s.fog_opacity = kNeutralScale;
        dirty = true;
    }
}

void RenderPostProcessingAndLightingSection(BbSettings::Values& s, int lang, bool& dirty) {
    ImGui::SeparatorText(Str(BbStrings::StringId::SectionPostProcess, lang));
    Slider(Str(BbStrings::StringId::BloomIntensity, lang), s.bloom_scale, 0.0f, 2.0f, dirty);
    Hint(Str(BbStrings::StringId::HintBloom, lang));
    Slider(Str(BbStrings::StringId::ColorSaturation, lang), s.color_saturation, 0.0f, 2.0f, dirty);
    Hint(Str(BbStrings::StringId::HintSaturation, lang));
    Checkbox(Str(BbStrings::StringId::VignetteEnable, lang), s.vignette, dirty);
    Hint(Str(BbStrings::StringId::HintVignette, lang));
    Slider(Str(BbStrings::StringId::SsaoStrength, lang), s.ao_strength, 0.0f, 2.0f, dirty);
    Hint(Str(BbStrings::StringId::HintSsaoStrength, lang));
    Slider(Str(BbStrings::StringId::ShadowDistance, lang), s.shadow_distance, 0.5f, 3.0f, dirty);
    Hint(Str(BbStrings::StringId::HintShadowDistance, lang));
    Slider(Str(BbStrings::StringId::FogOpacity, lang), s.fog_opacity, 0.0f, 2.0f, dirty);
    Hint(Str(BbStrings::StringId::HintFogOpacity, lang));
}

void RenderCameraSection(BbSettings::Values& s, int lang, bool& dirty) {
    ImGui::SeparatorText(Str(BbStrings::StringId::SectionCamera, lang));
    Slider(Str(BbStrings::StringId::CameraFov, lang), s.fov_scale, 0.80f, 1.40f, dirty);
    Hint(Str(BbStrings::StringId::HintCameraFov, lang));
    Slider(Str(BbStrings::StringId::CameraDistance, lang), s.cam_distance, 0.50f, 2.50f, dirty);
    Hint(Str(BbStrings::StringId::HintCameraDistance, lang));
    Slider(Str(BbStrings::StringId::CameraHeight, lang), s.cam_height, 0.50f, 2.00f, dirty);
    Hint(Str(BbStrings::StringId::HintCameraHeight, lang));
}

void RenderLightGridCheckbox(BbSettings::Values& s, int lang, bool& dirty) {
    Checkbox(Str(BbStrings::StringId::LightGridOpt, lang), s.light_grid_opt, dirty);
    Hint(Str(BbStrings::StringId::HintLightGridOpt, lang));
}

void RenderRawMouseCameraControls(BbSettings::Values& s, int lang, bool& dirty) {
    Checkbox(Str(BbStrings::StringId::RawMouseCamera, lang), s.raw_mouse_camera, dirty);
    Hint(Str(BbStrings::StringId::HintRawMouseCamera, lang));
    const bool raw_on = s.raw_mouse_camera.load(std::memory_order_relaxed);
    ImGui::BeginDisabled(!raw_on);
    Checkbox(Str(BbStrings::StringId::MouseHoldAutoRotate, lang), s.mouse_hold_auto_rotate, dirty);
    Hint(Str(BbStrings::StringId::HintMouseHoldAutoRotate, lang));
    ImGui::EndDisabled();
}

} // namespace BbOverlaySections
