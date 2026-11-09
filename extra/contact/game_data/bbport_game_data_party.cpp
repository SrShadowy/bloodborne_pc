// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the party -- who else is in the world (summoned NPC hunters, co-op players, invaders) and
// what that makes the local player (host or guest), so the HUD and the Discord card can show it.
// The character lists themselves are read in bbport_game_data_party_read.cpp.
#include "bbport_game_data_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "bbgpu.h"
#include "bbport_game_data_party_internal.h"
#include "bbport_scaleform_hit.h"

namespace BbGameData::detail {
namespace {

/// SHADPS4_BBSC_FIVE_HUNTERS=1 raises the party cap from 3 to 5 (the private-server patch).
constexpr const char* kFiveHuntersEnv = "SHADPS4_BBSC_FIVE_HUNTERS";
constexpr int kDefaultMaxHunters = 3;
constexpr int kFiveMaxHunters = 5;

/// Reads a guest pointer and rejects null/garbage.
bool ReadPointer(uintptr_t at, uintptr_t& out) {
    return BbMenuPointer::SafeRead(at, &out, sizeof(out)) && out >= kMinValidPointer;
}

/// The party size cap: 3 normally, 5 with the private-server five-hunter patch.
int MaxHunters() {
    const char* five = std::getenv(kFiveHuntersEnv);
    const bool enabled =
        five != nullptr && (std::strcmp(five, "1") == 0 || std::strcmp(five, "true") == 0);
    return enabled ? kFiveMaxHunters : kDefaultMaxHunters;
}

} // namespace

PartyState ReadPartyState() {
    PartyState party;
    party.max_hunters = MaxHunters();

    const uintptr_t img = bbgpu_get_guest_image_base();
    if (img == 0) {
        return party;
    }

    const int session_state = ReadSessionState(img);

    uintptr_t world_chr = 0;
    std::int32_t local_type = ChrHost;
    uintptr_t local = 0;
    if (ReadPointer(img + kWorldChrMan, world_chr)) {
        BbMenuPointer::SafeRead(world_chr + kLocalCharacter, &local, sizeof(local));
        if (local >= kMinValidPointer) {
            BbMenuPointer::SafeRead(local + kChrType, &local_type, sizeof(local_type));
        }
    }

    Population population;
    if (world_chr != 0) {
        const std::vector<uintptr_t> local_seen =
            local >= kMinValidPointer ? std::vector<uintptr_t>{local} : std::vector<uintptr_t>{};
        CollectPopulation(img, world_chr, local_seen, session_state, population);
    }

    if (local_type == ChrCoop || session_state == SessionGuest) {
        party.role = 2;
        party.hunters = std::max(2, 2 + population.npc + population.players);
    } else if (population.npc + population.players > 0) {
        party.role = 1;
        party.hunters = 1 + population.npc + population.players;
    } else if (session_state == SessionOnline) {
        party.role = 1;
    }
    party.npc_hunters = population.npc;
    party.player_hunters = population.players;
    party.invaders = population.invaders;

    // Log a change of population once, so a report can be matched to what was on screen.
    static int last_npc = -1;
    static int last_players = -1;
    static int last_invaders = -1;
    if (population.npc != last_npc || population.players != last_players ||
        population.invaders != last_invaders) {
        last_npc = population.npc;
        last_players = population.players;
        last_invaders = population.invaders;
        std::printf("[BBPort] Co-op state: %d NPC(s), %d player(s), %d invader(s), role=%d\n",
                    population.npc, population.players, population.invaders, party.role);
        std::fflush(stdout);
    }
    return party;
}

} // namespace BbGameData::detail
