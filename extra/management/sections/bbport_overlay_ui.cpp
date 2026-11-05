// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the pieces every settings section draws with -- the text for the current language and
// the widgets bound to a setting (bbport_overlay_ui.h declares them).
#include "bbport_overlay_ui.h"

#include <algorithm>

namespace BbOverlaySections {

const char* Tr(int lang, const char* en, const char* pt, const char* ru) {
    if (lang == BbSettings::LangPortuguese) {
        return pt;
    }
    if (lang == BbSettings::LangRussian && ru != nullptr) {
        return ru;
    }
    return en;
}

const char* const* TrList(int lang, const char* const* en, const char* const* pt,
                          const char* const* ru) {
    if (lang == BbSettings::LangPortuguese) {
        return pt;
    }
    if (lang == BbSettings::LangRussian && ru != nullptr) {
        return ru;
    }
    return en;
}

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

void ComboOptions(const char* label, const char* const* options, int count,
                  std::atomic<int>& value, bool& dirty, float width) {
    const int current = std::clamp(value.load(std::memory_order_relaxed), 0, count - 1);
    if (width > 0.0f) {
        ImGui::SetNextItemWidth(width);
    }
    if (ImGui::BeginCombo(label, options[current])) {
        for (int i = 0; i < count; ++i) {
            const bool selected = (i == current);
            if (ImGui::Selectable(options[i], selected)) {
                value.store(i, std::memory_order_relaxed);
                dirty = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
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

} // namespace BbOverlaySections
