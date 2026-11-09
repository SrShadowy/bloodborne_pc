// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the activity card's text -- the two lines Discord shows (hunter/level/deaths and
// boss/co-op), the playtime counter, the map artwork and the party size. The text is built here and
// also fed to the settings window's live preview, so both always agree. The JSON that carries it to
// Discord is in bbport_discord_rpc_json.cpp.
#include "bbport_discord_rpc_internal.h"

#include <chrono>
#include <cstdio>
#include <string>

#include "bbport_game_data.h"
#include "bbport_settings.h"

namespace BbDiscordRpc::detail {
namespace {

/// Discord truncates the two activity lines past 128 characters.
constexpr std::size_t kMaxLineLength = 127;

constexpr int kSecondsPerHour = 3600;
constexpr int kSecondsPerMinute = 60;

/// The text for `lang`, the way the presence has always been localized.
const char* Tr(int lang, const char* en, const char* pt, const char* ru) {
    if (lang == BbSettings::LangPortuguese) {
        return pt;
    }
    if (lang == BbSettings::LangRussian) {
        return ru;
    }
    return en;
}

/// The area name in the requested language.
const std::string& AreaName(const BbGameData::PresenceSnapshot& presence, int lang) {
    if (lang == BbSettings::LangPortuguese) {
        return presence.map_name_pt;
    }
    if (lang == BbSettings::LangRussian) {
        return presence.map_name_ru;
    }
    return presence.map_name_en;
}

/// "01:23:45 elapsed" for a session started at `start_unix_s`.
std::string ElapsedText(int lang, std::int64_t start_unix_s) {
    const auto now_s = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const std::int64_t elapsed = (now_s > start_unix_s) ? (now_s - start_unix_s) : 0;
    const int hours = static_cast<int>(elapsed / kSecondsPerHour);
    const int minutes = static_cast<int>((elapsed % kSecondsPerHour) / kSecondsPerMinute);
    const int seconds = static_cast<int>(elapsed % kSecondsPerMinute);

    char clock[16];
    if (hours > 0) {
        std::snprintf(clock, sizeof(clock), "%02d:%02d:%02d", hours, minutes, seconds);
    } else {
        std::snprintf(clock, sizeof(clock), "%02d:%02d", minutes, seconds);
    }
    return std::string(clock) + Tr(lang, " elapsed", " decorrido", " прошло");
}

/// "Nv. 120" / "Lv. 120".
std::string LevelText(int lang, std::uint32_t level) {
    return std::string(Tr(lang, "Lv. ", "Nv. ", "Ур. ")) + std::to_string(level);
}

/// "Mortes: 3" / "Deaths: 3".
std::string DeathsText(int lang, std::uint32_t deaths) {
    return std::string(Tr(lang, "Deaths: ", "Mortes: ", "Смерти: ")) + std::to_string(deaths);
}

/// The first line: hunter (and level), then the death counter, falling back to the area and then
/// to a plain "Hunter".
std::string DetailsLine(const BbGameData::PresenceSnapshot& presence, const BbSettings::Values& s,
                        int lang) {
    const bool show_name = s.discord_rpc_show_name.load(std::memory_order_relaxed);
    const bool show_level = s.discord_rpc_show_level.load(std::memory_order_relaxed);
    const bool show_deaths = s.discord_rpc_show_deaths.load(std::memory_order_relaxed) &&
                             (s.discord_rpc_deaths_mode.load(std::memory_order_relaxed) != 0);

    std::string hunter;
    if (show_name && !presence.hunter_name.empty()) {
        hunter = presence.hunter_name;
        if (show_level && presence.level > 0) {
            // The parenthesised level always reads "Nv.", as the presence always has.
            hunter += " (Nv. " + std::to_string(presence.level) + ")";
        }
    } else if (show_level && presence.level > 0) {
        hunter = LevelText(lang, presence.level);
    }

    const std::string deaths =
        show_deaths ? DeathsText(lang, presence.session_deaths) : std::string();

    if (!hunter.empty() && !deaths.empty()) {
        return hunter + " • " + deaths;
    }
    if (!hunter.empty()) {
        return hunter;
    }
    if (!deaths.empty()) {
        return deaths;
    }
    if (s.discord_rpc_show_area.load(std::memory_order_relaxed)) {
        return AreaName(presence, lang);
    }
    return Tr(lang, "Hunter", "Caçador", "Охотник");
}

/// "Co-op: Host (+1 NPC)" and the other party wordings.
std::string HostText(const BbGameData::PresenceSnapshot& presence, int lang) {
    char buffer[64];
    const int npc = presence.npc_hunters_count;
    const int players = presence.player_hunters_count;

    if (npc > 0 && players == 0) {
        const char* plural = npc > 1 ? "s" : "";
        std::snprintf(buffer, sizeof(buffer),
                      Tr(lang, "Co-op: Host (+%d NPC%s)", "Co-op: Anfitrião (+%d NPC%s)",
                         "Кооп: Хост (+%d NPC)"),
                      npc, plural);
        return buffer;
    }
    if (players > 0 && npc > 0) {
        std::snprintf(buffer, sizeof(buffer),
                      Tr(lang, "Co-op (+%d Pl, +%d NPC)", "Co-op (+%d Jog, +%d NPC)",
                         "Кооп (+%d игр, +%d NPC)"),
                      players, npc);
        return buffer;
    }
    if (presence.hunters_count > 1) {
        std::snprintf(buffer, sizeof(buffer),
                      Tr(lang, "Co-op: Host (+%d)", "Co-op: Anfitrião (+%d)", "Кооп: Хост (+%d)"),
                      presence.hunters_count - 1);
        return buffer;
    }
    return Tr(lang, "Co-op: Host", "Co-op: Anfitrião", "Кооп: Хост");
}

/// The second line: the boss being fought, else the party, else solo.
std::string StateLine(const BbGameData::PresenceSnapshot& presence, const BbSettings::Values& s,
                      int lang) {
    const bool show_boss = s.discord_rpc_show_boss.load(std::memory_order_relaxed);
    const bool show_party = s.discord_rpc_show_party.load(std::memory_order_relaxed);

    if (show_boss && !presence.boss_name.empty()) {
        return std::string(Tr(lang, "Fighting: ", "Lutando: ", "Бой: ")) + presence.boss_name;
    }
    // The party wording needs the party setting on; the solo line is the fallback either way.
    if (show_party && presence.coop_role == 1) {
        return HostText(presence, lang);
    }
    if (show_party && presence.coop_role == 2) {
        return Tr(lang, "Co-op: Guest", "Co-op: Convidado", "Кооп: Гость");
    }
    if (show_party && presence.invaders_count > 0) {
        return Tr(lang, "Invaded!", "Invadido!", "Вторжение!");
    }
    return Tr(lang, "Playing Solo", "Jogando Solo", "Одиночная игра");
}

} // namespace

ActivityDisplay FormatActivity(const BbGameData::PresenceSnapshot& presence, int lang) {
    const BbSettings::Values& s = BbSettings::Get();

    ActivityDisplay display;
    if (!presence.in_game) {
        display.details = Tr(lang, "Main Menu", "Menu Principal", "Главное меню");
        display.state = Tr(lang, "In Main Menu", "No Menu Principal", "В главном меню");
        if (s.discord_rpc_show_time.load(std::memory_order_relaxed) &&
            presence.session_start_unix_s > 0) {
            display.time_text = ElapsedText(lang, presence.session_start_unix_s);
        }
        FillMainMenuAssets(s, display);
        display.has_party = false;
        return display;
    }

    display.details = DetailsLine(presence, s, lang);
    display.state = StateLine(presence, s, lang);

    if (s.discord_rpc_show_time.load(std::memory_order_relaxed) &&
        presence.session_start_unix_s > 0) {
        display.time_text = ElapsedText(lang, presence.session_start_unix_s);
    }

    FillAssets(presence, s, lang, display);

    if (s.discord_rpc_show_party.load(std::memory_order_relaxed)) {
        display.has_party = true;
        display.party_size = presence.hunters_count > 0 ? presence.hunters_count : 1;
        display.party_max = presence.max_hunters > 0 ? presence.max_hunters : 3;
        display.party_text = "(" + std::to_string(display.party_size) +
                             Tr(lang, " of ", " de ", " из ") + std::to_string(display.party_max) +
                             ")";
    }

    if (display.details.size() > kMaxLineLength) {
        display.details.resize(kMaxLineLength);
    }
    if (display.state.size() > kMaxLineLength) {
        display.state.resize(kMaxLineLength);
    }
    return display;
}

ActivityDisplay FormatActivity(int lang) {
    return FormatActivity(BbGameData::GetSnapshot(), lang);
}

} // namespace BbDiscordRpc::detail
