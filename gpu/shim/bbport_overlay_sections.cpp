#include "bbport_overlay_sections.h"

#include "bbport_strings.h"

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
