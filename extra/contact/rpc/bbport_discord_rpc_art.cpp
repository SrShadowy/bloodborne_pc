// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the card's artwork -- which image the BBPort application shows per area, and the store
// art shadPS4's application shows instead. The card's text is in bbport_discord_rpc_activity.cpp.
#include "bbport_discord_rpc_internal.h"

#include <string>

#include "bbport_game_data.h"
#include "bbport_settings.h"

namespace BbDiscordRpc::detail {
namespace {

/// Artwork for the default (BBPort) application, keyed by the area's English name.
constexpr const char* kFallbackArt = "app_icon";
constexpr const char* kRemoteArt =
    "https://store.playstation.com/store/api/chihiro/00_09_000/titlecontainer/US/en/999/"
    "CUSA03173_00/image";
constexpr const char* kGameName = "Bloodborne (BBPort)";
constexpr const char* kBuildName = "BBport Shadowy Edition";
/// shadPS4's application shows a different small icon and the store art.
constexpr const char* kShadps4SmallArt = "hunter";

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

/// The artwork bundled with the BBPort application for an area.
std::string MapArtKey(const std::string& map_en) {
    if (map_en.find("Hunter's Dream") != std::string::npos ||
        map_en.find("Abandoned Old Workshop") != std::string::npos) {
        return "hunter_dream";
    }
    if (map_en.find("Central Yharnam") != std::string::npos ||
        map_en.find("Iosefka") != std::string::npos) {
        return "central_yharnam";
    }
    if (map_en.find("Cathedral") != std::string::npos ||
        map_en.find("Healing Church") != std::string::npos ||
        map_en.find("Altar of Despair") != std::string::npos) {
        return "cathedral_ward";
    }
    if (map_en.find("Old Yharnam") != std::string::npos) {
        return "old_yharnam";
    }
    if (map_en.find("Hemwick") != std::string::npos) {
        return "hemwick";
    }
    if (map_en.find("Cainhurst") != std::string::npos) {
        return "cainhurst";
    }
    if (map_en.find("Forbidden Woods") != std::string::npos) {
        return "forbidden_woods";
    }
    if (map_en.find("Yahar'gul") != std::string::npos ||
        map_en.find("Unseen Village") != std::string::npos ||
        map_en.find("Hypogean Gaol") != std::string::npos) {
        return "yahargul";
    }
    if (map_en.find("Byrgenwerth") != std::string::npos ||
        map_en.find("Lecture") != std::string::npos ||
        map_en.find("Moonside") != std::string::npos) {
        return "byrgenwerth";
    }
    if (map_en.find("Nightmare Frontier") != std::string::npos ||
        map_en.find("Nightmare of Mensis") != std::string::npos) {
        return "nightmare";
    }
    if (map_en.find("Hunter's Nightmare") != std::string::npos ||
        map_en.find("Research Hall") != std::string::npos ||
        map_en.find("Fishing Hamlet") != std::string::npos) {
        return "hunters_nightmare";
    }
    if (map_en.find("Chalice") != std::string::npos) {
        return "chalice_dungeon";
    }
    return kFallbackArt;
}

} // namespace

void FillMainMenuAssets(const BbSettings::Values& s, ActivityDisplay& display) {
    const bool custom_app = s.discord_client_id != kShadps4ClientId;
    display.large_image = custom_app ? kFallbackArt : kRemoteArt;
    display.large_text = kGameName;
    display.small_image = kFallbackArt;
    display.small_text = kBuildName;
}

void FillAssets(const BbGameData::PresenceSnapshot& presence, const BbSettings::Values& s, int lang,
                ActivityDisplay& display) {
    const bool show_area = s.discord_rpc_show_area.load(std::memory_order_relaxed);
    const bool is_custom_app = (s.discord_client_id != kShadps4ClientId);

    if (is_custom_app) {
        display.large_image = show_area ? MapArtKey(presence.map_name_en) : kFallbackArt;
        std::string area_text = show_area ? AreaName(presence, lang) : std::string();
        if (area_text.empty()) {
            area_text = kGameName;
        }
        display.large_text = area_text;
        display.small_image = kFallbackArt;
    } else {
        display.large_image = kRemoteArt;
        display.large_text = show_area ? AreaName(presence, lang) : std::string(kGameName);
        if (display.large_text.empty()) {
            display.large_text = kGameName;
        }
        display.small_image = kShadps4SmallArt;
    }
    display.small_text = kBuildName;
}

} // namespace BbDiscordRpc::detail
