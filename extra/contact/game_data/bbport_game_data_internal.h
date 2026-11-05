// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the game-data files share -- the guest addresses, the session clock and the caches
// one file fills in and another reads (bbport_game_data*.cpp). Only this module needs it.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include "bbport_game_data.h"

namespace BbGameData::detail {

// Memory offsets relative to the guest image base (CUSA03173 1.09 eboot).
inline constexpr uintptr_t kGameDataMan = 0x0553B130;
inline constexpr uintptr_t kCurrentMapList = 0x0553B148;
inline constexpr uintptr_t kWorldChrMan = 0x0553E878;
inline constexpr uintptr_t kMatchingState = 0x05540290;
inline constexpr uintptr_t kGlobalState = 0x05556678;
inline constexpr uintptr_t kBossGauge = 0x05562878;

/// A guest pointer below this is a null or garbage value, never a mapped address.
inline constexpr uintptr_t kMinValidPointer = 0x10000;
/// The local player inside WorldChrMan: the character struct at +0x60.
inline constexpr std::size_t kLocalCharacter = 0x60;
/// Character struct: type at +0x78, HP modules at +0x3B0, control at +0x58, network lock at +0x3D8.
inline constexpr std::size_t kChrType = 0x78;
inline constexpr std::size_t kChrModules = 0x3B0;
inline constexpr std::size_t kChrControl = 0x58;
/// The module list's data block (+0x20), its HP at +0xF8; the death flag at control +0x114 bit 0.
inline constexpr std::size_t kModulesData = 0x20;
inline constexpr std::size_t kDataHp = 0xF8;
inline constexpr std::size_t kControlDeathFlag = 0x114;
/// The global state's "player is dead" byte, set while waiting to respawn.
inline constexpr std::size_t kGlobalDeadFlag = 0x1522;
/// Save slots 0..9 (userdata0000..userdata0009); userdata0010 holds the slot metadata.
inline constexpr int kSaveSlotCount = 10;
inline constexpr int kSlotMetadataIndex = 10;

// Session clock, set by Init() in bbport_game_data.cpp.
extern std::chrono::steady_clock::time_point session_start;
extern std::int64_t session_start_unix;

// Death counter: CheckPlayerDeath() adds to it, ResetDeathCounter() clears it.
extern std::atomic<uint32_t> session_deaths;

/// Reads the packed map id the game is on; false in menus and while loading
/// (bbport_game_data_world.cpp).
bool GetPackedMap(std::uint32_t& packed);

/// The playtime the game keeps in memory, in seconds (0 before a save is loaded).
std::uint32_t MemoryPlaytimeSeconds();

/// Counts a death when the player's alive -> dead edge is seen; `in_game` false forgets the state
/// (bbport_game_data_death.cpp).
void CheckPlayerDeath(std::uintptr_t img, bool in_game);

// The character of the open save slot (bbport_game_data_save.cpp).
extern std::atomic<int> active_save_slot;
extern std::chrono::steady_clock::time_point last_save_read;
extern std::string cached_char_name;
extern std::uint32_t cached_char_level;
extern std::uint32_t cached_save_seconds;

/// Re-reads the save metadata, at most once every kSaveRefreshInterval.
void RefreshSaveCharacter();

/// Records which slot the game has open and re-reads it (no-op for a slot outside 0..9).
void SetActiveSaveSlot(int slot);

/// The slot's playtime from the save file (0 until the metadata was read).
std::uint32_t SavePlaytimeSeconds();

// Name tables (bbport_game_data_names.cpp).
/// Fills the three localizations of `packed` (the area/block/sub bytes of the map id).
void ResolveMapNames(std::uint32_t packed, std::string& pt, std::string& en, std::string& ru);
/// The name shown when there is no map: the main menu, or a load in progress when `loading`.
const char* NoMapName(int lang, bool loading);
/// The boss the gauge currently shows, "" when no boss is active.
std::string CurrentBossName(int lang);

// Co-op status text (bbport_game_data_status.cpp).
/// "Host (+1 NPC)", "Guest (Co-op)", "Invaded (1)", ... for the HUD and the Discord card.
std::string FormatCoopStatusText(const PresenceSnapshot& snap, int lang);

// Co-op state (bbport_game_data_party.cpp).
struct PartyState {
    int role = 0; ///< 0 = solo, 1 = host, 2 = guest
    int hunters = 1;
    int max_hunters = 3;
    int npc_hunters = 0;
    int player_hunters = 0;
    int invaders = 0;
};

/// Reads the party from the world character list, logging when the population changes.
PartyState ReadPartyState();

} // namespace BbGameData::detail
