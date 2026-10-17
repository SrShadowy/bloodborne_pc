// SPDX-License-Identifier: GPL-2.0-or-later
#include "pattern_scanner.h"
#include "mem_scanner.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <algorithm>

namespace Debugger {

bool PatternScanner::ParsePattern(const std::string& pattern_str, std::vector<PatternToken>& tokens) {
    tokens.clear();
    size_t i = 0;
    const size_t n = pattern_str.size();
    while (i < n) {
        while (i < n && (std::isspace(static_cast<unsigned char>(pattern_str[i])) || pattern_str[i] == ',')) {
            i++;
        }
        if (i >= n) break;

        char c = pattern_str[i];
        if (c == '?' || c == '*') {
            tokens.push_back({0x00, true});
            i++;
            if (i < n && (pattern_str[i] == '?' || pattern_str[i] == '*')) {
                i++;
            }
        } else if (std::isxdigit(static_cast<unsigned char>(c))) {
            char hex_buf[3] = {c, 0, 0};
            i++;
            if (i < n && std::isxdigit(static_cast<unsigned char>(pattern_str[i]))) {
                hex_buf[1] = pattern_str[i];
                i++;
            }
            uint8_t byte = static_cast<uint8_t>(std::strtoul(hex_buf, nullptr, 16));
            tokens.push_back({byte, false});
        } else {
            return false;
        }
    }
    return !tokens.empty();
}

bool PatternScanner::MatchesAt(const uint8_t* buffer, size_t size, const std::vector<PatternToken>& tokens) {
    if (!buffer || size < tokens.size()) return false;
    for (size_t k = 0; k < tokens.size(); ++k) {
        if (!tokens[k].wildcard && buffer[k] != tokens[k].value) return false;
    }
    return true;
}

std::string PatternScanner::FormatBytes(const uint8_t* bytes, size_t count) {
    if (!bytes || count == 0) return "";
    std::string res;
    char buf[8];
    for (size_t i = 0; i < count; ++i) {
        std::snprintf(buf, sizeof(buf), "%02X ", bytes[i]);
        res += buf;
    }
    if (!res.empty() && res.back() == ' ') res.pop_back();
    return res;
}

void PatternScanner::ScanChunk(const uint8_t* buffer, size_t size, uintptr_t base_addr,
                              const std::vector<PatternToken>& tokens,
                              std::vector<ScanMatch>& out_matches, size_t max_matches) {
    if (!buffer || tokens.empty() || size < tokens.size()) return;
    const size_t M = tokens.size();

    size_t first_fixed_idx = 0;
    bool has_fixed = false;
    for (size_t i = 0; i < M; ++i) {
        if (!tokens[i].wildcard) {
            first_fixed_idx = i;
            has_fixed = true;
            break;
        }
    }

    if (!has_fixed) {
        for (size_t off = 0; off <= size - M; ++off) {
            uint64_t raw = 0;
            std::memcpy(&raw, buffer + off, std::min(sizeof(raw), M));
            out_matches.push_back({base_addr + off, raw, raw});
            if (out_matches.size() >= max_matches) return;
        }
        return;
    }

    const uint8_t first_val = tokens[first_fixed_idx].value;
    size_t off = 0;
    const size_t max_off = size - M;

    while (off <= max_off) {
        const uint8_t* p = static_cast<const uint8_t*>(
            std::memchr(buffer + off + first_fixed_idx, first_val, (max_off - off + 1)));
        if (!p) break;

        off = (p - buffer) - first_fixed_idx;
        bool match = true;
        for (size_t k = 0; k < M; ++k) {
            if (!tokens[k].wildcard && buffer[off + k] != tokens[k].value) {
                match = false;
                break;
            }
        }
        if (match) {
            uint64_t raw = 0;
            std::memcpy(&raw, buffer + off, std::min(sizeof(raw), M));
            out_matches.push_back({base_addr + off, raw, raw});
            if (out_matches.size() >= max_matches) return;
        }
        off++;
    }
}

void PatternScanner::ExecuteScan(MemoryScanner* scanner, const std::string& pattern_str, ScanScope scope) {
    if (!scanner) return;

    std::vector<PatternToken> tokens;
    if (!ParsePattern(pattern_str, tokens)) {
        scanner->scanning.store(false);
        scanner->progress.store(1.0f);
        return;
    }

    const auto regions = scanner->QueryRegions(scope);
    const size_t M = tokens.size();
    const size_t overlap = (M > 1) ? (M - 1) : 0;
    constexpr size_t CHUNK_SIZE = 256 * 1024;
    std::vector<uint8_t> chunk(CHUNK_SIZE);

    constexpr size_t MAX_MATCHES = 100000;
    std::vector<ScanMatch> temp_matches;
    temp_matches.reserve(5000);

    size_t total_bytes = 0;
    for (const auto& r : regions) total_bytes += (r.end - r.start);
    size_t processed_bytes = 0;

    for (const auto& r : regions) {
        if (scanner->cancel_requested.load()) break;
        uintptr_t cur = r.start;
        while (cur < r.end) {
            if (scanner->cancel_requested.load()) break;
            const size_t to_read = std::min(CHUNK_SIZE, static_cast<size_t>(r.end - cur));
            bool ok = MemoryScanner::ReadMemory(cur, chunk.data(), to_read);
            if (!ok && to_read > 4096) {
                constexpr size_t PAGE_SIZE = 4096;
                bool any_page_read = false;
                for (size_t poff = 0; poff < to_read; poff += PAGE_SIZE) {
                    size_t psz = std::min(PAGE_SIZE, to_read - poff);
                    if (!MemoryScanner::ReadMemory(cur + poff, chunk.data() + poff, psz)) {
                        std::memset(chunk.data() + poff, 0, psz);
                    } else {
                        any_page_read = true;
                    }
                }
                ok = any_page_read;
            }
            if (ok) {
                ScanChunk(chunk.data(), to_read, cur, tokens, temp_matches, MAX_MATCHES);
                if (temp_matches.size() >= MAX_MATCHES) goto done;
            }
            processed_bytes += to_read;
            if (total_bytes > 0) {
                scanner->progress.store(static_cast<float>(processed_bytes) / static_cast<float>(total_bytes));
            }

            if (cur + to_read >= r.end) break;
            cur += (to_read > overlap) ? (to_read - overlap) : to_read;
        }
    }

done:
    if (!scanner->cancel_requested.load()) {
        std::lock_guard<std::mutex> lock(scanner->mutex);
        scanner->current_type = DataType::Pattern;
        scanner->current_tokens = std::move(tokens);
        scanner->current_pattern_str = pattern_str;
        scanner->matches = std::move(temp_matches);
    }
    scanner->scanning.store(false);
    scanner->progress.store(1.0f);
}

} // namespace Debugger
