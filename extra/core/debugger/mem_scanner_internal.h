// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the scanner's files share. mem_scanner.cpp holds the public API and the match list,
// mem_scanner_scan.cpp runs the two scans on their worker thread, and the rest of the module is
// next to them (mem_scanner_io.cpp reads maps and memory, mem_scanner_types.cpp the data types,
// pattern_scanner.cpp patterns). Only this module needs this header.
#pragma once

#include <cstddef>
#include <cstdint>

#include "mem_scanner.h"

namespace Debugger::detail {

// ---- The scans' limits ----

/// How much memory one read of the scan covers.
constexpr std::size_t kScanChunkBytes = 256 * 1024;
/// A page's size, for the read that falls back to page-by-page when a chunk fails.
constexpr std::size_t kScanPageBytes = 4096;
/// A first scan stops at this many matches, so a broad search cannot exhaust memory.
constexpr std::size_t kMaxScanMatches = 5'000'000;
/// How many matches the first scan's list starts with.
constexpr std::size_t kScanReserve = 50'000;
/// The progress is stored this often (every N matches examined).
constexpr std::size_t kProgressInterval = 1000;

/// Whether `current` satisfies `comp` against `prev` and `target`; floats compare with a tolerance.
bool MatchesCondition(ScanComparison comp, DataType type, double current, double prev, double target);

} // namespace Debugger::detail
