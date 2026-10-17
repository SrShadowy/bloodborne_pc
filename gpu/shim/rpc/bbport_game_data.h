// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace BbGameData {

struct PresenceSnapshot {
    bool in_game = false;
    std::string hunter_name;
    uint32_t level = 0;
    uint32_t in_game_seconds = 0;
    int64_t session_start_unix_s = 0;
    std::string map_name_pt;
    std::string map_name_en;
    std::string map_name_ru;
    std::string boss_name;
    int coop_role = 0; // 0 = solo, 1 = host, 2 = guest
    int hunters_count = 1;
    int max_hunters = 3;
};

/// Initializes session start timer and caches.
void Init();

/// Updates the current active character save slot (0..9).
void NotifySaveSlotOpened(int slot);

/// Returns snapshot of current game state for Discord RPC and other systems.
PresenceSnapshot GetSnapshot();

/// Returns localized current map name.
std::string GetCurrentMapName(int lang);

/// Returns localized co-op status string.
std::string GetCoopStatus(int lang);

/// Returns current in-game playtime in seconds.
uint32_t GetInGamePlaytimeSeconds();

} // namespace BbGameData

#ifdef __cplusplus
extern "C" {
#endif

void bbgpu_notify_save_slot(int slot);

#ifdef __cplusplus
}
#endif
