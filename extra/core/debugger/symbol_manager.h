// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <optional>

namespace Debugger {

struct SymbolEntry {
    uintptr_t address{0};
    std::string name;
    uint32_t size{0};
};

class SymbolManager {
public:
    static SymbolManager& Get();

    // Resolves an address to a symbol name. If exact match exists, returns name.
    // If inside a function within max_offset, returns "SymbolName + 0xOffset".
    // If not found, returns an empty string.
    std::string Resolve(uintptr_t addr, bool allow_offset = true, uintptr_t max_offset = 0x1000) const;

    // Checks if an address has an exact symbol
    bool HasExactSymbol(uintptr_t addr) const;

    // Returns exact symbol name if exists, else empty
    std::string GetExactName(uintptr_t addr) const;

    // Annotates disassembly text with symbol names (e.g. "lea rax, [0x80553b130]" -> "lea rax, [0x80553b130] <GameDataMan>")
    std::string AnnotateInstruction(const std::string& inst_text, uintptr_t cur_pc) const;

    // Adds or replaces a symbol
    void AddSymbol(uintptr_t addr, const std::string& name, uint32_t size = 0);

    // Loads symbols from an external file (.csv, .map, .sym)
    // Format supported:
    // CSV: "0x80553b130,GameDataMan" or "80553b130,GameDataMan"
    // MAP/SYM: "0x80553b130 GameDataMan" or "00000000:080553b130 GameDataMan"
    bool LoadFromFile(const std::string& path);

    // Auto-detects and loads symbols file from known locations
    void AutoLoad();

    // Returns all loaded symbols count
    size_t GetCount() const;

private:
    SymbolManager();
    void RegisterBuiltinSymbols();

    // Stored with 64-bit runtime addresses (0x800000000 base)
    std::map<uintptr_t, SymbolEntry> m_symbols;
};

} // namespace Debugger
