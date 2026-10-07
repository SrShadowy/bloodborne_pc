// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui_manager.h"
#include "tab_settings.h"
#include "tab_scanner.h"
#include "tab_watchlist.h"
#include "tab_hexview.h"
#include "tab_debugger.h"
#include "mem_editor.h"

#include <imgui.h>

namespace UI {

void UiManager::Tick() {
    Debugger::MemoryEditor::Get().TickFreeze();
}

void UiManager::Render() {
    if (ImGui::BeginTabBar("MainTabBar", ImGuiTabBarFlags_None)) {
        if (ImGui::BeginTabItem("Configuracoes")) {
            TabSettings::Render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Scanner de Memoria")) {
            TabScanner::Render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Watchlist / Cheats")) {
            TabWatchlist::Render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Hex Inspector")) {
            TabHexView::Render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Depurador & Breakpoints")) {
            TabDebugger::Render();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

} // namespace UI
