// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the two launcher buttons for the memory tools and the memory viewer windows.
#include "bbport_overlay_sections.h"

#include "bbport_overlay_ui.h"
#include "ui_manager.h"
#include "ui_strings.h"

#include <imgui.h>

namespace BbOverlaySections {
namespace {

/// A full-width button that opens or closes one of the memory windows: green while the window is
/// open (clicking hides it), blue while it is closed (clicking shows it).
void WindowToggleButton(bool window_open, float height, const char* label_open,
                        const char* label_closed, void (*set_open)(bool)) {
    const char* label = window_open ? label_open : label_closed;
    ImGui::PushStyleColor(ImGuiCol_Button, window_open ? Theme::kButtonOn : Theme::kButtonOff);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          window_open ? Theme::kButtonOnHovered : Theme::kButtonOffHovered);
    if (ImGui::Button(label, ImVec2(-1.0f, height))) {
        set_open(!window_open);
    }
    ImGui::PopStyleColor(2);
}

} // namespace

void RenderMemoryToolsSection(float base_scale) {
    ImGui::Spacing();
    ImGui::SeparatorText(UI::L("Memory & Reverse Engineering", "Operações de Memória",
                               "Операции с памятью"));

    WindowToggleButton(UI::UiManager::IsMemoryWindowOpen(),
                       Size::kMemoryToolsButtonHeight * base_scale,
                       UI::L("Hide Memory Tools Window###btn_memtools",
                             "Ocultar Janela de Memória###btn_memtools",
                             "Скрыть окно памяти###btn_memtools"),
                       UI::L("Open Memory Tools Window (Scanner, Watchlist, Debugger)###btn_memtools",
                             "Abrir Janela de Memória (Scanner, Watchlist, Debugger)###btn_memtools",
                             "Открыть окно памяти (Сканер, Таблица, Отладчик)###btn_memtools"),
                       &UI::UiManager::SetMemoryWindowOpen);

    WindowToggleButton(UI::UiManager::IsMemoryViewerOpen(),
                       Size::kMemoryViewerButtonHeight * base_scale,
                       UI::L("Hide Memory Viewer###btn_memviewer", "Ocultar Memory Viewer###btn_memviewer",
                             "Скрыть Memory Viewer###btn_memviewer"),
                       UI::L("Open Memory Viewer###btn_memviewer", "Abrir Memory Viewer###btn_memviewer",
                             "Открыть Memory Viewer###btn_memviewer"),
                       &UI::UiManager::SetMemoryViewerOpen);

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", UI::L(
        "Opens separate floating windows for memory scanning, hex view, and live disassembly.",
        "Abre janelas flutuantes separadas para varredura, hex view e desassemblagem em tempo real.",
        "Открывает отдельные плавающие окна для сканирования, hex-инспектора и дизассемблера."));
    ImGui::PopStyleColor();
}

} // namespace BbOverlaySections
