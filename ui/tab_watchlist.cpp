// SPDX-License-Identifier: GPL-2.0-or-later
#include "tab_watchlist.h"
#include "mem_editor.h"
#include "cutscene_detector.h"
#include "breakpoint.h"

#include <imgui.h>
#include <cstdio>
#include <cstdlib>

namespace UI {

void TabWatchlist::Render() {
    auto& editor = Debugger::MemoryEditor::Get();
    auto& cutscene = Debugger::CutsceneDetector::Get();
    auto& bp = Debugger::BreakpointManager::Get();

    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "Tabela de Enderecos / Watchlist (Cheat Table)");
    ImGui::Separator();

    // Cutscene flag status indicator
    if (cutscene.IsEnabled()) {
        const uintptr_t cs_addr = cutscene.GetAddress();
        const bool active = cutscene.IsCutsceneActive();
        ImGui::TextColored(active ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) : ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                           "Flag de Cutscene Ativa: [0x%012llx] -> Estado: %s (DrawPipe %s)",
                           static_cast<unsigned long long>(cs_addr),
                           active ? "CUTSCENE (1)" : "GAMEPLAY (0)",
                           active ? "Sincrono/Desligado" : "Acelerado/Ligado");
        ImGui::SameLine();
        if (ImGui::SmallButton("Desvincular")) {
            cutscene.SetEnabled(false);
        }
        ImGui::Separator();
    }

    const auto entries = editor.GetEntries();
    if (entries.empty()) {
        ImGui::TextDisabled("Nenhum endereco salvo. Adicione resultados da aba 'Scanner' ou crie um novo.");
    } else {
        if (ImGui::BeginTable("WatchlistTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 320))) {
            ImGui::TableSetupColumn("Descricao", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Endereco", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Tipo", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Valor Atual", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Congelar", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Acoes", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& e = entries[i];
                ImGui::TableNextRow();

                ImGui::PushID(static_cast<int>(i));

                // 1. Label
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", e.label.c_str());

                // 2. Address
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("0x%012llx", static_cast<unsigned long long>(e.address));

                // 3. Type
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", Debugger::MemoryScanner::GetTypeName(e.type));

                // 4. Value / Edit
                ImGui::TableSetColumnIndex(3);
                double cur_num = 0;
                std::string cur_str;
                Debugger::MemoryScanner::ReadFormatted(e.address, e.type, cur_num, cur_str);

                char edit_buf[32];
                std::snprintf(edit_buf, sizeof(edit_buf), "%s", cur_str.c_str());
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText("##val", edit_buf, sizeof(edit_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    const double new_val = std::strtod(edit_buf, nullptr);
                    editor.WriteValue(i, new_val);
                }

                // 5. Freeze
                ImGui::TableSetColumnIndex(4);
                bool frozen = e.frozen;
                if (ImGui::Checkbox("##frz", &frozen)) {
                    editor.SetFrozen(i, frozen);
                    editor.SetFreezeValue(i, cur_num);
                }

                // 6. Actions
                ImGui::TableSetColumnIndex(5);
                if (ImGui::SmallButton("Flag Cutscene")) {
                    cutscene.SetAddress(e.address);
                    cutscene.SetCutsceneValue(1);
                    cutscene.SetEnabled(true);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Quem Escreve?")) {
                    bp.SetWriteWatchpoint(e.address);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remover")) {
                    editor.RemoveWatch(i);
                    ImGui::PopID();
                    break;
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Limpar Lista")) {
        editor.Clear();
    }
}

} // namespace UI
