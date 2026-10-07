// SPDX-License-Identifier: GPL-2.0-or-later
#include "tab_debugger.h"
#include "breakpoint.h"

#include <imgui.h>
#include <cstdio>
#include <cstdlib>

namespace UI {

static char bp_addr_input[32] = "0x00400000";

void TabDebugger::Render() {
    auto& bp = Debugger::BreakpointManager::Get();

    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Depurador & Breakpoints (Cheat Engine 'Find What Writes')");
    ImGui::Separator();

    // 1. Execution Control
    ImGui::Text("Controle de Execucao:");
    ImGui::SameLine();
    const bool paused = bp.IsPaused();
    if (ImGui::Button(paused ? "Retomar Jogo" : "Pausar Jogo", ImVec2(120, 26))) {
        bp.SetPaused(!paused);
    }
    ImGui::SameLine();
    if (ImGui::Button("Avancar 1 Frame", ImVec2(130, 26))) {
        bp.StepFrame();
    }
    ImGui::Spacing();
    ImGui::Separator();

    // 2. Memory Write Watchpoint
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Watchpoint de Escrita ('Quem Escreve Neste Endereco?'):");
    if (bp.HasActiveWatchpoint()) {
        ImGui::Text("Monitorando ativamente: 0x%012llx",
                    static_cast<unsigned long long>(bp.GetWatchedAddress()));
        ImGui::SameLine();
        if (ImGui::Button("Parar Monitoramento")) {
            bp.ClearWriteWatchpoint();
        }
    } else {
        ImGui::TextDisabled("Nenhum watchpoint ativo. Va na aba 'Watchlist' e clique em 'Quem Escreve?'.");
    }

    // 3. Captured Hits Table
    const auto hits = bp.GetHits();
    ImGui::Spacing();
    ImGui::Text("Instrucoes que Escreveram no Endereco (Total: %zu):", hits.size());
    ImGui::SameLine(360.0f);
    if (ImGui::SmallButton("Limpar Registros")) {
        bp.ClearHits();
    }

    if (ImGui::BeginTable("HitsTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 160))) {
        ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Thread ID", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Instrucao (RIP)", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("Detalhes", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < hits.size(); ++i) {
            const auto& hit = hits[i];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%llu ms", static_cast<unsigned long long>(hit.timestamp % 1000000));

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", hit.thread_id);

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "0x%012llx",
                               static_cast<unsigned long long>(hit.rip));

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", hit.disassembly.c_str());
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // 4. Software Breakpoints
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Breakpoints de Codigo (Software Breakpoints):");
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("Endereco (Hex)", bp_addr_input, sizeof(bp_addr_input));
    ImGui::SameLine();
    if (ImGui::Button("Adicionar Breakpoint")) {
        const uintptr_t addr = std::strtoull(bp_addr_input, nullptr, 16);
        bp.AddBreakpoint(addr, "BP manual");
    }

    const auto bps = bp.GetBreakpoints();
    for (const auto& b : bps) {
        ImGui::BulletText("0x%012llx: %s (Original: 0x%02X)",
                          static_cast<unsigned long long>(b.address),
                          b.label.c_str(),
                          b.original_byte);
        ImGui::SameLine();
        ImGui::PushID(reinterpret_cast<void*>(b.address));
        if (ImGui::SmallButton("Remover")) {
            bp.RemoveBreakpoint(b.address);
        }
        ImGui::PopID();
    }
}

} // namespace UI
