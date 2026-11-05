// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the memory scanner tab's state -- the value the user typed and the type, scope and
// condition pickers -- and the scans those inputs run. tab_scanner_controls.cpp draws them and
// tab_scanner_results.cpp shows what they find.
#include "tab_scanner.h"

#include <cstdlib>

#include <imgui.h>

#include "mem_scanner.h"
#include "tab_scanner_internal.h"
#include "ui_strings.h"

namespace UI::ScannerDetail {

const char* const kTypeNames[] = {
    "1 Byte (u8)",  "2 Bytes (u16)", "4 Bytes (u32)",  "8 Bytes (u64)", "1 Byte (i8)",
    "2 Bytes (i16)", "4 Bytes (i32)", "8 Bytes (i64)", "Float",         "Double",
    "Array of Bytes (AoB)",
};

const char* const kScopeNamesEn[] = {"Executable & Data (<256MB, Fast)",
                                     "Guest Memory (PS4 Heap)", "Full Process"};
const char* const kScopeNamesPt[] = {"Executavel e .data (<256MB, Rapido)",
                                     "Memoria Guest (Heap PS4)", "Processo Completo"};
const char* const kScopeNamesRu[] = {"Исполняемый файл и данные (<256 МБ)", "Память PS4 (Heap)",
                                     "Весь процесс"};

const char* const kComparisonNamesEn[] = {"Exact Value", "Changed", "Unchanged", "Increased",
                                          "Decreased"};
const char* const kComparisonNamesPt[] = {"Valor Exato", "Mudou", "Nao Mudou", "Aumentou",
                                          "Diminuiu"};
const char* const kComparisonNamesRu[] = {"Точное значение", "Изменилось", "Не изменилось",
                                          "Увеличилось", "Уменьшилось"};

namespace {

char s_value_input[kValueBufferBytes] = "20";
int s_selected_type = 2;      // u32
int s_selected_scope = 1;     // GuestMemory (the PS4 heap)
int s_selected_comparison = 0; // Exact

/// The language's list of `count` names: the English one is the fallback.
const char* const* ForLanguage(const char* const* en, const char* const* pt, const char* const* ru) {
    switch (GetCurrentLang()) {
    case BbSettings::LangPortuguese:
        return pt;
    case BbSettings::LangRussian:
        return ru;
    default:
        return en;
    }
}

} // namespace

char* ValueInput() {
    return s_value_input;
}

int& SelectedType() {
    return s_selected_type;
}

int& SelectedScope() {
    return s_selected_scope;
}

int& SelectedComparison() {
    return s_selected_comparison;
}

bool IsPattern() {
    return s_selected_type == kPatternType;
}

double ParsedValue() {
    return std::strtod(s_value_input, nullptr);
}

const char* const* ScopeNames() {
    return ForLanguage(kScopeNamesEn, kScopeNamesPt, kScopeNamesRu);
}

const char* const* ComparisonNames() {
    return ForLanguage(kComparisonNamesEn, kComparisonNamesPt, kComparisonNamesRu);
}

void RunScan(bool first_scan) {
    auto& scanner = Debugger::MemoryScanner::Get();
    const auto scope = static_cast<Debugger::ScanScope>(s_selected_scope);
    if (IsPattern()) {
        scanner.StartPatternScan(s_value_input, scope);
    } else if (first_scan || scanner.GetMatchCount() == 0) {
        scanner.StartFirstScan(static_cast<Debugger::DataType>(s_selected_type),
                               static_cast<Debugger::ScanComparison>(s_selected_comparison),
                               ParsedValue(), scope);
    } else {
        scanner.NextScan(static_cast<Debugger::ScanComparison>(s_selected_comparison),
                         ParsedValue());
    }
}

} // namespace UI::ScannerDetail

namespace UI {

void TabScanner::Render() {
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "%s",
                       L("Memory Scanner", "Scanner de Memoria", "Сканер памяти"));
    ImGui::Separator();

    ScannerDetail::RenderControls();
    ScannerDetail::RenderResults();
}

} // namespace UI
