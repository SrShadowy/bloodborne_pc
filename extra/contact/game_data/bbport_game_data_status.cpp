// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the co-op status line -- "Host (+1 NPC)", "Guest (Co-op)", "Invaded (2)" -- shown by the
// info HUD and the Discord card.
#include "bbport_game_data_internal.h"

#include <cstdio>

#include "bbport_settings.h"

namespace BbGameData::detail {
namespace {

enum CoopRole : int { RoleSolo = 0, RoleHost = 1, RoleGuest = 2 };

/// Portuguese pluralises by adding "s"/"es" to the noun; Russian only carries a count.
constexpr const char* kPortuguesePluralS = "s";
constexpr const char* kPortuguesePluralEs = "es";
constexpr const char* kEnglishPluralS = "s";

/// The host line: which kind of helper is in the party decides the wording.
std::string FormatHostStatus(const PresenceSnapshot& snap, int lang) {
    const int npc = snap.npc_hunters_count;
    const int players = snap.player_hunters_count;
    char buffer[64];

    if (npc > 0 && players == 0) {
        const char* plural = npc > 1 ? kEnglishPluralS : "";
        if (lang == BbSettings::LangPortuguese) {
            std::snprintf(buffer, sizeof(buffer), "Anfitrião (+%d NPC%s)", npc,
                          npc > 1 ? kPortuguesePluralS : "");
        } else if (lang == BbSettings::LangRussian) {
            std::snprintf(buffer, sizeof(buffer), "Хост (+%d NPC)", npc);
        } else {
            std::snprintf(buffer, sizeof(buffer), "Host (+%d NPC%s)", npc, plural);
        }
        return buffer;
    }

    if (players > 0 && npc > 0) {
        if (lang == BbSettings::LangPortuguese) {
            std::snprintf(buffer, sizeof(buffer), "Anfitrião (+%d Jog., +%d NPC)", players, npc);
        } else if (lang == BbSettings::LangRussian) {
            std::snprintf(buffer, sizeof(buffer), "Хост (+%d Игрок, +%d NPC)", players, npc);
        } else {
            std::snprintf(buffer, sizeof(buffer), "Host (+%d Player, +%d NPC)", players, npc);
        }
        return buffer;
    }

    const int hunters = snap.hunters_count - 1;
    if (hunters <= 0) {
        if (lang == BbSettings::LangPortuguese) {
            return "Anfitrião (Host)";
        }
        if (lang == BbSettings::LangRussian) {
            return "Хост";
        }
        return "Host";
    }

    if (lang == BbSettings::LangPortuguese) {
        std::snprintf(buffer, sizeof(buffer), "Anfitrião (+%d Caçador%s)", hunters,
                      hunters > 1 ? kPortuguesePluralEs : "");
    } else if (lang == BbSettings::LangRussian) {
        std::snprintf(buffer, sizeof(buffer), "Хост (+%d)", hunters);
    } else {
        std::snprintf(buffer, sizeof(buffer), "Host (+%d Hunter%s)", hunters,
                      hunters > 1 ? kEnglishPluralS : "");
    }
    return buffer;
}

/// " [! 2 Invaders]" appended when the party is being invaded.
std::string InvaderSuffix(int invaders, int lang) {
    char buffer[32];
    if (lang == BbSettings::LangPortuguese) {
        std::snprintf(buffer, sizeof(buffer), " [! %d Invasor%s]", invaders,
                      invaders > 1 ? kPortuguesePluralEs : "");
    } else if (lang == BbSettings::LangRussian) {
        std::snprintf(buffer, sizeof(buffer), " [! %d Вторженец]", invaders);
    } else {
        std::snprintf(buffer, sizeof(buffer), " [! %d Invader%s]", invaders,
                      invaders > 1 ? kEnglishPluralS : "");
    }
    return buffer;
}

} // namespace

std::string FormatCoopStatusText(const PresenceSnapshot& snap, int lang) {
    if (snap.coop_role == RoleGuest) {
        if (lang == BbSettings::LangPortuguese) {
            return "Convidado (Co-op)";
        }
        if (lang == BbSettings::LangRussian) {
            return "Гость (Кооп)";
        }
        return "Guest (Co-op)";
    }

    if (snap.coop_role != RoleHost) {
        if (snap.invaders_count <= 0) {
            if (lang == BbSettings::LangPortuguese) {
                return "Solo";
            }
            if (lang == BbSettings::LangRussian) {
                return "Одиночная";
            }
            return "Solo";
        }
        char buffer[64];
        if (lang == BbSettings::LangPortuguese) {
            std::snprintf(buffer, sizeof(buffer), "Invadido (%d Invasor%s)", snap.invaders_count,
                          snap.invaders_count > 1 ? kPortuguesePluralEs : "");
        } else if (lang == BbSettings::LangRussian) {
            std::snprintf(buffer, sizeof(buffer), "Вторжение (%d)", snap.invaders_count);
        } else {
            std::snprintf(buffer, sizeof(buffer), "Invaded (%d Invader%s)", snap.invaders_count,
                          snap.invaders_count > 1 ? kEnglishPluralS : "");
        }
        return buffer;
    }

    std::string status = FormatHostStatus(snap, lang);
    if (snap.invaders_count > 0) {
        status += InvaderSuffix(snap.invaders_count, lang);
    }
    return status;
}

} // namespace BbGameData::detail
