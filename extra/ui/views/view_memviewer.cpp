// SPDX-License-Identifier: GPL-2.0-or-later
#include "view_memviewer.h"
#include "view_disasm.h"
#include "view_hexgrid.h"
#include "ui_strings.h"

#include <imgui.h>
#include <cstdio>
#include <algorithm>

namespace UI {

static bool s_memory_viewer_open = false;
static uintptr_t s_memview_disasm_addr = 0x00400000;
static uintptr_t s_memview_hex_addr = 0x00400000;
static bool s_memview_initialized = false;
static MemViewerMode s_memviewer_mode = MemViewerMode::Split;

bool ViewMemViewer::IsOpen() {
    return s_memory_viewer_open;
}

void ViewMemViewer::SetOpen(bool open) {
    s_memory_viewer_open = open;
}

void ViewMemViewer::Toggle() {
    s_memory_viewer_open = !s_memory_viewer_open;
}

MemViewerMode ViewMemViewer::GetMode() {
    return s_memviewer_mode;
}

void ViewMemViewer::SetMode(MemViewerMode mode) {
    s_memviewer_mode = mode;
}

void ViewMemViewer::NavigateHex(uintptr_t addr) {
    s_memview_hex_addr = addr;
    ViewHexGrid::SetSelectedAddress(addr);
    s_memory_viewer_open = true;
}

void ViewMemViewer::NavigateDisasm(uintptr_t addr) {
    s_memview_disasm_addr = addr;
    ViewDisasm::SetDisasmAddress(addr);
    s_memory_viewer_open = true;
}

void ViewMemViewer::Render() {
    if (!s_memory_viewer_open) return;

    if (!s_memview_initialized) {
        s_memview_disasm_addr = ViewDisasm::GetDisasmAddress();
        if (s_memview_disasm_addr == 0) s_memview_disasm_addr = 0x00400000;
        s_memview_hex_addr = ViewHexGrid::GetSelectedAddress();
        if (s_memview_hex_addr == 0) s_memview_hex_addr = 0x00400000;
        s_memview_initialized = true;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    float scale = 1.0f;
    if (viewport && viewport->WorkSize.y > 0.0f) {
        scale = std::max(viewport->WorkSize.y / 1080.0f, 0.75f);
    }

    ImVec2 default_size(920.0f * scale, 680.0f * scale);
    ImVec2 default_pos(viewport ? (viewport->WorkPos.x + 500.0f * scale) : 500.0f,
                       viewport ? (viewport->WorkPos.y + 60.0f * scale) : 60.0f);

    ImGui::SetNextWindowPos(default_pos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(default_size, ImGuiCond_FirstUseEver);

    bool keep_open = s_memory_viewer_open;
    char window_title[128];
    std::snprintf(window_title, sizeof(window_title), "%s###bbport_memviewer",
                  L("Memory Viewer", "Visualizador de Memoria", "Просмотрщик памяти"));

    if (!ImGui::Begin(window_title, &keep_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        s_memory_viewer_open = keep_open;
        return;
    }
    s_memory_viewer_open = keep_open;

    ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "%s",
                       L("Dual-Pane Memory & Code Inspector",
                         "Inspetor Duplo de Memoria e Codigo",
                         "Двухоконный инспектор памяти и кода"));
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    const bool is_split = (s_memviewer_mode == MemViewerMode::Split);
    const bool is_disasm = (s_memviewer_mode == MemViewerMode::DisasmOnly);
    const bool is_hex = (s_memviewer_mode == MemViewerMode::HexOnly);

    if (is_split) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.75f, 1.0f));
    if (ImGui::SmallButton(L("Split", "Dividido", "Разделить"))) s_memviewer_mode = MemViewerMode::Split;
    if (is_split) ImGui::PopStyleColor();

    ImGui::SameLine();
    if (is_disasm) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.75f, 1.0f));
    if (ImGui::SmallButton(L("Disasm Full", "Apenas Disasm", "Весь Дизасм"))) s_memviewer_mode = MemViewerMode::DisasmOnly;
    if (is_disasm) ImGui::PopStyleColor();

    ImGui::SameLine();
    if (is_hex) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.75f, 1.0f));
    if (ImGui::SmallButton(L("Hex Full", "Apenas Hex", "Весь Hex"))) s_memviewer_mode = MemViewerMode::HexOnly;
    if (is_hex) ImGui::PopStyleColor();

    ImGui::Separator();

    const float avail_y = ImGui::GetContentRegionAvail().y;
    if (s_memviewer_mode == MemViewerMode::DisasmOnly) {
        float disasm_h = std::max(200.0f * scale, avail_y - 30.0f);
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s",
                           L("Disassembler (x86-64 Machine Code):",
                             "Desassemblador (Codigo de Maquina x86-64):",
                             "Дизассемблер (Машинный код x86-64):"));
        ViewDisasm::RenderDisassemblerView(s_memview_disasm_addr, disasm_h, true);
    } else if (s_memviewer_mode == MemViewerMode::HexOnly) {
        float hex_h = std::max(200.0f * scale, avail_y - 30.0f);
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "%s",
                           L("Hex Memory Dump:", "Dump Hexadecimal de Memoria:", "Шестнадцатеричный дамп памяти:"));
        ViewHexGrid::RenderHexGridView(s_memview_hex_addr, hex_h, true);
    } else {
        float disasm_h = std::max(180.0f * scale, (avail_y - 50.0f) * 0.48f);
        float hex_h = std::max(180.0f * scale, (avail_y - 50.0f) * 0.48f);

        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s",
                           L("Disassembler (x86-64 Machine Code):",
                             "Desassemblador (Codigo de Maquina x86-64):",
                             "Дизассемблер (Машинный код x86-64):"));
        ViewDisasm::RenderDisassemblerView(s_memview_disasm_addr, disasm_h, true);

        ImGui::Spacing();
        ImGui::Separator();

        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "%s",
                           L("Hex Memory Dump:", "Dump Hexadecimal de Memoria:", "Шестнадцатеричный дамп памяти:"));
        ViewHexGrid::RenderHexGridView(s_memview_hex_addr, hex_h, true);
    }

    ImGui::End();
}

} // namespace UI
