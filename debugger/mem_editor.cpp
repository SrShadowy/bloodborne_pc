// SPDX-License-Identifier: GPL-2.0-or-later
#include "mem_editor.h"

#include <cstdlib>
#include <cstring>

namespace Debugger {

MemoryEditor::MemoryEditor() {
    LoadTableBBT();
}

MemoryEditor& MemoryEditor::Get() {
    static MemoryEditor instance;
    return instance;
}

std::string MemoryEditor::GetDefaultTablePath() {
    const char* data_dir = std::getenv("BB_DATA_DIR");
    if (data_dir && data_dir[0]) {
        return std::string(data_dir) + "/cheats/default.bbt";
    }
    return "user/cheats/default.bbt";
}

uintptr_t MemoryEditor::ResolveAddress(const WatchEntry& entry) const {
    if (!entry.is_pointer) return entry.address;
    if (entry.address == 0) return 0;

    uintptr_t curr = entry.address;
    for (size_t i = 0; i < entry.offsets.size(); ++i) {
        uintptr_t next_ptr = 0;
        if (!MemoryScanner::ReadMemory(curr, &next_ptr, sizeof(next_ptr)) || next_ptr == 0) {
            return 0; // Null or unmapped pointer
        }
        curr = static_cast<uintptr_t>(static_cast<int64_t>(next_ptr) + entry.offsets[i]);
    }
    return curr;
}

uintptr_t MemoryEditor::GetResolvedAddress(size_t index) const {
    std::lock_guard<std::mutex> lock(mutex);
    if (index >= entries.size()) return 0;
    return ResolveAddress(entries[index]);
}

void MemoryEditor::AddWatch(const std::string& label, uintptr_t address, DataType type) {
    std::lock_guard<std::mutex> lock(mutex);
    double cur_val = 0;
    std::string str;
    MemoryScanner::ReadFormatted(address, type, cur_val, str);
    entries.push_back({label, address, type, false, cur_val, false, {}});
}

void MemoryEditor::AddPointerWatch(const std::string& label, uintptr_t base_address, const std::vector<int64_t>& offsets, DataType type) {
    std::lock_guard<std::mutex> lock(mutex);
    WatchEntry entry;
    entry.label = label;
    entry.address = base_address;
    entry.type = type;
    entry.frozen = false;
    entry.is_pointer = true;
    entry.offsets = offsets;

    double cur_val = 0;
    std::string str;
    const uintptr_t resolved = ResolveAddress(entry);
    if (resolved != 0) {
        MemoryScanner::ReadFormatted(resolved, type, cur_val, str);
    }
    entry.freeze_value = cur_val;
    entries.push_back(entry);
}

void MemoryEditor::RemoveWatch(size_t index) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries.erase(entries.begin() + index);
    }
}

void MemoryEditor::Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    entries.clear();
}

size_t MemoryEditor::GetCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return entries.size();
}

std::vector<WatchEntry> MemoryEditor::GetEntries() const {
    std::lock_guard<std::mutex> lock(mutex);
    return entries;
}

bool MemoryEditor::GetEntry(size_t index, WatchEntry& out) const {
    std::lock_guard<std::mutex> lock(mutex);
    if (index >= entries.size()) return false;
    out = entries[index];
    return true;
}

void MemoryEditor::UpdateEntry(size_t index, const WatchEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries[index].label = entry.label;
        entries[index].address = entry.address;
        entries[index].type = entry.type;
        entries[index].is_pointer = entry.is_pointer;
        entries[index].offsets = entry.offsets;
    }
}

void MemoryEditor::SetFrozen(size_t index, bool frozen) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries[index].frozen = frozen;
    }
}

void MemoryEditor::SetFreezeValue(size_t index, double value) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries[index].freeze_value = value;
    }
}

void MemoryEditor::SetLabel(size_t index, const std::string& label) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries[index].label = label;
    }
}

void MemoryEditor::SetType(size_t index, DataType type) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries[index].type = type;
    }
}

void MemoryEditor::SetOffsets(size_t index, const std::vector<int64_t>& offsets) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < entries.size()) {
        entries[index].offsets = offsets;
    }
}

bool MemoryEditor::WriteValue(size_t index, double value) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index >= entries.size()) return false;
    entries[index].freeze_value = value;
    const uintptr_t target = ResolveAddress(entries[index]);
    if (target == 0) return false;
    return MemoryScanner::WriteFormatted(target, entries[index].type, value);
}

void MemoryEditor::TickFreeze() {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& entry : entries) {
        if (entry.frozen) {
            const uintptr_t target = ResolveAddress(entry);
            if (target != 0) {
                MemoryScanner::WriteFormatted(target, entry.type, entry.freeze_value);
            }
        }
    }
}

} // namespace Debugger
