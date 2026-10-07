// SPDX-License-Identifier: GPL-2.0-or-later
#include "tab_hexview.h"
#include "mem_scanner.h"
#include "mem_editor.h"
#include "breakpoint.h"
#include "cutscene_detector.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <vector>
#include <string>
#include <chrono>

namespace UI {

static char addr_input[64] = "0x00400000";
static uintptr_t current_addr = 0x00400000;
static uintptr_t selected_addr = 0x00400000;
static bool has_selection = false;

static std::vector<uintptr_t> history = {0x00400000};
static size_t history_idx = 0;

static std::string status_msg;
static std::chrono::steady_clock::time_point status_time{};

static void CopyToClipboard(const char* text, const char* desc) {
    ImGui::SetClipboardText(text);
    SDL_SetClipboardText(text);
    status_msg = std::string(desc) + " copiado para a area de transferencia!";
    status_time = std::chrono::steady_clock::now();
}

static void NavigateTo(uintptr_t addr) {
    current_addr = addr;
    std::snprintf(addr_input, sizeof(addr_input), "0x%llx", static_cast<unsigned long long>(current_addr));
    if (history.empty() || history[history_idx] != addr) {
        if (history_idx + 1 < history.size()) {
            history.erase(history.begin() + history_idx + 1, history.end());
        }
        history.push_back(addr);
        history_idx = history.size() - 1;
    }
}

void TabHexView::Render() {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Visualizador e Editor Hexadecimal (Hex Inspector)");
    ImGui::Separator();

    // 1. Navigation Toolbar
    ImGui::BeginGroup();
    // History Back / Forward
    const bool can_back = history_idx > 0;
    const bool can_fwd = history_idx + 1 < history.size();
    if (!can_back) ImGui::BeginDisabled();
    if (ImGui::Button("<##back", ImVec2(28, 0))) {
        if (can_back) {
            history_idx--;
            current_addr = history[history_idx];
            std::snprintf(addr_input, sizeof(addr_input), "0x%llx", static_cast<unsigned long long>(current_addr));
        }
    }
    if (!can_back) ImGui::EndDisabled();

    ImGui::SameLine();
    if (!can_fwd) ImGui::BeginDisabled();
    if (ImGui::Button(">##fwd", ImVec2(28, 0))) {
        if (can_fwd) {
            history_idx++;
            current_addr = history[history_idx];
            std::snprintf(addr_input, sizeof(addr_input), "0x%llx", static_cast<unsigned long long>(current_addr));
        }
    }
    if (!can_fwd) ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::InputText("Endereco##goto", addr_input, sizeof(addr_input), ImGuiInputTextFlags_EnterReturnsTrue)) {
        NavigateTo(std::strtoull(addr_input, nullptr, 16));
    }
    ImGui::SameLine();
    if (ImGui::Button("Ir##btn", ImVec2(40, 0))) {
        NavigateTo(std::strtoull(addr_input, nullptr, 16));
    }

    // Step offsets
    ImGui::SameLine();
    if (ImGui::Button("-4 KB") && current_addr >= 4096) {
        NavigateTo(current_addr - 4096);
    }
    ImGui::SameLine();
    if (ImGui::Button("-256 B") && current_addr >= 256) {
        NavigateTo(current_addr - 256);
    }
    ImGui::SameLine();
    if (ImGui::Button("+256 B")) {
        NavigateTo(current_addr + 256);
    }
    ImGui::SameLine();
    if (ImGui::Button("+4 KB")) {
        NavigateTo(current_addr + 4096);
    }
    ImGui::EndGroup();

