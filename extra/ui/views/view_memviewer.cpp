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

bool ViewMemViewer::IsOpen() {
    return s_memory_viewer_open;
}

void ViewMemViewer::SetOpen(bool open) {
    s_memory_viewer_open = open;
}

void ViewMemViewer::Toggle() {
    s_memory_viewer_open = !s_memory_viewer_open;
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
    if (ImGui::SmallButton(L("Hex -> Disasm", "Hex -> Disasm", "Hex -> Дизасм"))) {
        s_memview_disasm_addr = s_memview_hex_addr;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("Set Disassembler address to match current Hex address",
                                  "Ajustar endereco do Desassemblador para o mesmo do Hex",
                                  "Синхронизировать адрес дизассемблера с шестнадцатеричным"));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Disasm -> Hex", "Disasm -> Hex", "Дизасм -> Hex"))) {
        s_memview_hex_addr = s_memview_disasm_addr;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("Set Hex address to match current Disassembler address",
                                  "Ajustar endereco do Hex para o mesmo do Desassemblador",
                                  "Синхронизировать адрес Hex с адресом дизассемблера"));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("| %s", L("Scroll wheel to navigate dynamically",
                                  "Use a roda do mouse para rolar dinamicamente",
                                  "Колесо мыши для динамической прокрутки"));
    ImGui::Separator();

    const float avail_y = ImGui::GetContentRegionAvail().y;
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

    ImGui::End();
}

} // namespace UI
