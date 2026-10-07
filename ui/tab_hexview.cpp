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
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::InputText("Endereco (Hex)", addr_input, sizeof(addr_input), ImGuiInputTextFlags_EnterReturnsTrue)) {
        current_addr = std::strtoull(addr_input, nullptr, 16);
    }
    ImGui::SameLine();
    if (ImGui::Button("Ir")) {
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

    if (ImGui::BeginChild("HexScroll", ImVec2(0, 320), true)) {
        for (size_t row = 0; row < VIEW_SIZE; row += 16) {
            const uintptr_t row_addr = current_addr + row;
            // 1. Address
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "0x%012llx: ", static_cast<unsigned long long>(row_addr));

            // 2. Hex bytes
            for (size_t col = 0; col < 16; ++col) {
                ImGui::SameLine();
                if (col == 8) ImGui::TextUnformatted(" ");
                ImGui::SameLine();
                const uint8_t b = buffer[row + col];
                if (b == 0) {
                    ImGui::TextDisabled("%02X", b);
                } else {
                    ImGui::Text("%02X", b);
                }
            }

            // 3. ASCII representation
            ImGui::SameLine(460.0f);
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "|");
            for (size_t col = 0; col < 16; ++col) {
                ImGui::SameLine();
                const char c = static_cast<char>(buffer[row + col]);
                const char disp = (std::isprint(static_cast<unsigned char>(c))) ? c : '.';
                ImGui::Text("%c", disp);
            }
        }
    }
    ImGui::EndChild();
}

} // namespace UI