    // 2. Quick Jump Presets & Actions
    ImGui::BeginGroup();
    struct JumpPreset { const char* label; uintptr_t address; };
    const JumpPreset presets[] = {
        {"Base eboot.bin (ELF)", 0x00400000},
        {"Codigo Principal", 0x02000000},
        {"Parametros e Globais (.data)", 0x05400000},
        {"Memoria Direta PS4 (Heap)", 0x1000000000},
    };
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Salto Rapido", "Locais do Jogo...")) {
        for (const auto& p : presets) {
            if (ImGui::Selectable(p.label)) {
                NavigateTo(p.address);
            }
        }
        auto& cs = Debugger::CutsceneDetector::Get();
        if (cs.IsEnabled() && cs.GetAddress() != 0) {
            char cs_label[64];
            std::snprintf(cs_label, sizeof(cs_label), "Flag de Cutscene (0x%llx)",
                          static_cast<unsigned long long>(cs.GetAddress()));
            if (ImGui::Selectable(cs_label)) {
                NavigateTo(cs.GetAddress());
            }
        }
        ImGui::EndCombo();
    }

    // Copy Toolbar
    ImGui::SameLine(360.0f);
    char cur_addr_hex[32];
    std::snprintf(cur_addr_hex, sizeof(cur_addr_hex), "0x%llx",
                  static_cast<unsigned long long>(has_selection ? selected_addr : current_addr));

    if (ImGui::Button("Copiar Endereco")) {
        CopyToClipboard(cur_addr_hex, "Endereco");
    }
    ImGui::SameLine();
    if (ImGui::Button("Copiar 16 Bytes")) {
        uint8_t copy_buf[16] = {0};
        Debugger::MemoryScanner::ReadMemory(has_selection ? selected_addr : current_addr, copy_buf, 16);
        char hex_str[64];
        std::snprintf(hex_str, sizeof(hex_str),
                      "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                      copy_buf[0], copy_buf[1], copy_buf[2], copy_buf[3],
                      copy_buf[4], copy_buf[5], copy_buf[6], copy_buf[7],
                      copy_buf[8], copy_buf[9], copy_buf[10], copy_buf[11],
                      copy_buf[12], copy_buf[13], copy_buf[14], copy_buf[15]);
        CopyToClipboard(hex_str, "16 Bytes Hex");
    }
    ImGui::SameLine();
    if (ImGui::Button("Copiar ASCII")) {
        uint8_t copy_buf[16] = {0};
        Debugger::MemoryScanner::ReadMemory(has_selection ? selected_addr : current_addr, copy_buf, 16);
        char asc_str[17] = {0};
        for (int i = 0; i < 16; ++i) {
            asc_str[i] = std::isprint(copy_buf[i]) ? copy_buf[i] : '.';
        }
        CopyToClipboard(asc_str, "Texto ASCII");
    }
    ImGui::EndGroup();

    // Status / Feedback message
    if (!status_msg.empty()) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - status_time).count();
        if (elapsed < 3) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%s", status_msg.c_str());
        } else {
            status_msg.clear();
        }
    }

    ImGui::Spacing();

    // 3. Memory Hex Grid
    constexpr size_t VIEW_SIZE = 256;
    uint8_t buffer[VIEW_SIZE] = {0};
    const bool readable = Debugger::MemoryScanner::ReadMemory(current_addr, buffer, VIEW_SIZE);

    if (!readable) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Endereco inacessivel ou memoria nao alocada.");
        return;
    }

    if (ImGui::BeginTable("HexTable", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 270))) {
        ImGui::TableSetupColumn("Endereco (Clique p/ Selecionar)", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn("00 01 02 03  04 05 06 07", ImGuiTableColumnFlags_WidthFixed, 195.0f);
        ImGui::TableSetupColumn("08 09 0A 0B  0C 0D 0E 0F", ImGuiTableColumnFlags_WidthFixed, 195.0f);
        ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (size_t row = 0; row < VIEW_SIZE; row += 16) {
            const uintptr_t row_addr = current_addr + row;
            ImGui::TableNextRow();

            // 1. Address / Selectable
            ImGui::TableSetColumnIndex(0);
            char row_label[32];
            std::snprintf(row_label, sizeof(row_label), "0x%012llx", static_cast<unsigned long long>(row_addr));

            const bool is_selected = has_selection && (selected_addr == row_addr);
            if (ImGui::Selectable(row_label, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                selected_addr = row_addr;
                has_selection = true;
            }

            // Right-Click Context Menu
            if (ImGui::BeginPopupContextItem()) {
                selected_addr = row_addr;
                has_selection = true;

                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "0x%012llx",
                                   static_cast<unsigned long long>(row_addr));
                ImGui::Separator();

                if (ImGui::MenuItem("Copiar Endereco")) {
                    char hex_addr[32];
                    std::snprintf(hex_addr, sizeof(hex_addr), "0x%llx", static_cast<unsigned long long>(row_addr));
                    CopyToClipboard(hex_addr, "Endereco");
                }
                if (ImGui::MenuItem("Copiar Bytes Hex")) {
                    char hex_row[64];
                    std::snprintf(hex_row, sizeof(hex_row),
                                  "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                                  buffer[row + 0], buffer[row + 1], buffer[row + 2], buffer[row + 3],
                                  buffer[row + 4], buffer[row + 5], buffer[row + 6], buffer[row + 7],
                                  buffer[row + 8], buffer[row + 9], buffer[row + 10], buffer[row + 11],
                                  buffer[row + 12], buffer[row + 13], buffer[row + 14], buffer[row + 15]);
                    CopyToClipboard(hex_row, "Bytes Hex");
                }
                if (ImGui::MenuItem("Copiar ASCII")) {
                    char asc_row[17] = {0};
                    for (int i = 0; i < 16; ++i) {
                        asc_row[i] = std::isprint(buffer[row + i]) ? buffer[row + i] : '.';
                    }
                    CopyToClipboard(asc_row, "ASCII");
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Adicionar a Watchlist")) {
                    Debugger::MemoryEditor::Get().AddWatch("Hex Item", row_addr, Debugger::DataType::U32);
                }
                if (ImGui::MenuItem("Monitorar com Watchpoint ('Quem Escreve?')")) {
                    Debugger::BreakpointManager::Get().SetWriteWatchpoint(row_addr);
                }
                if (ImGui::MenuItem("Definir como Flag de Cutscene")) {
                    Debugger::CutsceneDetector::Get().SetAddress(row_addr);
                    Debugger::CutsceneDetector::Get().SetEnabled(true);
                }
                ImGui::EndPopup();
            }

            // 2. First 8 bytes
            ImGui::TableSetColumnIndex(1);
            char hex1[64];
            std::snprintf(hex1, sizeof(hex1), "%02X %02X %02X %02X  %02X %02X %02X %02X",
                          buffer[row + 0], buffer[row + 1], buffer[row + 2], buffer[row + 3],
                          buffer[row + 4], buffer[row + 5], buffer[row + 6], buffer[row + 7]);
            ImGui::TextUnformatted(hex1);

            // 3. Second 8 bytes
            ImGui::TableSetColumnIndex(2);
            char hex2[64];
            std::snprintf(hex2, sizeof(hex2), "%02X %02X %02X %02X  %02X %02X %02X %02X",
                          buffer[row + 8], buffer[row + 9], buffer[row + 10], buffer[row + 11],
                          buffer[row + 12], buffer[row + 13], buffer[row + 14], buffer[row + 15]);
            ImGui::TextUnformatted(hex2);

            // 4. ASCII representation
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

    // 4. Selection Inspector & Editor Bar
    if (has_selection) {
        ImGui::Spacing();
        ImGui::Separator();
        uint8_t sel_bytes[8] = {0};
        Debugger::MemoryScanner::ReadMemory(selected_addr, sel_bytes, sizeof(sel_bytes));

        const uint8_t v_u8 = sel_bytes[0];
        const uint16_t v_u16 = *reinterpret_cast<const uint16_t*>(sel_bytes);
        const uint32_t v_u32 = *reinterpret_cast<const uint32_t*>(sel_bytes);
        const float v_flt = *reinterpret_cast<const float*>(sel_bytes);

        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f),
                           "Selecionado: 0x%llx  |  u8: %u  |  u16: %u  |  u32: %u  |  Float: %.3f",
                           static_cast<unsigned long long>(selected_addr),
                           v_u8, v_u16, v_u32, v_flt);

        static char write_val_buf[32] = "0";
        ImGui::SetNextItemWidth(100.0f);
        ImGui::InputText("Escrever u32##edit", write_val_buf, sizeof(write_val_buf));
        ImGui::SameLine();
        if (ImGui::Button("Gravar Valor")) {
            const uint32_t to_write = static_cast<uint32_t>(std::strtoul(write_val_buf, nullptr, 0));
            Debugger::MemoryScanner::WriteMemory(selected_addr, &to_write, sizeof(to_write));
        }
        ImGui::SameLine();
        if (ImGui::Button("Monitorar Escrita")) {
            Debugger::BreakpointManager::Get().SetWriteWatchpoint(selected_addr);
        }
    }
}

} // namespace UI
