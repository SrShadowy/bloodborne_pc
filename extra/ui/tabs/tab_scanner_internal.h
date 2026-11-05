// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the scanner tab's files share. tab_scanner.cpp owns the input state and composes the
// page, tab_scanner_controls.cpp draws the search controls and the progress bar, and
// tab_scanner_results.cpp the results table with its per-row actions. Only this module needs this
// header.
#pragma once

#include <cstddef>
#include <cstdint>

#include "mem_scanner.h"

namespace UI::ScannerDetail {

// ---- The scanner's inputs (the type, scope and condition pickers) ----

/// The data types the type combo lists, in Debugger::DataType's order.
extern const char* const kTypeNames[];
constexpr int kTypeCount = 11;
/// "Array of Bytes (AoB)": the type whose value is a pattern, not a number.
constexpr int kPatternType = 10;

/// The scopes, in Debugger::ScanScope's order, one name list per language.
extern const char* const kScopeNamesEn[];
extern const char* const kScopeNamesPt[];
extern const char* const kScopeNamesRu[];
constexpr int kScopeCount = 3;
/// "Executable & Data": the scope that cannot find dynamic variables.
constexpr int kExecutableScope = 0;

/// The comparisons, in Debugger::ScanComparison's order, one name list per language.
extern const char* const kComparisonNamesEn[];
extern const char* const kComparisonNamesPt[];
extern const char* const kComparisonNamesRu[];
constexpr int kComparisonCount = 5;

/// How long the value buffer is (an address pattern needs some room).
constexpr std::size_t kValueBufferBytes = 128;
/// The matches shown per page of the results table.
constexpr std::size_t kPageSize = 50;
/// The results table's height.
constexpr float kResultsHeight = 310.0f;
/// One previous value, formatted.
constexpr std::size_t kPreviousTextBytes = 64;

/// The value the user typed (kept between frames).
char* ValueInput();

/// The user's picks, kept between frames.
int& SelectedType();
int& SelectedScope();
int& SelectedComparison();

/// The picked type is a pattern.
bool IsPattern();

/// The value the user typed, as a number.
double ParsedValue();

/// The name list of `names_*` picks for the current language.
const char* const* ScopeNames();
const char* const* ComparisonNames();

/// Runs the scan the controls ask for: a pattern scan, a first scan, or a next scan.
void RunScan(bool first_scan);

/// The controls and the progress bar (tab_scanner_controls.cpp).
void RenderControls();

/// The result count, the limit notices, the pager and the table (tab_scanner_results.cpp).
void RenderResults();

/// The match's previous value as text, in its data type.
void FormatPrevious(char (&out)[kPreviousTextBytes], Debugger::DataType type, std::uint64_t raw);

} // namespace UI::ScannerDetail
