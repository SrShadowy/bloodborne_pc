// SPDX-License-Identifier: GPL-2.0-or-later
#include "view_hexgrid.h"
#include "ui_strings.h"
#include "view_disasm.h"
#include "mem_scanner.h"
#include "mem_editor.h"
#include "breakpoint.h"
#include "cutscene_detector.h"
#include "ui_manager.h"
#include "gpu/bbgpu.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <string>
#include <algorithm>

namespace UI {

static uintptr_t s_selected_addr = 0x00400000;
static bool s_has_selection = false;

static uintptr_t ResolveHexAddress(uintptr_t addr) {
    const uintptr_t img = bbgpu_get_guest_image_base();
    if (img != 0 && addr >= 0x00400000 && addr < 0x20000000) {
        return img + (addr - 0x00400000);
    }
    return addr;
}

uintptr_t ViewHexGrid::GetSelectedAddress() {
    return s_selected_addr;
}

bool ViewHexGrid::HasSelection() {
    return s_has_selection;
}

void ViewHexGrid::SetSelectedAddress(uintptr_t addr) {
    s_selected_addr = addr;
    s_has_selection = true;
}

void ViewHexGrid::RenderHexGridView(uintptr_t& cur_addr, float height, bool standalone) {
    if (standalone) {
        ImGui::SetNextItemWidth(150.0f);
        char addr_buf[32];
        std::snprintf(addr_buf, sizeof(addr_buf), "0x%llx", static_cast<unsigned long long>(cur_addr));
        if (ImGui::InputText("##HexAddrMV", addr_buf, sizeof(addr_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            cur_addr = ResolveHexAddress(std::strtoull(addr_buf, nullptr, 16));
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("eboot##mv")) cur_addr = ResolveHexAddress(0x00400000);
        ImGui::SetItemTooltip("%s", L("Jump to eboot base (0x00400000)", "Pular para a base do eboot (0x00400000)", "Перейти к eboot"));

        ImGui::SameLine();
        if (ImGui::SmallButton("Heap##mv")) cur_addr = 0x1000000000ULL;
        ImGui::SetItemTooltip("%s", L("Jump to main heap (0x1000000000)", "Pular para o heap principal (0x1000000000)", "Перейти к куче"));
    }

    const char* child_id = standalone ? "HexChildMV" : "HexChildTab";
    ImGui::BeginChild(child_id, ImVec2(0, height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel > 0.0f) {
            int steps = std::max(1, static_cast<int>(wheel * 2));
            uintptr_t delta = static_cast<uintptr_t>(steps) * 16;
            cur_addr = (cur_addr >= delta) ? (cur_addr - delta) : 0;
        } else if (wheel < 0.0f) {
            int steps = std::max(1, static_cast<int>(-wheel * 2));
            cur_addr += static_cast<uintptr_t>(steps) * 16;
        }
    }

    const size_t view_size = (height > 300.0f) ? 512 : 256;
    uint8_t buffer[512] = {0};
    const bool readable = Debugger::MemoryScanner::ReadMemory(cur_addr, buffer, view_size);

    if (!readable) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s (0x%012llx)",
                           L("Inaccessible address or unallocated memory.", "Endereco inacessivel ou memoria nao alocada.", "Недоступный адрес или невыделенная память."),
                           static_cast<unsigned long long>(cur_addr));
        ImGui::EndChild();
        return;
    }

    const char* table_id = standalone ? "HexTableMV" : "HexTable";
    if (ImGui::BeginTable(table_id, 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, height))) {
        ImGui::TableSetupColumn(L("Address (Click to Select)", "Endereco (Clique p/ Selecionar)", "Адрес (выбрать)"), ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("00 01 02 03  04 05 06 07", ImGuiTableColumnFlags_WidthFixed, 195.0f);
        ImGui::TableSetupColumn("08 09 0A 0B  0C 0D 0E 0F", ImGuiTableColumnFlags_WidthFixed, 195.0f);
        ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthFixed, 145.0f);
        ImGui::TableHeadersRow();

        for (size_t row = 0; row < view_size; row += 16) {
            const uintptr_t row_addr = cur_addr + row;
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            char row_label[32];
            std::snprintf(row_label, sizeof(row_label), "0x%012llx", static_cast<unsigned long long>(row_addr));

            const bool is_selected = s_has_selection && (s_selected_addr == row_addr);
            if (ImGui::Selectable(row_label, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                s_selected_addr = row_addr;
                s_has_selection = true;
            }

            char hex_pop_id[32];
            std::snprintf(hex_pop_id, sizeof(hex_pop_id), "##hex_row_%08llx", static_cast<unsigned long long>(row_addr));
            if (ImGui::BeginPopupContextItem(hex_pop_id)) {
                s_selected_addr = row_addr;
                s_has_selection = true;
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "0x%012llx", static_cast<unsigned long long>(row_addr));
                ImGui::Separator();

                if (ImGui::MenuItem(L("Copy Address", "Copiar Endereco", "Копировать адрес"))) {
                    char hex_addr[32];
                    std::snprintf(hex_addr, sizeof(hex_addr), "0x%llx", static_cast<unsigned long long>(row_addr));
                    ImGui::SetClipboardText(hex_addr);
                }
                if (ImGui::MenuItem(L("Copy Hex Bytes", "Copiar Bytes Hex", "Копировать байты Hex"))) {
                    char hex_row[64];
                    std::snprintf(hex_row, sizeof(hex_row),
                                  "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                                  buffer[row + 0], buffer[row + 1], buffer[row + 2], buffer[row + 3],
                                  buffer[row + 4], buffer[row + 5], buffer[row + 6], buffer[row + 7],
                                  buffer[row + 8], buffer[row + 9], buffer[row + 10], buffer[row + 11],
                                  buffer[row + 12], buffer[row + 13], buffer[row + 14], buffer[row + 15]);
                    ImGui::SetClipboardText(hex_row);
                }
                if (ImGui::MenuItem(L("Copy ASCII", "Copiar ASCII", "Копировать ASCII"))) {
                    char asc_row[17] = {0};
                    for (int i = 0; i < 16; ++i) asc_row[i] = std::isprint(buffer[row + i]) ? buffer[row + i] : '.';
                    ImGui::SetClipboardText(asc_row);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(L("Follow in Disassembler", "Seguir no Desassemblador", "Перейти в дизассемблер"))) {
                    ViewDisasm::SetDisasmAddress(row_addr);
                    if (!standalone) UiManager::RequestTab(TabId::Debugger);
                }
                if (ImGui::MenuItem(L("Add to Watchlist", "Adicionar a Watchlist", "Добавить в таблицу"))) {
                    Debugger::MemoryEditor::Get().AddWatch("Hex Item", row_addr, Debugger::DataType::U32);
                    UiManager::SetStatus(L("Address added to Watchlist!", "Endereco adicionado a Watchlist!", "Адрес добавлен в таблицу!"));
                }
                if (ImGui::MenuItem(L("Watch Writes ('Who Writes?')", "Monitorar com Watchpoint ('Quem Escreve?')", "Кто пишет по адресу?"))) {
                    Debugger::BreakpointManager::Get().SetWriteWatchpoint(row_addr);
                    if (!standalone) UiManager::RequestTab(TabId::Debugger);
                    UiManager::SetStatus(L("Watchpoint attached!", "Watchpoint ativado!", "Точка наблюдения установлена!"));
                }
                if (ImGui::MenuItem(L("Set as Cutscene Flag", "Definir como Flag de Cutscene", "Назначить флагом кат-сцены"))) {
                    Debugger::CutsceneDetector::Get().SetAddress(row_addr);
                    Debugger::CutsceneDetector::Get().SetEnabled(true);
                    UiManager::SetStatus(L("Cutscene flag assigned!", "Flag de cutscene definida!", "Флаг кат-сцены назначен!"));
                }
                ImGui::EndPopup();
            }

            ImGui::TableSetColumnIndex(1);
            char hex1[64];
            std::snprintf(hex1, sizeof(hex1), "%02X %02X %02X %02X  %02X %02X %02X %02X",
                          buffer[row + 0], buffer[row + 1], buffer[row + 2], buffer[row + 3],
                          buffer[row + 4], buffer[row + 5], buffer[row + 6], buffer[row + 7]);
            ImGui::TextUnformatted(hex1);

            ImGui::TableSetColumnIndex(2);
            char hex2[64];
            std::snprintf(hex2, sizeof(hex2), "%02X %02X %02X %02X  %02X %02X %02X %02X",
                          buffer[row + 8], buffer[row + 9], buffer[row + 10], buffer[row + 11],
                          buffer[row + 12], buffer[row + 13], buffer[row + 14], buffer[row + 15]);
            ImGui::TextUnformatted(hex2);

            ImGui::TableSetColumnIndex(3);
            char ascii[32] = {0};
            for (size_t c = 0; c < 16; ++c) {
                const char ch = static_cast<char>(buffer[row + c]);
                ascii[c] = (std::isprint(static_cast<unsigned char>(ch))) ? ch : '.';
            }
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "| %s |", ascii);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

} // namespace UI
