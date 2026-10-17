// SPDX-License-Identifier: GPL-2.0-or-later
#include "symbol_manager.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <regex>

namespace Debugger {

namespace {
constexpr uintptr_t GuestImageBase = 0x800000000ULL;

uintptr_t NormalizeGuestAddr(uintptr_t addr) {
    if (addr >= GuestImageBase) return addr;
    if (addr >= 0x00400000ULL && addr < 0x10000000ULL) {
        return (addr - 0x00400000ULL) + GuestImageBase;
    }
    if (addr < 0x10000000ULL) {
        return addr + GuestImageBase;
    }
    return addr;
}
} // namespace

SymbolManager& SymbolManager::Get() {
    static SymbolManager instance;
    return instance;
}

SymbolManager::SymbolManager() {
    RegisterBuiltinSymbols();
    AutoLoad();
}

void SymbolManager::RegisterBuiltinSymbols() {
    // Retail Bloodborne 1.09 (CUSA03173) Core Engine & Hook Symbols
    auto Add = [this](uintptr_t offset, const char* name, uint32_t size = 0) {
        AddSymbol(GuestImageBase + offset, name, size);
    };

    // Global Managers & Singletons
    Add(0x0553B130, "GameDataMan");
    Add(0x0553B148, "CurrentMapList");
    Add(0x05540290, "MatchingState");
    Add(0x05556678, "GlobalState");
    Add(0x05562878, "BossGauge");
    Add(0x0553E878, "CandidateLocalState");
    Add(0x0553B120, "SummonManagerRoot");
    Add(0x0553D6D0, "SummonSessionRules");
    Add(0x0553D750, "RoleMetadataTable");
    Add(0x0556E560, "SummonBuildRoleTable");
    Add(0x056C7048, "OnlineState");
    Add(0x0553B0C8, "SprjLuaEventMan");
    Add(0x05540230, "MultiPlayMan");
    Add(0x0553B130, "PlayerSlotManager");

    // Engine Core, Memory & Task Scheduler
    Add(0x02199310, "TaskScheduler_Alloc");
    Add(0x0216D420, "ResourceLoader_Memcpy1");
    Add(0x02171D60, "ResourceLoader_Memcpy2");
    Add(0x00BB5FB0, "ReleaseCheck");
    Add(0x019419D6, "PlayTimeAdd");

    // Camera, Controls & Graphics Patches
    Add(0x01338660, "CamLook");
    Add(0x01339120, "CamFov");
    Add(0x018F9720, "UseItemNativeApply");
    Add(0x013CDE30, "StageTransition");
    Add(0x01336B90, "SummonedMapReload");
    Add(0x01332BC0, "SelectSummonedPlacement");
    Add(0x015BED10, "WorldChrMan_RemoteCleanup");
    Add(0x018BBA70, "CharacterWarp_Floor");
    Add(0x01E52560, "MultiPlayerIns_SetState");
    Add(0x00C98070, "MatchingStopTaskEnqueue");
    Add(0x00CA1C70, "MemberIdEquals");
    Add(0x01308DC0, "BbscGameTick");

    // Multiplayer, Matching & SOS
    Add(0x0157F8C8, "BeckoningAreaComparison");
    Add(0x0157F960, "BeckoningAreaFlagResult");
    Add(0x0157F6D1, "ResponderBellAreaResult");
    Add(0x015068BB, "ActiveBellAreaComparison");
    Add(0x0191A8C3, "ResponderSearchAreaRange");
    Add(0x018700D3, "SosStatusAreaRestriction");
    Add(0x014B714A, "SummonCandidateAreaRestriction");
    Add(0x01874B79, "SummonBuildAreaRestriction");
    Add(0x018749E8, "SummonBuildWorldStateRestriction");
    Add(0x01874710, "SummonBuildEntry");
    Add(0x01872360, "SosStatusUpdate");
    Add(0x01944F23, "StageWarpDescriptorFinalize");
    Add(0x01ED07A0, "MultiPlayStopRequest");
    Add(0x01E4FF3A, "CrossMapGuestHandoff");
    Add(0x012F836E, "HealingFountainAvailability");
    Add(0x0156CF10, "SetForcedSummonMap");
    Add(0x0156CF20, "SetForcedSummonPosition");
    Add(0x0156CF40, "SetForcedSummonOrientation");
    Add(0x0156CF60, "SetForcedSummonWarp");
    Add(0x0178D9A0, "SetSummonReloadState");
}

void SymbolManager::AddSymbol(uintptr_t addr, const std::string& name, uint32_t size) {
    uintptr_t norm = NormalizeGuestAddr(addr);
    m_symbols[norm] = SymbolEntry{norm, name, size};
}

bool SymbolManager::HasExactSymbol(uintptr_t addr) const {
    uintptr_t norm = NormalizeGuestAddr(addr);
    return m_symbols.find(norm) != m_symbols.end();
}

std::string SymbolManager::GetExactName(uintptr_t addr) const {
    uintptr_t norm = NormalizeGuestAddr(addr);
    auto it = m_symbols.find(norm);
    if (it != m_symbols.end()) return it->second.name;
    return "";
}

std::string SymbolManager::Resolve(uintptr_t addr, bool allow_offset, uintptr_t max_offset) const {
    if (m_symbols.empty()) return "";
    uintptr_t norm = NormalizeGuestAddr(addr);

    // Exact match
    auto it = m_symbols.find(norm);
    if (it != m_symbols.end()) {
        return it->second.name;
    }

    if (!allow_offset) return "";

    // Find previous symbol
    auto upper = m_symbols.upper_bound(norm);
    if (upper != m_symbols.begin()) {
        auto prev = std::prev(upper);
        uintptr_t diff = norm - prev->first;
        if (diff <= max_offset) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s + 0x%llX", prev->second.name.c_str(), static_cast<unsigned long long>(diff));
            return buf;
        }
    }

