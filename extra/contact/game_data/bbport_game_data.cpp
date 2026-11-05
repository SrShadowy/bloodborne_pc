// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the game state the HUD and the Discord presence show -- session clock, playtime and the
// snapshot that ties it all together. The parts live next to this file:
// bbport_game_data_world.cpp (the map and the playtime in memory), bbport_game_data_death.cpp (the
// death counter), bbport_game_data_save.cpp (the open save slot), bbport_game_data_names.cpp
// (map/boss names) and bbport_game_data_party.cpp (who else is in the world).
#include "bbport_game_data.h"

#include <algorithm>
#include <chrono>

#include "bbgpu.h"
#include "bbport_game_data_internal.h"
#include "bbport_settings.h"

namespace BbGameData::detail {

std::chrono::steady_clock::time_point session_start{};
std::int64_t session_start_unix = 0;
std::atomic<std::uint32_t> session_deaths{0};

std::atomic<int> active_save_slot{-1};
std::chrono::steady_clock::time_point last_save_read{};
std::string cached_char_name;
std::uint32_t cached_char_level = 0;
std::uint32_t cached_save_seconds = 0;

} // namespace BbGameData::detail

namespace BbGameData {
namespace {

using namespace detail;

/// How often Tick() samples the world when the game itself does not call it.
constexpr auto kTickInterval = std::chrono::milliseconds(100);

} // namespace

void Init() {
    detail::session_start = std::chrono::steady_clock::now();
    detail::session_start_unix = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count();
}

void Tick() {
    static auto last_tick = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (last_tick.time_since_epoch().count() != 0 && (now - last_tick) < kTickInterval) {
        return;
    }
    last_tick = now;

    const uintptr_t img = bbgpu_get_guest_image_base();
    if (img == 0) {
        return;
    }
    std::uint32_t packed = 0;
    detail::CheckPlayerDeath(img, detail::GetPackedMap(packed));
}

void NotifySaveSlotOpened(int slot) {
    detail::SetActiveSaveSlot(slot);
}

void ResetDeathCounter(bool /*reset_total*/) {
    detail::session_deaths.store(0, std::memory_order_relaxed);
    std::printf("[BBPort] Death counter reset: session=0\n");
    std::fflush(stdout);
}

std::uint32_t GetInGamePlaytimeSeconds() {
    return std::max(detail::MemoryPlaytimeSeconds(), detail::SavePlaytimeSeconds());
}

std::string GetCurrentMapName(int lang) {
    std::uint32_t packed = 0;
    if (!detail::GetPackedMap(packed)) {
        const uintptr_t img = bbgpu_get_guest_image_base();
        if (img == 0) {
            return detail::NoMapName(lang, false);
        }
        return detail::NoMapName(lang, true);
    }
    std::string pt;
    std::string en;
    std::string ru;
    detail::ResolveMapNames(packed, pt, en, ru);
    if (lang == BbSettings::LangPortuguese) {
        return pt;
    }
    if (lang == BbSettings::LangRussian) {
        return ru;
    }
    return en;
}

std::string FormatCoopStatus(const PresenceSnapshot& snap, int lang) {
    return detail::FormatCoopStatusText(snap, lang);
}

std::string GetCoopStatus(int lang) {
    return FormatCoopStatus(GetSnapshot(), lang);
}

PresenceSnapshot GetSnapshot() {
    PresenceSnapshot data;
    data.session_start_unix_s = detail::session_start_unix;
    data.in_game_seconds = GetInGamePlaytimeSeconds();
    detail::RefreshSaveCharacter();
    data.hunter_name = detail::cached_char_name;
    data.level = detail::cached_char_level;

    // While a load happens the map list is empty for a moment; keep showing the last area.
    static std::uint32_t last_valid_packed = 0;
    std::uint32_t packed = 0;
    if (detail::GetPackedMap(packed)) {
        last_valid_packed = packed;
        data.in_game = true;
        detail::ResolveMapNames(packed, data.map_name_pt, data.map_name_en, data.map_name_ru);
    } else if (last_valid_packed != 0 && data.in_game_seconds > 0) {
        data.in_game = true;
        detail::ResolveMapNames(last_valid_packed, data.map_name_pt, data.map_name_en, data.map_name_ru);
    } else {
        data.in_game = false;
        data.map_name_pt = detail::NoMapName(BbSettings::LangPortuguese, false);
        data.map_name_en = detail::NoMapName(BbSettings::LangEnglish, false);
        data.map_name_ru = detail::NoMapName(BbSettings::LangRussian, false);
    }

    const int lang = BbSettings::Get().menu_language.load();
    data.boss_name = detail::CurrentBossName(lang);

    const detail::PartyState party = detail::ReadPartyState();
    data.coop_role = party.role;
    data.hunters_count = party.hunters;
    data.max_hunters = party.max_hunters;
    data.npc_hunters_count = party.npc_hunters;
    data.player_hunters_count = party.player_hunters;
    data.invaders_count = party.invaders;

    detail::CheckPlayerDeath(bbgpu_get_guest_image_base(), data.in_game);
    data.session_deaths = detail::session_deaths.load(std::memory_order_relaxed);
    data.total_deaths = data.session_deaths;
    return data;
}

} // namespace BbGameData

extern "C" void bbgpu_notify_save_slot(int slot) {
    BbGameData::NotifySaveSlotOpened(slot);
}
