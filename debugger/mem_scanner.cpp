// SPDX-License-Identifier: GPL-2.0-or-later
#include "mem_scanner.h"
#include "pattern_scanner.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <csetjmp>
#include <csignal>
#include <unistd.h>
#include "gpu/bbgpu.h"

extern "C" __thread sigjmp_buf* runtime_fault_recover;
extern "C" int runtime_memory_write_backing(uintptr_t address, const void* data, uint64_t size);

namespace Debugger {

MemoryScanner& MemoryScanner::Get() {
    static MemoryScanner instance;
    return instance;
}


static bool MatchesCondition(ScanComparison comp, DataType type, double current, double prev, double target) {
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

MemoryScanner::~MemoryScanner() {
    CancelScan();
    if (scan_thread.joinable()) {
        scan_thread.join();
    }
}

void MemoryScanner::CancelScan() {
    cancel_requested.store(true);
}

DataType MemoryScanner::GetCurrentType() const {
    std::lock_guard<std::mutex> lock(mutex);
    return current_type;
}

size_t MemoryScanner::GetCurrentPatternLength() const {
    std::lock_guard<std::mutex> lock(mutex);
    return current_tokens.size();
}

std::string MemoryScanner::GetCurrentPatternString() const {
    std::lock_guard<std::mutex> lock(mutex);
    return current_pattern_str;
}

void MemoryScanner::StartPatternScan(const std::string& pattern_str, ScanScope scope) {
    if (scanning.load()) return;
    if (scan_thread.joinable()) {
        scan_thread.join();
    }

    cancel_requested.store(false);
    scanning.store(true);
    progress.store(0.0f);

    scan_thread = std::thread([this, pattern_str, scope]() {
        PatternScanner::ExecuteScan(this, pattern_str, scope);
    });
}

void MemoryScanner::StartFirstScan(DataType type, ScanComparison comp, double value, ScanScope scope) {
    if (scanning.load()) return;
    if (scan_thread.joinable()) {
        scan_thread.join();
    }

    cancel_requested.store(false);
    scanning.store(true);
    progress.store(0.0f);

    scan_thread = std::thread([this, type, comp, value, scope]() {
        DoFirstScan(type, comp, value, scope);
    });
}

void MemoryScanner::DoFirstScan(DataType type, ScanComparison comp, double value, ScanScope scope) {
    const auto regions = QueryRegions(scope);
    const size_t type_size = GetTypeSize(type);
    constexpr size_t CHUNK_SIZE = 256 * 1024; // 256 KB chunks
    std::vector<uint8_t> chunk(CHUNK_SIZE);

    constexpr size_t MAX_MATCHES = 300000;
    std::vector<ScanMatch> temp_matches;
    temp_matches.reserve(10000);

    size_t total_bytes = 0;
    for (const auto& r : regions) total_bytes += (r.end - r.start);
    size_t processed_bytes = 0;

    for (const auto& r : regions) {
        if (cancel_requested.load()) break;
        for (uintptr_t cur = r.start; cur < r.end; cur += CHUNK_SIZE) {
            if (cancel_requested.load()) break;
            const size_t to_read = std::min(CHUNK_SIZE, static_cast<size_t>(r.end - cur));
            bool ok = ReadMemory(cur, chunk.data(), to_read);
            if (!ok && to_read > 4096) {
                // Page-by-page fallback so a single guarded page doesn't drop 256 KB
                constexpr size_t PAGE_SIZE = 4096;
                bool any_page_read = false;
                for (size_t poff = 0; poff < to_read; poff += PAGE_SIZE) {
                    size_t psz = std::min(PAGE_SIZE, to_read - poff);
                    if (!ReadMemory(cur + poff, chunk.data() + poff, psz)) {
                        std::memset(chunk.data() + poff, 0, psz);
                    } else {
                        any_page_read = true;
                    }
                }
                ok = any_page_read;
            }
            if (ok) {
                for (size_t off = 0; off + type_size <= to_read; off += type_size) {
                    const double val = DecodeValue(type, chunk.data() + off);
                    if (MatchesCondition(comp, type, val, val, value)) {
                        const uint64_t raw = EncodeRaw(type, chunk.data() + off);
                        temp_matches.push_back({cur + off, raw, raw});
                        if (temp_matches.size() >= MAX_MATCHES) {
                            goto done;
                        }
                    }
                }
            }
            processed_bytes += to_read;
            if (total_bytes > 0) {
                progress.store(static_cast<float>(processed_bytes) / static_cast<float>(total_bytes));
            }
        }
    }

done:
    if (!cancel_requested.load()) {
        std::lock_guard<std::mutex> lock(mutex);
        current_type = type;
        matches = std::move(temp_matches);
    }
    scanning.store(false);
    progress.store(1.0f);
}

void MemoryScanner::NextScan(ScanComparison comp, double value) {
    if (scanning.load()) return;
    if (scan_thread.joinable()) {
        scan_thread.join();
    }

    cancel_requested.store(false);
    scanning.store(true);
    progress.store(0.0f);

    scan_thread = std::thread([this, comp, value]() {
        DoNextScan(comp, value);
    });
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
        const size_t plen = tokens.size();
        std::vector<uint8_t> pbuf(plen);
        std::vector<ScanMatch> next_matches;
        for (size_t i = 0; i < prev_list.size(); ++i) {
            if (cancel_requested.load()) break;
            auto& m = prev_list[i];
            if (ReadMemory(m.address, pbuf.data(), plen)) {
                if (PatternScanner::MatchesAt(pbuf.data(), plen, tokens)) {
                    m.prev_raw = m.current_raw;
                    uint64_t raw = 0;
                    std::memcpy(&raw, pbuf.data(), std::min(sizeof(raw), plen));
                    m.current_raw = raw;
                    next_matches.push_back(m);
                }
            }
            if ((i % 1000) == 0) {
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

    const size_t type_size = GetTypeSize(type);
    std::vector<ScanMatch> next_matches;
    next_matches.reserve(std::min(prev_list.size(), static_cast<size_t>(50000)));

    uint8_t buffer[8];
    for (size_t i = 0; i < prev_list.size(); ++i) {
        if (cancel_requested.load()) break;

        auto& m = prev_list[i];
        if (ReadMemory(m.address, buffer, type_size)) {
            const double current_val = DecodeValue(type, buffer);
            const double prev_val = DecodeValue(type, &m.current_raw);
            if (MatchesCondition(comp, type, current_val, prev_val, value)) {
                m.prev_raw = m.current_raw;
                m.current_raw = EncodeRaw(type, buffer);
                next_matches.push_back(m);
            }
        }
        if ((i % 1000) == 0) {
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

void MemoryScanner::Reset() {
    CancelScan();
    if (scan_thread.joinable()) {
        scan_thread.join();
    }
    std::lock_guard<std::mutex> lock(mutex);
    matches.clear();
    scanning.store(false);
    progress.store(0.0f);
}

size_t MemoryScanner::GetMatchCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return matches.size();
}

std::vector<ScanMatch> MemoryScanner::GetMatches(size_t offset, size_t limit) {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<ScanMatch> result;
    if (offset >= matches.size()) return result;
    const size_t count = std::min(limit, matches.size() - offset);
    result.insert(result.end(), matches.begin() + offset, matches.begin() + offset + count);
    return result;
}

} // namespace Debugger
