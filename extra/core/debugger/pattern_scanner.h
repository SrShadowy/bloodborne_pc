// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace Debugger {

class MemoryScanner;
enum class ScanScope;
struct ScanMatch;

struct PatternToken {
    uint8_t value{0};
    bool wildcard{false};
};

class PatternScanner {
public:
    static bool ParsePattern(const std::string& pattern_str, std::vector<PatternToken>& tokens);
    static void ScanChunk(const uint8_t* buffer, size_t size, uintptr_t base_addr,
                          const std::vector<PatternToken>& tokens,
                          std::vector<ScanMatch>& out_matches, size_t max_matches);
    static bool MatchesAt(const uint8_t* buffer, size_t size, const std::vector<PatternToken>& tokens);
    static std::string FormatBytes(const uint8_t* bytes, size_t count);

    static void ExecuteScan(MemoryScanner* scanner, const std::string& pattern_str, ScanScope scope);
};

} // namespace Debugger
