// SPDX-License-Identifier: GPL-2.0-or-later
#include "adw_menu.h"

namespace UI::Adw {

void Theme::Push(float scale) {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(30, 30, 34, 252));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(56, 56, 64, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(48, 48, 56, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(60, 60, 70, 255));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(70, 70, 82, 255));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, IM_COL32(53, 132, 228, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(53, 132, 228, 255));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(73, 145, 235, 255));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(48, 48, 56, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(64, 64, 76, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(76, 76, 90, 255));
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(58, 58, 68, 255));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(70, 70, 82, 255));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(53, 132, 228, 255));
    ImGui::PushStyleColor(ImGuiCol_Separator, IM_COL32(56, 56, 64, 255));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 6.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f * scale, 6.0f * scale));
}

void Theme::Pop() {
    ImGui::PopStyleVar(6);
    ImGui::PopStyleColor(15);
}

bool ModalWindow::Begin(const char* name, bool* p_open, float width, float height, float scale) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();

    // Dim background
    ImDrawList* bg_draw = ImGui::GetBackgroundDrawList();
    bg_draw->AddRectFilled(vp->WorkPos, ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y),
                          IM_COL32(0, 0, 0, 160));

    // Centered layout
    const ImVec2 pos(vp->WorkPos.x + (vp->WorkSize.x - width) * 0.5f,
                     vp->WorkPos.y + (vp->WorkSize.y - height) * 0.5f);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);

    return ImGui::Begin(name, p_open,
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void ModalWindow::End() {
    ImGui::End();
}

void TopBar::Render(const Config& cfg, bool* p_open) {
    const ImVec2 header_pos = ImGui::GetCursorScreenPos();
    const ImVec2 win_sz = ImGui::GetWindowSize();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Left pane header background
    draw->AddRectFilled(header_pos, ImVec2(header_pos.x + cfg.sidebar_width, header_pos.y + cfg.height),
                        IM_COL32(24, 24, 28, 255), 12.0f * cfg.scale, ImDrawFlags_RoundCornersTopLeft);

    // Right pane header background
    draw->AddRectFilled(ImVec2(header_pos.x + cfg.sidebar_width, header_pos.y),
                        ImVec2(header_pos.x + win_sz.x, header_pos.y + cfg.height),
                        IM_COL32(30, 30, 34, 255), 12.0f * cfg.scale, ImDrawFlags_RoundCornersTopRight);

    // Horizontal divider
    draw->AddLine(ImVec2(header_pos.x, header_pos.y + cfg.height),
                  ImVec2(header_pos.x + win_sz.x, header_pos.y + cfg.height),
                  IM_COL32(56, 56, 64, 255), 1.0f);

    // Vertical divider between sidebar and content
    draw->AddLine(ImVec2(header_pos.x + cfg.sidebar_width, header_pos.y),
                  ImVec2(header_pos.x + cfg.sidebar_width, header_pos.y + win_sz.y),
                  IM_COL32(56, 56, 64, 255), 1.0f);

    // Brand text on far left
    const float text_y = header_pos.y + (cfg.height - ImGui::GetFontSize()) * 0.5f;
    float cur_x = header_pos.x + 16.0f * cfg.scale;
    if (cfg.brand_prefix && cfg.brand_prefix[0]) {
        draw->AddText(ImVec2(cur_x, text_y), IM_COL32(140, 140, 150, 255), cfg.brand_prefix);
        cur_x += ImGui::CalcTextSize(cfg.brand_prefix).x + 8.0f * cfg.scale;
    }
    if (cfg.brand_title && cfg.brand_title[0]) {
        draw->AddText(ImVec2(cur_x, text_y), IM_COL32(245, 245, 250, 255), cfg.brand_title);
    }

    // Active tab title centered over content pane
    if (cfg.active_tab_title && cfg.active_tab_title[0]) {
        const ImVec2 title_sz = ImGui::CalcTextSize(cfg.active_tab_title);
        const float center_x = header_pos.x + cfg.sidebar_width + (win_sz.x - cfg.sidebar_width) * 0.5f;
        draw->AddText(ImVec2(center_x - title_sz.x * 0.5f, text_y),
                      IM_COL32(245, 245, 250, 255), cfg.active_tab_title);
    }

    // Close button (X) on far right
    if (cfg.show_close_button && p_open) {
        const float btn_sz = 28.0f * cfg.scale;
        const float margin = 16.0f * cfg.scale;
        ImGui::SetCursorScreenPos(ImVec2(header_pos.x + win_sz.x - margin - btn_sz,
                                         header_pos.y + (cfg.height - btn_sz) * 0.5f));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(48, 48, 56, 180));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(215, 55, 55, 230));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(180, 40, 40, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, btn_sz * 0.5f);
        if (ImGui::Button("X##adw_close", ImVec2(btn_sz, btn_sz))) {
            *p_open = false;
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
}

bool Sidebar::Begin(float sidebar_w, float header_h, float height, float scale) {
    const float pad_x = 5.0f * scale;
    const float pad_y = 10.0f * scale;
    const float item_gap = 4.0f * scale;

    ImGui::SetCursorPos(ImVec2(0.0f, header_h));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(24, 24, 28, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad_x, pad_y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, item_gap));
    return ImGui::BeginChild("##adw_sidebar", ImVec2(sidebar_w, height),
                             ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
}

void Sidebar::End() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

bool Sidebar::TabItem(int id, const char* label, bool selected, float sidebar_w, float scale,
                      IconDrawer draw_icon) {
    const float pad_x = 5.0f * scale;
    const float row_w = sidebar_w - (pad_x * 2.0f);
    const float row_h = 36.0f * scale;
    const ImVec2 p = ImGui::GetCursorScreenPos();

    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton(label ? label : "##tab", ImVec2(row_w, row_h));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* sdraw = ImGui::GetWindowDrawList();
    if (selected) {
        sdraw->AddRectFilled(p, ImVec2(p.x + row_w, p.y + row_h), IM_COL32(53, 132, 228, 255), 7.0f * scale);
    } else if (hovered) {
        sdraw->AddRectFilled(p, ImVec2(p.x + row_w, p.y + row_h), IM_COL32(44, 44, 52, 255), 7.0f * scale);
    }

    const ImU32 col = selected ? IM_COL32(255, 255, 255, 255)
                               : (hovered ? IM_COL32(240, 240, 248, 255)
                                          : IM_COL32(175, 175, 185, 255));

    if (draw_icon) {
        const ImVec2 icon_pos(p.x + 12.0f * scale, p.y + (row_h - 16.0f * scale) * 0.5f);
        draw_icon(sdraw, id, icon_pos, scale, col);
    }

    if (label && label[0]) {
        const ImVec2 txt_sz = ImGui::CalcTextSize(label);
        const float txt_y = p.y + (row_h - txt_sz.y) * 0.5f;
        sdraw->AddText(ImVec2(p.x + 36.0f * scale, txt_y), col, label);
    }

    return clicked;
}

static bool s_content_item_width_pushed = false;

bool ContentArea::Begin(float sidebar_w, float header_h, float width, float height, float scale, float pad_x, float item_w) {
    const float actual_pad_x = (pad_x >= 0.0f) ? pad_x : 52.0f * scale;
    const float actual_pad_y = 14.0f * scale;
    const float actual_item_w = (item_w >= 0.0f) ? item_w : 240.0f * scale;

    ImGui::SetCursorPos(ImVec2(sidebar_w, header_h));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 34, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(actual_pad_x, actual_pad_y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f * scale, 10.0f * scale));
    const bool open = ImGui::BeginChild("##adw_content", ImVec2(width, height),
                                        ImGuiChildFlags_AlwaysUseWindowPadding, 0);
    if (open) {
        ImGui::PushItemWidth(actual_item_w);
        ImGui::PushTextWrapPos(0.0f);
        s_content_item_width_pushed = true;
    } else {
        s_content_item_width_pushed = false;
    }
    return open;
}

void ContentArea::End() {
    if (s_content_item_width_pushed) {
        ImGui::PopTextWrapPos();
        ImGui::PopItemWidth();
        s_content_item_width_pushed = false;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void ContentArea::Subtitle(const char* subtitle, float scale) {
    if (subtitle && subtitle[0]) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(145, 145, 155, 255));
        ImGui::TextUnformatted(subtitle);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));
    }
}

} // namespace UI::Adw
