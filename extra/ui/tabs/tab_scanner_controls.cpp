// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the memory scanner's search controls -- the value field, the type/scope/condition pickers,
// the first/next/cancel/reset buttons and the progress bar while a scan runs.
#include "tab_scanner_internal.h"

#include <cstdio>

#include <imgui.h>

#include "mem_scanner.h"
#include "ui_strings.h"

namespace UI::ScannerDetail {
namespace {

// The scan buttons: green for a first scan, blue for a next one, red for cancel.
constexpr ImVec4 kFirstButton{0.18f, 0.48f, 0.32f, 0.85f};
constexpr ImVec4 kFirstButtonHover{0.24f, 0.62f, 0.40f, 1.0f};
constexpr ImVec4 kNextButton{0.20f, 0.38f, 0.58f, 0.85f};
constexpr ImVec4 kNextButtonHover{0.26f, 0.50f, 0.74f, 1.0f};
constexpr ImVec4 kCancelButton{0.70f, 0.20f, 0.20f, 0.90f};
constexpr ImVec4 kCancelButtonHover{0.90f, 0.25f, 0.25f, 1.0f};

// The progress bar's fill, background and border.
constexpr ImVec4 kProgressFill{0.20f, 0.55f, 0.82f, 1.00f};
constexpr ImVec4 kProgressBackground{0.08f, 0.10f, 0.14f, 1.00f};
constexpr ImVec4 kProgressBorder{0.30f, 0.42f, 0.58f, 0.70f};

/// The progress bar: its own text, centred with a halo so it reads over any fill.
void RenderProgress(float progress) {
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%s (%.0f%%)",
                  L("Scanning...", "Escaneando...", "Сканирование..."), progress * 100.0f);

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kProgressFill);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kProgressBackground);
    ImGui::PushStyleColor(ImGuiCol_Border, kProgressBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

    constexpr float kProgressHeight = 26.0f;
    ImGui::ProgressBar(progress, ImVec2(-1.0f, kProgressHeight), "");

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    // ImGui cannot put text over a progress bar, so it is drawn here, centred.
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_max = ImGui::GetItemRectMax();
    const ImVec2 text_size = ImGui::CalcTextSize(overlay);
    const ImVec2 text_pos(rect_min.x + (rect_max.x - rect_min.x - text_size.x) * 0.5f,
                          rect_min.y + (rect_max.y - rect_min.y - text_size.y) * 0.5f);

    ImDrawList* const draw_list = ImGui::GetWindowDrawList();
    const ImU32 shadow = IM_COL32(0, 0, 0, 240);
    draw_list->AddText(ImVec2(text_pos.x - 1, text_pos.y), shadow, overlay);
    draw_list->AddText(ImVec2(text_pos.x + 1, text_pos.y), shadow, overlay);
    draw_list->AddText(ImVec2(text_pos.x, text_pos.y - 1), shadow, overlay);
    draw_list->AddText(ImVec2(text_pos.x, text_pos.y + 1), shadow, overlay);
    draw_list->AddText(ImVec2(text_pos.x + 1, text_pos.y + 1), shadow, overlay);
    draw_list->AddText(text_pos, IM_COL32(245, 248, 252, 255), overlay);
}

/// The input side: the value, and what it is searched as.
void RenderInputs() {
    auto& scanner = Debugger::MemoryScanner::Get();
    const bool is_pattern = IsPattern();

    ImGui::SetNextItemWidth(210.0f);
    const char* const value_label =
        is_pattern ? L("Pattern (Hex / ?\\?)", "Padrao (Hex / ?\\?)", "Сигнатура (Hex / ?\\?)")
                   : L("Value", "Valor", "Значение");
    if (ImGui::InputText(value_label, ValueInput(), kValueBufferBytes,
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        RunScan(true);
    }
    ImGui::SetItemTooltip("%s", is_pattern ? "e.g. 48 8B 05 ?? ?? ?? ?? 48 85 C0"
                                           : L("Press Enter to run Scan",
                                               "Pressione Enter para escanear",
                                               "Нажмите Enter для поиска"));

    ImGui::SetNextItemWidth(170.0f);
    ImGui::Combo(L("Type", "Tipo", "Тип"), &SelectedType(), kTypeNames, kTypeCount);

    ImGui::SetNextItemWidth(230.0f);
    ImGui::Combo(L("Scope", "Regiao", "Область"), &SelectedScope(), ScopeNames(), kScopeCount);

    // A pattern has no value to compare against.
    ImGui::BeginDisabled(is_pattern);
    ImGui::SetNextItemWidth(170.0f);
    ImGui::Combo(L("Condition", "Condicao", "Тип поиска"), &SelectedComparison(), ComparisonNames(),
                 kComparisonCount);
    ImGui::EndDisabled();

    (void)scanner;
}

/// The action side: first/next while idle, cancel while scanning, reset once results exist.
void RenderActions(bool is_scanning) {
    auto& scanner = Debugger::MemoryScanner::Get();
    constexpr ImVec2 kButtonSize(125, 30);
    constexpr ImVec2 kSmallButtonSize(125, 26);

    ImGui::BeginDisabled(is_scanning);
    ImGui::PushStyleColor(ImGuiCol_Button, kFirstButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kFirstButtonHover);
    if (ImGui::Button(L("First Scan", "Primeiro Scan", "Первый поиск"), kButtonSize)) {
        RunScan(true);
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, kNextButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kNextButtonHover);
    if (ImGui::Button(L("Next Scan", "Proximo Scan", "След. поиск"), kButtonSize)) {
        RunScan(false);
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (is_scanning) {
        ImGui::PushStyleColor(ImGuiCol_Button, kCancelButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kCancelButtonHover);
        if (ImGui::Button(L("Cancel", "Cancelar", "Отмена"), kSmallButtonSize)) {
            scanner.CancelScan();
        }
        ImGui::PopStyleColor(2);
    } else if (ImGui::Button(L("Reset", "Resetar Busca", "Сброс"), kSmallButtonSize)) {
        scanner.Reset();
    }
}

} // namespace

void RenderControls() {
    auto& scanner = Debugger::MemoryScanner::Get();

    if (ImGui::BeginTable("ScanControls", 2, ImGuiTableFlags_None)) {
        ImGui::TableSetupColumn("Inputs", ImGuiTableColumnFlags_WidthFixed, 360.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        RenderInputs();

        ImGui::TableSetColumnIndex(1);
        RenderActions(scanner.IsScanning());

        ImGui::EndTable();
    }

    if (scanner.IsScanning()) {
        ImGui::Spacing();
        RenderProgress(scanner.GetProgress());
        ImGui::Spacing();
    }
}

} // namespace UI::ScannerDetail