    return "";
}

std::string SymbolManager::AnnotateInstruction(const std::string& inst_text, uintptr_t cur_pc) const {
    (void)cur_pc;
    if (m_symbols.empty()) return inst_text;

    // Detect hex addresses in format 0x[0-9a-fA-F]+
    static const std::regex hex_regex(R"(0x([0-9a-fA-F]{6,16}))");
    std::string result = inst_text;
    std::smatch match;

    std::string::const_iterator search_start(inst_text.cbegin());
    while (std::regex_search(search_start, inst_text.cend(), match, hex_regex)) {
        try {
            uintptr_t addr = std::strtoull(match[0].str().c_str(), nullptr, 16);
            std::string sym = Resolve(addr, true, 0x200);
            if (!sym.empty()) {
                // If not already annotated
                if (result.find("<" + sym + ">") == std::string::npos) {
                    result += " <" + sym + ">";
                }
            }
        } catch (...) {}
        search_start = match.suffix().first;
    }

    return result;
}

bool SymbolManager::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    size_t loaded = 0;
    while (std::getline(file, line)) {
        // Strip comments and whitespace
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) line = line.substr(0, comment_pos);
        comment_pos = line.find("//");
        if (comment_pos != std::string::npos) line = line.substr(0, comment_pos);

        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
        if (line.empty()) continue;

        uintptr_t addr = 0;
        std::string name;

        // Try CSV: 0x80553b130,GameDataMan
        size_t comma = line.find(',');
        if (comma != std::string::npos) {
            std::string addr_part = line.substr(0, comma);
            name = line.substr(comma + 1);
            addr = std::strtoull(addr_part.c_str(), nullptr, 16);
        } else {
            // Try whitespace: 0x80553b130 GameDataMan or 00000000:080553b130 GameDataMan
            std::istringstream ss(line);
            std::string first_token;
            ss >> first_token >> name;
            if (!first_token.empty() && !name.empty()) {
                size_t colon = first_token.find(':');
                if (colon != std::string::npos) first_token = first_token.substr(colon + 1);
                addr = std::strtoull(first_token.c_str(), nullptr, 16);
            }
        }

        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front()))) name.erase(name.begin());
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();

        if (addr != 0 && !name.empty()) {
            AddSymbol(addr, name);
            loaded++;
        }
    }

    return loaded > 0;
}

void SymbolManager::AutoLoad() {
    const std::vector<std::string> candidates = {
        "symbols.csv", "symbols.map", "symbols.txt", "bloodborne.sym",
        "data/symbols.csv", "../symbols.csv", "game/symbols.csv"
    };
    for (const auto& path : candidates) {
        if (LoadFromFile(path)) {
            break;
        }
    }
}

size_t SymbolManager::GetCount() const {
    return m_symbols.size();
}

} // namespace Debugger
