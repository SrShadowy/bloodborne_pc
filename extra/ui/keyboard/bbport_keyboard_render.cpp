// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the virtual keyboard dialog -- the title, the text being typed, the four character rows,
// the action row and the gamepad help. The cursor and the input live in bbport_keyboard.cpp.
#include "bbport_keyboard_internal.h"

#include <algorithm>
#include <array>
#include <string>

#include <imgui.h>

#include "bbport_strings.h"

namespace BbVirtualKeyboard::detail {
namespace {

// The dialog's frame.
constexpr ImVec4 kWindowBg{0.07f, 0.08f, 0.11f, 0.98f};
constexpr ImVec4 kWindowBorder{0.50f, 0.40f, 0.20f, 0.85f};
constexpr ImVec4 kTitleText{0.95f, 0.82f, 0.45f, 1.0f};

// The text box being typed into.
constexpr ImVec4 kInputBg{0.04f, 0.05f, 0.08f, 1.0f};
constexpr ImVec4 kInputBorder{0.60f, 0.50f, 0.25f, 0.80f};
constexpr ImVec4 kInputText{0.96f, 0.90f, 0.65f, 1.0f};

// A key: when the cursor is on it, a luminous gold; otherwise dark slate.
constexpr ImVec4 kSelectedKey{0.82f, 0.62f, 0.18f, 0.95f};
constexpr ImVec4 kSelectedKeyHover{0.92f, 0.72f, 0.25f, 1.0f};
constexpr ImVec4 kSelectedKeyBorder{1.0f, 0.95f, 0.55f, 1.0f};
constexpr ImVec4 kSelectedKeyText{0.08f, 0.06f, 0.02f, 1.0f};
constexpr ImVec4 kKey{0.16f, 0.18f, 0.24f, 0.90f};
constexpr ImVec4 kKeyHover{0.24f, 0.28f, 0.38f, 1.0f};
constexpr ImVec4 kKeyBorder{0.35f, 0.35f, 0.40f, 0.60f};
constexpr ImVec4 kKeyText{0.92f, 0.92f, 0.92f, 1.0f};

// An action key's own colour (Caps uses the gold when it is on, slate when off).
constexpr ImVec4 kActionBorder{0.40f, 0.40f, 0.45f, 0.60f};
constexpr ImVec4 kActionText{0.95f, 0.95f, 0.95f, 1.0f};
constexpr ImVec4 kCapsOn{0.20f, 0.42f, 0.68f, 0.95f};
constexpr ImVec4 kCapsOnHover{0.26f, 0.50f, 0.78f, 1.0f};
constexpr ImVec4 kCapsOff{0.18f, 0.20f, 0.26f, 0.90f};
constexpr ImVec4 kCapsOffHover{0.24f, 0.28f, 0.36f, 1.0f};
constexpr ImVec4 kSpaceKey{0.22f, 0.24f, 0.32f, 0.90f};
constexpr ImVec4 kSpaceKeyHover{0.28f, 0.32f, 0.42f, 1.0f};
constexpr ImVec4 kBackspaceKey{0.40f, 0.20f, 0.20f, 0.90f};
constexpr ImVec4 kBackspaceKeyHover{0.50f, 0.25f, 0.25f, 1.0f};
constexpr ImVec4 kClearKey{0.32f, 0.18f, 0.18f, 0.90f};
constexpr ImVec4 kClearKeyHover{0.42f, 0.22f, 0.22f, 1.0f};
constexpr ImVec4 kConfirmKey{0.18f, 0.48f, 0.26f, 0.95f};
constexpr ImVec4 kConfirmKeyHover{0.24f, 0.58f, 0.32f, 1.0f};
constexpr ImVec4 kCancelKey{0.45f, 0.18f, 0.18f, 0.95f};
constexpr ImVec4 kCancelKeyHover{0.55f, 0.24f, 0.24f, 1.0f};

constexpr ImVec4 kHelpText{0.70f, 0.70f, 0.65f, 1.0f};

// The measurements, in display units before the scale.
constexpr float kKeyWidth = 44.0f;
constexpr float kKeyHeight = 38.0f;
constexpr float kCornerRounding = 4.0f;
constexpr float kWindowRounding = 8.0f;
constexpr float kInputPaddingX = 10.0f;
constexpr float kInputPaddingY = 8.0f;
constexpr float kActionPadding = 20.0f;
constexpr float kSpacePadding = 36.0f;
/// An action key is at least this much wider than a character key.
constexpr float kActionMinWidthFactor = 1.3f;

/// One action key's definition: the callback it runs, its label and its colours.
struct ActionDef {
    int act;
    BbStrings::StringId sid;
    ImVec4 base;
    ImVec4 hovered;
};

/// The action row, in the order it is drawn.
std::array<ActionDef, ActCount> ActionTable(bool caps_on) {
    return {{
        {ActCaps, BbStrings::StringId::KbCaps, caps_on ? kCapsOn : kCapsOff,
         caps_on ? kCapsOnHover : kCapsOffHover},
        {ActSpace, BbStrings::StringId::KbSpace, kSpaceKey, kSpaceKeyHover},
        {ActBackspace, BbStrings::StringId::KbBackspace, kBackspaceKey, kBackspaceKeyHover},
        {ActClear, BbStrings::StringId::KbClear, kClearKey, kClearKeyHover},
        {ActConfirm, BbStrings::StringId::KbConfirm, kConfirmKey, kConfirmKeyHover},
        {ActCancel, BbStrings::StringId::KbCancel, kCancelKey, kCancelKeyHover},
    }};
}

/// Draws one key with `style`, and reports whether it was clicked.
bool KeyButton(const char* label, const ImVec2& size, const ImVec4& base, const ImVec4& hovered,
               const ImVec4& border, const ImVec4& text, float border_size) {
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
    ImGui::PushStyleColor(ImGuiCol_Border, border);
    ImGui::PushStyleColor(ImGuiCol_Text, text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, border_size);
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return clicked;
}

/// Centres a row of `total_w` in what is left of the line.
void CentreRow(float total_w) {
    const float avail_w = ImGui::GetContentRegionAvail().x;
    if (avail_w > total_w) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - total_w) * 0.5f);
    }
}

} // namespace

