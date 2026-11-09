// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the scanner's public API and state -- the singleton, the worker thread that runs a scan,
// the match list the results table reads and the progress it shows. The scans themselves are in
// mem_scanner_scan.cpp, and memory access in mem_scanner_io.cpp.
#include "mem_scanner.h"

#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>

#include "pattern_scanner.h"

namespace Debugger {

MemoryScanner& MemoryScanner::Get() {
    static MemoryScanner instance;
    return instance;
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
    if (scanning.load()) {
        return;
    }
    if (scan_thread.joinable()) {
        scan_thread.join();
    }

    cancel_requested.store(false);
    match_limit_hit.store(false);
    scanning.store(true);
    progress.store(0.0f);

    scan_thread =
        std::thread([this, pattern_str, scope]() { PatternScanner::ExecuteScan(this, pattern_str, scope); });
}

void MemoryScanner::StartFirstScan(DataType type, ScanComparison comp, double value,
                                   ScanScope scope) {
    if (scanning.load()) {
        return;
    }
    if (scan_thread.joinable()) {
        scan_thread.join();
    }

    cancel_requested.store(false);
    match_limit_hit.store(false);
    scanning.store(true);
    progress.store(0.0f);

    scan_thread = std::thread([this, type, comp, value, scope]() {
        DoFirstScan(type, comp, value, scope);
    });
}

void MemoryScanner::NextScan(ScanComparison comp, double value) {
    if (scanning.load()) {
        return;
    }
    if (scan_thread.joinable()) {
        scan_thread.join();
    }

    cancel_requested.store(false);
    scanning.store(true);
    progress.store(0.0f);

    scan_thread = std::thread([this, comp, value]() { DoNextScan(comp, value); });
}

void MemoryScanner::Reset() {
    CancelScan();
    if (scan_thread.joinable()) {
        scan_thread.join();
    }
    std::lock_guard<std::mutex> lock(mutex);
    matches.clear();
    match_limit_hit.store(false);
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
    if (offset >= matches.size()) {
        return result;
    }
    const size_t count = std::min(limit, matches.size() - offset);
    result.insert(result.end(), matches.begin() + offset, matches.begin() + offset + count);
    return result;
}

} // namespace Debugger
