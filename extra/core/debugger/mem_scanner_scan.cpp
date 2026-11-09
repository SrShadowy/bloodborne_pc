// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the two scans themselves, run on the scanner's worker thread. A first scan walks the
// mapped regions in chunks, decoding a value per type; a next scan filters the previous match list.
// mem_scanner.cpp holds the state they fill in and the API that starts them.
#include "mem_scanner_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "mem_scanner_algo.h"
#include "pattern_scanner.h"

namespace Debugger::detail {

bool MatchesCondition(ScanComparison comp, DataType type, double current, double prev,
                      double target) {
    const double epsilon = (type == DataType::Float || type == DataType::Double) ? 0.001 : 0.0;
    switch (comp) {
    case ScanComparison::Exact:
        return std::fabs(current - target) <= epsilon;
    case ScanComparison::Changed:
        return std::fabs(current - prev) > epsilon;
    case ScanComparison::Unchanged:
        return std::fabs(current - prev) <= epsilon;
    case ScanComparison::Increased:
        return current > (prev + epsilon);
    case ScanComparison::Decreased:
        return current < (prev - epsilon);
    }
    return false;
}

} // namespace Debugger::detail

namespace Debugger {

using detail::kMaxScanMatches;
using detail::kProgressInterval;
using detail::kScanChunkBytes;
using detail::kScanPageBytes;
using detail::kScanReserve;
using detail::MatchesCondition;

namespace {

/// Reads `to_read` bytes at `cur`, falling back to page by page so one guarded page does not drop
/// the whole chunk (the pages that fail then read as zero).
bool ReadChunk(uintptr_t cur, std::size_t to_read, std::uint8_t* chunk) {
    if (MemoryScanner::ReadMemory(cur, chunk, to_read)) {
        return true;
    }
    if (to_read <= kScanPageBytes) {
        return false;
    }
    bool any_page_read = false;
    for (std::size_t offset = 0; offset < to_read; offset += kScanPageBytes) {
        const std::size_t size = std::min(kScanPageBytes, to_read - offset);
        if (!MemoryScanner::ReadMemory(cur + offset, chunk + offset, size)) {
            std::memset(chunk + offset, 0, size);
        } else {
            any_page_read = true;
        }
    }
    return any_page_read;
}

/// The exact-value scan: one typed loop per data type.
void ScanExact(uint8_t* chunk, std::size_t size, uintptr_t base, DataType type, double value,
               std::vector<ScanMatch>& out, bool& limit_hit) {
    switch (type) {
    case DataType::U8:
        ScanExactTyped<uint8_t>(chunk, size, base, value, out, kMaxScanMatches, limit_hit);
        break;
    case DataType::I8:
        ScanExactTyped<int8_t>(chunk, size, base, value, out, kMaxScanMatches, limit_hit);
        break;
    case DataType::U16:
        ScanExactTyped<uint16_t>(chunk, size, base, value, out, kMaxScanMatches, limit_hit);
        break;
    case DataType::I16:
        ScanExactTyped<int16_t>(chunk, size, base, value, out, kMaxScanMatches, limit_hit);
        break;
    case DataType::U32:
        ScanExactTyped<uint32_t>(chunk, size, base, value, out, kMaxScanMatches, limit_hit);
        break;
    case DataType::I32:
        ScanExactTyped<int32_t>(chunk, size, base, value, out, kMaxScanMatches, limit_hit);
        break;
    case DataType::U64:
        ScanExactTyped<uint64_t>(chunk, size, base, value, out, kMaxScanMatches, limit_hit);
        break;
    case DataType::I64:
        ScanExactTyped<int64_t>(chunk, size, base, value, out, kMaxScanMatches, limit_hit);
        break;
    case DataType::Float:
        ScanExactFloat(chunk, size, base, static_cast<float>(value), out, kMaxScanMatches, limit_hit);
        break;
    case DataType::Double:
        ScanExactDouble(chunk, size, base, value, out, kMaxScanMatches, limit_hit);
        break;
    case DataType::Pattern:
        break;
    }
}

} // namespace

void MemoryScanner::DoFirstScan(DataType type, ScanComparison comp, double value, ScanScope scope) {
    const auto regions = QueryRegions(scope);
    const std::size_t type_size = GetTypeSize(type);
    std::vector<uint8_t> chunk(kScanChunkBytes);

    std::vector<ScanMatch> temp_matches;
    temp_matches.reserve(kScanReserve);
    bool limit_hit = false;

    std::size_t total_bytes = 0;
    for (const auto& region : regions) {
        total_bytes += (region.end - region.start);
    }
    std::size_t processed_bytes = 0;

    // A match limit ends both loops at once.
    for (const auto& region : regions) {
        if (cancel_requested.load()) {
            break;
        }
        for (uintptr_t cur = region.start; cur < region.end; cur += kScanChunkBytes) {
            if (cancel_requested.load()) {
                break;
            }
            const std::size_t to_read =
                std::min(kScanChunkBytes, static_cast<std::size_t>(region.end - cur));
            if (ReadChunk(cur, to_read, chunk.data())) {
                if (comp == ScanComparison::Exact) {
                    ScanExact(chunk.data(), to_read, cur, type, value, temp_matches, limit_hit);
                } else {
                    // The first scan has no earlier value: the condition compares against itself.
                    for (std::size_t off = 0; off + type_size <= to_read; off += type_size) {
                        const double val = DecodeValue(type, chunk.data() + off);
                        if (!MatchesCondition(comp, type, val, val, value)) {
                            continue;
                        }
                        const uint64_t raw = EncodeRaw(type, chunk.data() + off);
                        temp_matches.push_back({cur + off, raw, raw});
                        if (temp_matches.size() >= kMaxScanMatches) {
                            limit_hit = true;
                            break;
                        }
                    }
                }
                if (limit_hit) {
                    break;
                }
            }
            processed_bytes += to_read;
            if (total_bytes > 0) {
                progress.store(static_cast<float>(processed_bytes) /
                               static_cast<float>(total_bytes));
            }
        }
        if (limit_hit) {
            break;
        }
    }

    match_limit_hit.store(limit_hit);
    if (!cancel_requested.load()) {
        std::lock_guard<std::mutex> lock(mutex);
        current_type = type;
        matches = std::move(temp_matches);
    }
    scanning.store(false);
    progress.store(1.0f);
}

void MemoryScanner::DoNextScan(ScanComparison comp, double value) {
    std::vector<ScanMatch> prev_list;
    DataType type;
    {
        std::lock_guard<std::mutex> lock(mutex);
        prev_list = matches;
        type = current_type;
    }

    if (prev_list.empty()) {
        scanning.store(false);
        progress.store(1.0f);
        return;
    }

    // A pattern keeps matching the same bytes in place.
    if (type == DataType::Pattern) {
        std::vector<PatternToken> tokens;
        {
            std::lock_guard<std::mutex> lock(mutex);
            tokens = current_tokens;
        }
        if (tokens.empty()) {
            scanning.store(false);
            progress.store(1.0f);
            return;
        }
        const std::size_t plen = tokens.size();
        std::vector<uint8_t> pbuf(plen);
        std::vector<ScanMatch> next_matches;
        for (std::size_t i = 0; i < prev_list.size(); ++i) {
            if (cancel_requested.load()) {
                break;
            }
            ScanMatch& match = prev_list[i];
            if (ReadMemory(match.address, pbuf.data(), plen) &&
                PatternScanner::MatchesAt(pbuf.data(), plen, tokens)) {
                match.prev_raw = match.current_raw;
                uint64_t raw = 0;
                std::memcpy(&raw, pbuf.data(), std::min(sizeof(raw), plen));
                match.current_raw = raw;
                next_matches.push_back(match);
            }
            if ((i % kProgressInterval) == 0) {
                progress.store(static_cast<float>(i) / static_cast<float>(prev_list.size()));
            }
        }
        if (!cancel_requested.load()) {
            std::lock_guard<std::mutex> lock(mutex);
            matches = std::move(next_matches);
        }
        scanning.store(false);
        progress.store(1.0f);
        return;
    }

    const std::size_t type_size = GetTypeSize(type);
    std::vector<ScanMatch> next_matches;
    next_matches.reserve(std::min(prev_list.size(), kScanReserve));

    uint8_t buffer[8];
    for (std::size_t i = 0; i < prev_list.size(); ++i) {
        if (cancel_requested.load()) {
            break;
        }
        ScanMatch& match = prev_list[i];
        if (ReadMemory(match.address, buffer, type_size)) {
            const double current_val = DecodeValue(type, buffer);
            const double prev_val = DecodeValue(type, &match.current_raw);
            if (MatchesCondition(comp, type, current_val, prev_val, value)) {
                match.prev_raw = match.current_raw;
                match.current_raw = EncodeRaw(type, buffer);
                next_matches.push_back(match);
            }
        }
        if ((i % kProgressInterval) == 0) {
            progress.store(static_cast<float>(i) / static_cast<float>(prev_list.size()));
        }
    }

    if (!cancel_requested.load()) {
        std::lock_guard<std::mutex> lock(mutex);
        matches = std::move(next_matches);
    }
    scanning.store(false);
    progress.store(1.0f);
}

} // namespace Debugger