/// The character rows: one button per key, the cursor's key in gold.
void RenderCharRows(const ImVec2& key_size, float scale) {
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    for (int row = 0; row < kCharRows; ++row) {
        const char* const* keys = RowKeys(row);
        const int count = RowLength(row);
        CentreRow(count * key_size.x + (count - 1) * spacing);
        for (int col = 0; col < count; ++col) {
            if (col > 0) {
                ImGui::SameLine();
            }
            const bool selected = SelectedRow() == row && SelectedCol() == col;
            if (KeyButton(keys[col], key_size,
                          selected ? kSelectedKey : kKey, selected ? kSelectedKeyHover : kKeyHover,
                          selected ? kSelectedKeyBorder : kKeyBorder,
                          selected ? kSelectedKeyText : kKeyText, selected ? 2.0f : 1.0f)) {
                SelectedRow() = row;
                SelectedCol() = col;
                if (Callbacks& cb = GetCallbacks(); cb.on_append) {
                    cb.on_append(keys[col]);
                }
            }
        }
    }
    (void)scale;
}

/// The action row: one key per action, each as wide as its label needs.
void RenderActionRow(const ImVec2& key_size, float scale, int lang) {
    const auto actions = ActionTable(CapsLock());
    const float spacing = ImGui::GetStyle().ItemSpacing.x;

    std::array<float, ActCount> widths{};
    float total_w = 0.0f;
    for (int i = 0; i < ActCount; ++i) {
        const float text_w = ImGui::CalcTextSize(BbStrings::Get(actions[i].sid, lang)).x;
        const float pad = (actions[i].act == ActSpace) ? kSpacePadding * scale : kActionPadding * scale;
        widths[i] = std::max(key_size.x * kActionMinWidthFactor, text_w + pad);
        total_w += widths[i];
    }
    total_w += (ActCount - 1) * spacing;
    CentreRow(total_w);

    for (int i = 0; i < ActCount; ++i) {
        if (i > 0) {
            ImGui::SameLine();
        }
        const auto& action = actions[i];
        const bool selected = SelectedRow() == kActionRow && SelectedCol() == i;
        if (KeyButton(BbStrings::Get(action.sid, lang), ImVec2(widths[i], key_size.y),
                      selected ? kSelectedKey : action.base,
                      selected ? kSelectedKeyHover : action.hovered,
                      selected ? kSelectedKeyBorder : kActionBorder,
                      selected ? kSelectedKeyText : kActionText, selected ? 2.0f : 1.0f)) {
            SelectedRow() = kActionRow;
            SelectedCol() = i;
            TriggerAction(action.act);
        }
    }
}

} // namespace BbVirtualKeyboard::detail

namespace BbVirtualKeyboard {

void Render(const std::string& title, const std::string& current_text, int lang,
            float display_scale) {
    using namespace detail;

    const float scale = std::max(1.0f, display_scale);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.96f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kWindowRounding * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, kWindowBg);
    ImGui::PushStyleColor(ImGuiCol_Border, kWindowBorder);

    ImGui::Begin("##virtual_keyboard_dialog", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings);

    ImGui::PushStyleColor(ImGuiCol_Text, kTitleText);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // The text being typed, full width, with a cursor at its end.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kInputBg);
    ImGui::PushStyleColor(ImGuiCol_Border, kInputBorder);
    ImGui::PushStyleColor(ImGuiCol_Text, kInputText);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kCornerRounding * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(kInputPaddingX * scale, kInputPaddingY * scale));
    std::string display_str = current_text + "_";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##text_display", display_str.data(), display_str.size() + 1,
                     ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
    ImGui::Spacing();

    const ImVec2 key_size(kKeyWidth * scale, kKeyHeight * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kCornerRounding * scale);
    detail::RenderCharRows(key_size, scale);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    detail::RenderActionRow(key_size, scale, lang);
    ImGui::PopStyleVar(); // FrameRounding

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, kHelpText);
    ImGui::TextUnformatted(BbStrings::Get(BbStrings::StringId::PromptGamepadHelp, lang));
    ImGui::TextUnformatted(BbStrings::Get(BbStrings::StringId::PromptKeyboardHelp, lang));
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleColor(2); // WindowBg, Border
    ImGui::PopStyleVar(2);   // WindowRounding, WindowBorderSize
}

} // namespace BbVirtualKeyboard
