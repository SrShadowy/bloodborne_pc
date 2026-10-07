// SPDX-License-Identifier: GPL-2.0-or-later
#include "tab_hexview.h"
#include "mem_scanner.h"

#include <imgui.h>
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace UI {

static char addr_input[32] = "0x00400000";
static uintptr_t current_addr = 0x00400000;

void TabHexView::Render() {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Visualizador Hexadecimal (Hex Inspector)");
    ImGui::Separator();

    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputText("Endereco (Hex)", addr_input, sizeof(addr_input), ImGuiInputTextFlags_EnterReturnsTrue)) {
        current_addr = std::strtoull(addr_input, nullptr, 16);
    }
    ImGui::SameLine();
    if (ImGui::Button("Ir", ImVec2(40, 0))) {
        current_addr = std::strtoull(addr_input, nullptr, 16);
    }
    ImGui::SameLine();
    if (ImGui::Button("< -256 B") && current_addr >= 256) {
        current_addr -= 256;
        std::snprintf(addr_input, sizeof(addr_input), "0x%llx", static_cast<unsigned long long>(current_addr));
    }
    ImGui::SameLine();
    if (ImGui::Button("+256 B >")) {
        current_addr += 256;
        std::snprintf(addr_input, sizeof(addr_input), "0x%llx", static_cast<unsigned long long>(current_addr));
    }
    ImGui::EndGroup();

    ImGui::Spacing();

    // Read 256 bytes
    constexpr size_t VIEW_SIZE = 256;
    uint8_t buffer[VIEW_SIZE] = {0};
    const bool readable = Debugger::MemoryScanner::ReadMemory(current_addr, buffer, VIEW_SIZE);

    if (!readable) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Endereco inacessivel ou memoria nao alocada.");
        return;
    }

    if (ImGui::BeginTable("HexTable", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 320))) {
        ImGui::TableSetupColumn("Endereco", ImGuiTableColumnFlags_WidthFixed, 130.0f);
        ImGui::TableSetupColumn("00 01 02 03  04 05 06 07", ImGuiTableColumnFlags_WidthFixed, 195.0f);
        ImGui::TableSetupColumn("08 09 0A 0B  0C 0D 0E 0F", ImGuiTableColumnFlags_WidthFixed, 195.0f);
        ImGui::TableSetupColumn("ASCII", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (size_t row = 0; row < VIEW_SIZE; row += 16) {
            const uintptr_t row_addr = current_addr + row;
            ImGui::TableNextRow();

            // 1. Address
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "0x%012llx", static_cast<unsigned long long>(row_addr));

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
}

} // namespace UI
