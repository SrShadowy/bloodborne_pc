// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: reading the party out of the world -- the character lists (and the block array that also
// holds summoned NPC hunters), whether each one is alive, and whether a co-op phantom is a real
// player or an NPC. bbport_game_data_party.cpp turns the counts into the party state.
#include "bbport_game_data_party_internal.h"

#include <algorithm>
#include <cstddef>

#include "bbport_game_data_internal.h"
#include "bbport_scaleform_hit.h"

namespace BbGameData::detail {
namespace {

/// The world character list at world_chr + 0x30/0x38, and the block array at world_chr + 0x850.
constexpr std::size_t kWorldChrCount = 0x30;
constexpr std::size_t kWorldChrEntries = 0x38;
constexpr std::size_t kWorldChrStride = 0x38;
constexpr std::size_t kChrBlockArray = 0x850;
constexpr std::size_t kChrBlockStride = 8;
/// One block: character count at +0, entries at +8.
constexpr std::size_t kBlockCount = 0;
constexpr std::size_t kBlockEntries = 8;
/// The block array's header: count at +0x18; the game keeps a few spare blocks past it.
constexpr std::size_t kBlockArrayCount = 0x18;
constexpr int kMaxBlocks = 64;
constexpr int kSpareBlocks = 4;
constexpr int kMaxBlockCharacters = 0x3FFF;
constexpr int kMaxWorldCharacters = 64;

/// The co-op phantom's network object at chr + 0x3D8; it has a vtable while the session is online.
constexpr std::size_t kChrNetworkLock = 0x3D8;
constexpr std::size_t kNetworkLockVtableSlot = 0x30;

/// The online population record at global + 0x16F8, its co-op count at +0x08.
constexpr std::size_t kGlobalPopulation = 0x16F8;
constexpr std::size_t kPopulationCoop = 0x08;
constexpr int kMaxPopulationCoop = 4;
/// matching + 0x124.
constexpr std::size_t kMatchingSessionState = 0x124;

/// Reads a guest pointer and rejects null/garbage.
bool ReadPointer(uintptr_t at, uintptr_t& out) {
    return BbMenuPointer::SafeRead(at, &out, sizeof(out)) && out >= kMinValidPointer;
}

/// The HP of a character, through its module list; 0 when it cannot be read.
std::int32_t CharacterHp(uintptr_t chr) {
    uintptr_t modules = 0;
    if (!ReadPointer(chr + kChrModules, modules)) {
        return 0;
    }
    uintptr_t data = 0;
    if (!ReadPointer(modules + kModulesData, data)) {
        return 0;
    }
    std::int32_t hp = 0;
    if (!BbMenuPointer::SafeRead(data + kDataHp, &hp, sizeof(hp))) {
        return 0;
    }
    return hp;
}

/// The death flag at [[chr+0x58]+0x114] bit 0.
bool CharacterIsDead(uintptr_t chr) {
    uintptr_t control = 0;
    if (!ReadPointer(chr + kChrControl, control)) {
        return false;
    }
    std::uint16_t dead_flag = 0;
    return BbMenuPointer::SafeRead(control + kControlDeathFlag, &dead_flag, sizeof(dead_flag)) &&
           (dead_flag & 1) != 0;
}

/// Whether a co-op phantom is a network player (vs. a summoned NPC): its network object has a
/// vtable while online.
bool IsNetworkPhantom(uintptr_t chr, int session_state) {
    if (session_state != SessionOnline && session_state != SessionGuest) {
        return false;
    }
    uintptr_t lock = 0;
    if (!ReadPointer(chr + kChrNetworkLock, lock)) {
        return false;
    }
    uintptr_t vtable = 0;
    if (!ReadPointer(lock, vtable)) {
        return false;
    }
    return ReadPointer(vtable + kNetworkLockVtableSlot, vtable);
}

/// Visits every character once (the list and the block array overlap), with the checks every
/// caller wants: a valid, alive entity.
template <typename Visitor>
void ForEachCharacter(uintptr_t world_chr, const std::vector<uintptr_t>& already_seen,
                      Visitor visit) {
    std::vector<uintptr_t> seen = already_seen;

    auto process = [&](uintptr_t chr) {
        if (chr < kMinValidPointer || std::find(seen.begin(), seen.end(), chr) != seen.end()) {
            return;
        }
        seen.push_back(chr);
        if (CharacterHp(chr) <= 0 || CharacterIsDead(chr)) {
            return;
        }
        visit(chr);
    };

    std::int32_t count = 0;
    uintptr_t entries = 0;
    BbMenuPointer::SafeRead(world_chr + kWorldChrCount, &count, sizeof(count));
    BbMenuPointer::SafeRead(world_chr + kWorldChrEntries, &entries, sizeof(entries));
    if (count > 0 && count <= kMaxWorldCharacters && entries >= kMinValidPointer) {
        for (std::int32_t i = 0; i < count; ++i) {
            uintptr_t chr = 0;
            if (BbMenuPointer::SafeRead(entries + static_cast<std::size_t>(i) * kWorldChrStride, &chr,
                                        sizeof(chr))) {
                process(chr);
            }
        }
    }

    // The block array also holds summoned NPC hunters, which the list above may not have yet.
    int block_count = kMaxBlocks;
    uintptr_t blocks_info = 0;
    if (ReadPointer(world_chr + 0x08, blocks_info)) {
        std::int32_t stored = 0;
        if (BbMenuPointer::SafeRead(blocks_info + kBlockArrayCount, &stored, sizeof(stored)) &&
            stored >= 0 && stored < kMaxBlocks) {
            block_count = std::min(kMaxBlocks, stored + kSpareBlocks);
        }
    }
    for (int b = 0; b < block_count; ++b) {
        uintptr_t block = 0;
        if (!ReadPointer(world_chr + kChrBlockArray + static_cast<std::size_t>(b) * kChrBlockStride,
                         block)) {
            continue;
        }
        std::int32_t block_chars = 0;
        uintptr_t block_entries = 0;
        if (!BbMenuPointer::SafeRead(block + kBlockCount, &block_chars, sizeof(block_chars)) ||
            block_chars <= 0 || block_chars > kMaxBlockCharacters ||
            !ReadPointer(block + kBlockEntries, block_entries)) {
            continue;
        }
        for (std::int32_t i = 0; i < block_chars; ++i) {
            uintptr_t chr = 0;
            if (BbMenuPointer::SafeRead(block_entries + static_cast<std::size_t>(i) * kWorldChrStride,
                                        &chr, sizeof(chr))) {
                process(chr);
            }
        }
    }
}

/// When no network phantom has spawned yet but the population record already counts them, trust
/// the record (the game spawns the models a moment later).
int PopulationCoopPlayers(uintptr_t img, int session_state) {
    if (session_state != SessionOnline) {
        return 0;
    }
    uintptr_t global = 0;
    if (!ReadPointer(img + kGlobalState, global)) {
        return 0;
    }
    uintptr_t population = 0;
    if (!ReadPointer(global + kGlobalPopulation, population)) {
        return 0;
    }
    std::int32_t players = 0;
    if (!BbMenuPointer::SafeRead(population + kPopulationCoop, &players, sizeof(players)) ||
        players <= 0 || players > kMaxPopulationCoop) {
        return 0;
    }
    return players;
}

} // namespace

int ReadSessionState(std::uintptr_t img) {
    uintptr_t matching = 0;
    if (!ReadPointer(img + kMatchingState, matching)) {
        return 0;
    }
    std::int32_t state = 0;
    BbMenuPointer::SafeRead(matching + kMatchingSessionState, &state, sizeof(state));
    return state;
}

void CollectPopulation(std::uintptr_t img, std::uintptr_t world_chr,
                       const std::vector<std::uintptr_t>& already_seen, int session_state,
                       Population& population) {
    ForEachCharacter(world_chr, already_seen, [&](uintptr_t chr) {
        std::int32_t char_type = -1;
        BbMenuPointer::SafeRead(chr + kChrType, &char_type, sizeof(char_type));
        if (char_type == ChrCoop) {
            if (IsNetworkPhantom(chr, session_state)) {
                ++population.players;
            } else {
                ++population.npc;
            }
        } else if (char_type == ChrInvader || char_type == ChrInvaderAlt) {
            ++population.invaders;
        }
    });

    if (population.players == 0) {
        population.players = PopulationCoopPlayers(img, session_state);
    }
}

} // namespace BbGameData::detail
