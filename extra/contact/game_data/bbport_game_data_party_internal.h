// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the party reader shares with ReadPartyState(). bbport_game_data_party.cpp decides
// what the party is from these counts, bbport_game_data_party_read.cpp reads them out of the
// world's character lists. Only this module needs this header.
#pragma once

#include <cstdint>
#include <vector>

namespace BbGameData::detail {

/// Character types at chr + 0x78, as the game stores them.
enum CharacterType : int {
    ChrHost = 0, ///< the world's own entity (or the remote host's)
    ChrCoop = 1, ///< a summoned co-op phantom
    ChrInvader = 2, ///< an invader
    ChrInvaderAlt = 12,
};

/// Session states at matching + 0x124.
enum SessionState : int {
    SessionOnline = 3, ///< online, waiting for or holding a party
    SessionGuest = 6,  ///< joined someone else's world
};

/// The counts one pass over the character lists produces.
struct Population {
    int npc = 0;
    int players = 0;
    int invaders = 0;
};

/// Session state from the matching struct; 0 when not online.
int ReadSessionState(std::uintptr_t img);

/// Counts the alive characters of `world_chr`, skipping `local` (it is counted separately). When
/// no network phantom has spawned yet but the population record already counts them, that record
/// fills `population.players`.
void CollectPopulation(std::uintptr_t img, std::uintptr_t world_chr,
                       const std::vector<std::uintptr_t>& already_seen, int session_state,
                       Population& population);

} // namespace BbGameData::detail
