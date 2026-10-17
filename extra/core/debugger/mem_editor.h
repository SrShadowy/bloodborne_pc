// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "mem_scanner.h"

#include <vector>
#include <string>
#include <mutex>

namespace Debugger {

struct WatchEntry {
    std::string label;
    uintptr_t address{0}; // Endereço fixo OU Endereço Base do ponteiro
    DataType type{DataType::U32};
    bool frozen{false};
    double freeze_value{0.0};
    bool is_pointer{false};
    std::vector<int64_t> offsets; // ex: { 0x10, 0x20 }
};

class MemoryEditor {
public:
    static MemoryEditor& Get();

    void AddWatch(const std::string& label, uintptr_t address, DataType type);
    void AddPointerWatch(const std::string& label, uintptr_t base_address, const std::vector<int64_t>& offsets, DataType type);
    void RemoveWatch(size_t index);
    void Clear();

    size_t GetCount() const;
    std::vector<WatchEntry> GetEntries() const;

    void SetFrozen(size_t index, bool frozen);
    void SetFreezeValue(size_t index, double value);
    void SetLabel(size_t index, const std::string& label);
    void SetType(size_t index, DataType type);
    void SetOffsets(size_t index, const std::vector<int64_t>& offsets);
    bool GetEntry(size_t index, WatchEntry& out) const;
    void UpdateEntry(size_t index, const WatchEntry& entry);

    /// Safely resolves pointer chain (returns 0 if null or unmapped)
    uintptr_t ResolveAddress(const WatchEntry& entry) const;
    uintptr_t GetResolvedAddress(size_t index) const;

    bool WriteValue(size_t index, double value);

    /// Ticks every frame to enforce frozen values
    void TickFreeze();

    /// Table persistence (.bbt / Bloodborne Table format)
    bool SaveTableBBT(const std::string& filepath = "") const;
    bool LoadTableBBT(const std::string& filepath = "");
    static std::string GetDefaultTablePath();

private:
    MemoryEditor();

    mutable std::mutex mutex;
    std::vector<WatchEntry> entries;
};

} // namespace Debugger
