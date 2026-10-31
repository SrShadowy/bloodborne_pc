// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace BbTrophy {

struct TrophyListItem {
    int id = -1;
    char grade = '?'; // 'P' (Platinum), 'G' (Gold), 'S' (Silver), 'B' (Bronze)
    bool hidden = false;
    bool unlocked = false;
    std::string name;
    std::string detail;
    std::filesystem::path icon_path;
};

struct TrophyStats {
    size_t total = 0;
    size_t unlocked = 0;
    size_t platinum_total = 0;
    size_t platinum_unlocked = 0;
    size_t gold_total = 0;
    size_t gold_unlocked = 0;
    size_t silver_total = 0;
    size_t silver_unlocked = 0;
    size_t bronze_total = 0;
    size_t bronze_unlocked = 0;
};

// Returns a snapshot of all known trophies and their unlocked states
std::vector<TrophyListItem> GetTrophyList();

// Returns aggregated statistics of unlocked vs total trophies
TrophyStats GetTrophyStats();

} // namespace BbTrophy
