// SPDX-License-Identifier: GPL-2.0-or-later
#include "tab_scanner.h"
#include "mem_scanner.h"
#include "mem_editor.h"

#include <imgui.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace UI {

static char value_input[128] = "1";
static int selected_type = 2; // Default u32
static int selected_scope = 0; // Default ExecutableOnly
static int selected_comp = 0; // Default Exact

void TabScanner::Render() {
    auto& scanner = Debugger::MemoryScanner::Get();
    auto& editor = Debugger::MemoryEditor::Get();

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Scanner de Memoria (Cheat Engine)");
    ImGui::Separator();

    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputText("Valor", value_input, sizeof(value_input));

    const char* type_names[] = {
        "1 Byte (u8)", "2 Bytes (u16)", "4 Bytes (u32)", "8 Bytes (u64)",
        "1 Byte (i8)", "2 Bytes (i16)", "4 Bytes (i32)", "8 Bytes (i64)",
        "Float", "Double"
    };
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Tipo", &selected_type, type_names, IM_ARRAYSIZE(type_names));

    const char* scope_names[] = {
        "Executavel e .data (<256MB, Rapido)",
        "Memoria Guest (Heap PS4)",
        "Processo Completo"
    };
    ImGui::SetNextItemWidth(250.0f);
    ImGui::Combo("Regiao", &selected_scope, scope_names, IM_ARRAYSIZE(scope_names));

    const char* comp_names[] = {
        "Valor Exato", "Mudou", "Nao Mudou", "Aumentou", "Diminuiu"
    };
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Condicao", &selected_comp, comp_names, IM_ARRAYSIZE(comp_names));
    ImGui::EndGroup();

    ImGui::SameLine(320.0f);
    ImGui::BeginGroup();
    const bool is_first = scanner.GetMatchCount() == 0;
    const double val = std::strtod(value_input, nullptr);

    if (ImGui::Button("Primeiro Scan", ImVec2(130, 32))) {
        scanner.StartFirstScan(static_cast<Debugger::DataType>(selected_type),
                               static_cast<Debugger::ScanComparison>(selected_comp),
                               val,
                               static_cast<Debugger::ScanScope>(selected_scope));
    }
    ImGui::SameLine();
    if (ImGui::Button("Proximo Scan", ImVec2(130, 32))) {
        scanner.NextScan(static_cast<Debugger::ScanComparison>(selected_comp), val);
    }

    if (ImGui::Button("Resetar Busca", ImVec2(130, 26))) {
        scanner.Reset();
    }
    ImGui::EndGroup();

    if (scanner.IsScanning()) {
        ImGui::ProgressBar(scanner.GetProgress(), ImVec2(-1, 6), "Escaneando...");
    }

    ImGui::Spacing();
    const size_t count = scanner.GetMatchCount();
    ImGui::Text("Resultados encontrados: %zu", count);
    ImGui::Separator();

    if (count > 0) {
        static int current_page = 0;
        constexpr size_t PAGE_SIZE = 50;
        const int total_pages = static_cast<int>((count + PAGE_SIZE - 1) / PAGE_SIZE);
        if (current_page >= total_pages) current_page = total_pages - 1;
        if (current_page < 0) current_page = 0;

        ImGui::Text("Pagina %d de %d", current_page + 1, total_pages);
        ImGui::SameLine();
        if (ImGui::Button("< Anterior") && current_page > 0) current_page--;
        ImGui::SameLine();
        if (ImGui::Button("Proxima >") && current_page + 1 < total_pages) current_page++;

        if (ImGui::BeginTable("ScanResults", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 320))) {
            ImGui::TableSetupColumn("Endereco", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Valor Atual", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Valor Anterior", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Acao", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            const auto matches = scanner.GetMatches(current_page * PAGE_SIZE, PAGE_SIZE);
            for (size_t i = 0; i < matches.size(); ++i) {
                const auto& m = matches[i];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("0x%012llx", static_cast<unsigned long long>(m.address));

                double cur_num = 0;
                std::string cur_str;
                Debugger::MemoryScanner::ReadFormatted(m.address, static_cast<Debugger::DataType>(selected_type), cur_num, cur_str);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", cur_str.c_str());

                double prev_num = 0;
                std::string prev_str;
                Debugger::MemoryScanner::ReadFormatted(m.address, static_cast<Debugger::DataType>(selected_type), prev_num, prev_str);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", prev_str.c_str());

                ImGui::TableSetColumnIndex(3);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::SmallButton("+ Watchlist")) {
                    char label[32];
                    std::snprintf(label, sizeof(label), "Item %zu", editor.GetCount() + 1);
                    editor.AddWatch(label, m.address, static_cast<Debugger::DataType>(selected_type));
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
}

} // namespace UI
