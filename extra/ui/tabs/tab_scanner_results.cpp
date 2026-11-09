// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the memory scanner's results -- how many matched, the notices when the limit was reached
// or the scope cannot find dynamic values, the pager, and one row per match with its actions
// (watch, hex, disassembly, who writes it).
#include "tab_scanner_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include <imgui.h>

#include "breakpoint.h"
#include "mem_editor.h"
#include "mem_scanner.h"
#include "ui_manager.h"
#include "ui_strings.h"
#include "view_memviewer.h"

namespace UI::ScannerDetail {
namespace {

// The row actions' colours.
constexpr ImVec4 kWatchButton{0.20f, 0.45f, 0.35f, 0.75f};
constexpr ImVec4 kWatchButtonHover{0.28f, 0.60f, 0.45f, 0.95f};
constexpr ImVec4 kHexButton{0.22f, 0.32f, 0.46f, 0.75f};
constexpr ImVec4 kHexButtonHover{0.30f, 0.44f, 0.64f, 0.95f};
constexpr ImVec4 kAsmButton{0.18f, 0.40f, 0.48f, 0.75f};
constexpr ImVec4 kAsmButtonHover{0.25f, 0.52f, 0.62f, 0.95f};
constexpr ImVec4 kWriteButton{0.36f, 0.26f, 0.50f, 0.75f};
constexpr ImVec4 kWriteButtonHover{0.48f, 0.35f, 0.68f, 0.95f};

/// One row's actions, at the table's last column.
void RenderRowActions(const Debugger::ScanMatch& match, Debugger::DataType scan_type) {
    auto& editor = Debugger::MemoryEditor::Get();

    ImGui::PushStyleColor(ImGuiCol_Button, kWatchButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kWatchButtonHover);
    if (ImGui::SmallButton(L("+ Watch", "+ Watch", "+ Список"))) {
        char label[32];
        std::snprintf(label, sizeof(label), "Item %zu", editor.GetCount() + 1);
        // A pattern's match has no numeric type: watch it as a u32.
        editor.AddWatch(label, match.address,
                        scan_type == Debugger::DataType::Pattern ? Debugger::DataType::U32 : scan_type);
        UiManager::SetStatus(L("Address added to Watchlist!", "Endereco adicionado a Watchlist!",
                               "Адрес добавлен в таблицу!"));
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, kHexButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kHexButtonHover);
    if (ImGui::SmallButton("Hex")) {
        ViewMemViewer::NavigateHex(match.address);
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, kAsmButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAsmButtonHover);
    if (ImGui::SmallButton("ASM")) {
        ViewMemViewer::NavigateDisasm(match.address);
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, kWriteButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kWriteButtonHover);
    if (ImGui::SmallButton(L("Who Writes?", "Quem Escreve?", "Кто пишет?"))) {
        Debugger::BreakpointManager::Get().SetWriteWatchpoint(match.address);
        UiManager::RequestTab(TabId::Debugger);
    }
    ImGui::PopStyleColor(2);
}

/// The paginated table of matches.
void RenderTable(std::size_t count) {
    static int current_page = 0;

    const int total_pages = static_cast<int>((count + kPageSize - 1) / kPageSize);
    current_page = std::clamp(current_page, 0, total_pages - 1);

    ImGui::Text(L("Page %d of %d", "Pagina %d de %d", "Страница %d из %d"), current_page + 1,
                total_pages);
    ImGui::SameLine();
    if (ImGui::Button(L("< Previous", "< Anterior", "< Назад")) && current_page > 0) {
        --current_page;
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Next >", "Proxima >", "Вперед >")) && current_page + 1 < total_pages) {
        ++current_page;
    }

    if (!ImGui::BeginTable("ScanResults", 4,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                           ImVec2(0, kResultsHeight))) {
        return;
    }
    ImGui::TableSetupColumn(L("Address", "Endereco", "Адрес"), ImGuiTableColumnFlags_WidthFixed,
                            140.0f);
    ImGui::TableSetupColumn(L("Current Value", "Valor Atual", "Текущее"),
                            ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn(L("Previous Value", "Valor Anterior", "Предыдущее"),
                            ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn(L("Action", "Acao", "Действие"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    auto& scanner = Debugger::MemoryScanner::Get();
    const auto scan_type = scanner.GetCurrentType();
    const auto matches =
        scanner.GetMatches(static_cast<std::size_t>(current_page) * kPageSize, kPageSize);
    for (std::size_t i = 0; i < matches.size(); ++i) {
        const Debugger::ScanMatch& match = matches[i];
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(0.40f, 0.80f, 1.0f, 1.0f), "0x%012llx",
                           static_cast<unsigned long long>(match.address));

        double current = 0;
        std::string current_text;
        Debugger::MemoryScanner::ReadFormatted(match.address, scan_type, current, current_text);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(current_text.c_str());

        char previous[kPreviousTextBytes];
        FormatPrevious(previous, scan_type, match.prev_raw);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(previous);

        ImGui::TableSetColumnIndex(3);
        ImGui::PushID(static_cast<int>(i));
        RenderRowActions(match, scan_type);
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace

void FormatPrevious(char (&out)[kPreviousTextBytes], Debugger::DataType type, std::uint64_t raw) {
    switch (type) {
    case Debugger::DataType::U8:
        std::snprintf(out, sizeof(out), "%u", static_cast<std::uint8_t>(raw));
        break;
    case Debugger::DataType::I8:
        std::snprintf(out, sizeof(out), "%d", static_cast<std::int8_t>(raw));
        break;
    case Debugger::DataType::U16:
        std::snprintf(out, sizeof(out), "%u", static_cast<std::uint16_t>(raw));
        break;
    case Debugger::DataType::I16:
        std::snprintf(out, sizeof(out), "%d", static_cast<std::int16_t>(raw));
        break;
    case Debugger::DataType::U32:
        std::snprintf(out, sizeof(out), "%u", static_cast<std::uint32_t>(raw));
        break;
    case Debugger::DataType::I32:
        std::snprintf(out, sizeof(out), "%d", static_cast<std::int32_t>(raw));
        break;
    case Debugger::DataType::U64:
        std::snprintf(out, sizeof(out), "%llu", static_cast<unsigned long long>(raw));
        break;
    case Debugger::DataType::I64:
        std::snprintf(out, sizeof(out), "%lld", static_cast<long long>(raw));
        break;
    case Debugger::DataType::Float: {
        float value = 0.0f;
        std::memcpy(&value, &raw, sizeof(value));
        std::snprintf(out, sizeof(out), "%.3f", value);
        break;
    }
    case Debugger::DataType::Double: {
        double value = 0.0;
        std::memcpy(&value, &raw, sizeof(value));
        std::snprintf(out, sizeof(out), "%.4f", value);
        break;
    }
    case Debugger::DataType::Pattern:
        std::snprintf(out, sizeof(out), "Pattern");
        break;
    }
}

void RenderResults() {
    auto& scanner = Debugger::MemoryScanner::Get();

    ImGui::Spacing();
    const std::size_t count = scanner.GetMatchCount();
    ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f), "%s",
                       L("Search Results:", "Resultados da Busca:", "Результаты поиска:"));
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu %s)", count, L("matches", "encontrados", "найдено"));

    if (scanner.HasHitMatchLimit()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "[%s]",
                           L("LIMIT REACHED (5,000,000)", "LIMITE ATINGIDO (5.000.000)",
                             "ДОСТИГНУТ ЛИМИТ (5 000 000)"));
    }
    ImGui::Separator();

    if (scanner.HasHitMatchLimit()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.30f, 1.0f), "%s",
                           L("Notice: Results were capped at 5,000,000 to prevent running out of memory. Perform a 'Next Scan' with changed values or use a more specific number.",
                             "Aviso: Resultados limitados a 5.000.000 para evitar esgotar a memoria. Faca um 'Proximo Scan' com valor alterado ou use um numero mais especifico.",
                             "Примечание: Результаты ограничены до 5 000 000. Выполните 'След. поиск' для фильтрации."));
    }
    // The executable's own data cannot hold the game's live values.
    if (count == 0 && !scanner.IsScanning() && SelectedScope() == kExecutableScope) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f), "%s",
                           L("Tip: Dynamic variables (HP, Vials, Echoes) are located in 'Guest Memory (PS4 Heap)'.",
                             "Dica: Variaveis dinamicas (HP, Vials, Ecos) ficam em 'Memoria Guest (Heap PS4)'.",
                             "Подсказка: Динамические данные (HP, склянки, отголоски) находятся в 'Памяти PS4 (Heap)'."));
    }

    if (count > 0) {
        RenderTable(count);
    }
}

} // namespace UI::ScannerDetail
