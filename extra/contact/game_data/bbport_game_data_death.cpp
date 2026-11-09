// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the death counter. Reads the local player's HP and the two death flags, and counts a
// death when the alive -> dead edge is seen (debounced, since the flag stays set through the
// respawn). GetSnapshot() in bbport_game_data.cpp calls it once per frame.
#include "bbport_game_data_internal.h"

#include <cstdio>

#include "bbgpu.h"
#include "bbport_scaleform_hit.h"

namespace BbGameData::detail {
namespace {

/// A death is only counted once per this window: the flag stays set through the respawn.
constexpr auto kDeathDebounce = std::chrono::seconds(4);

/// The last death counted, and whether the player was alive at the previous sample.
std::chrono::steady_clock::time_point g_last_death_time{};
bool g_player_was_alive = false;

/// The local player's HP, and the death flags that go with it: 0 when the character cannot be
/// read yet (`readable` false).
struct PlayerVitals {
    std::int32_t hp = 0;
    bool death_flag = false;
    bool readable = false;
};

/// Reads the local player through WorldChrMan.
PlayerVitals ReadLocalPlayer(uintptr_t img) {
    PlayerVitals vitals;
    uintptr_t world_chr = 0;
    if (!BbMenuPointer::SafeRead(img + kWorldChrMan, &world_chr, sizeof(world_chr)) ||
        world_chr < kMinValidPointer) {
        return vitals;
    }
    uintptr_t local = 0;
    if (!BbMenuPointer::SafeRead(world_chr + kLocalCharacter, &local, sizeof(local)) ||
        local < kMinValidPointer) {
        return vitals;
    }
    uintptr_t modules = 0;
    if (!BbMenuPointer::SafeRead(local + kChrModules, &modules, sizeof(modules)) ||
        modules < kMinValidPointer) {
        return vitals;
    }
    uintptr_t data = 0;
    if (!BbMenuPointer::SafeRead(modules + kModulesData, &data, sizeof(data)) ||
        data < kMinValidPointer) {
        return vitals;
    }
    std::int32_t hp = 0;
    if (!BbMenuPointer::SafeRead(data + kDataHp, &hp, sizeof(hp))) {
        return vitals;
    }
    vitals.hp = hp;
    vitals.readable = true;

    uintptr_t control = 0;
    std::uint16_t death_bit = 0;
    if (BbMenuPointer::SafeRead(local + kChrControl, &control, sizeof(control)) &&
        control >= kMinValidPointer &&
        BbMenuPointer::SafeRead(control + kControlDeathFlag, &death_bit, sizeof(death_bit)) &&
        (death_bit & 1) != 0) {
        vitals.death_flag = true;
    }

    uintptr_t global = 0;
    std::uint8_t global_dead = 0;
    if (BbMenuPointer::SafeRead(img + kGlobalState, &global, sizeof(global)) &&
        global >= kMinValidPointer &&
        BbMenuPointer::SafeRead(global + kGlobalDeadFlag, &global_dead, sizeof(global_dead)) &&
        global_dead != 0) {
        vitals.death_flag = true;
    }
    return vitals;
}

} // namespace

void CheckPlayerDeath(std::uintptr_t img, bool in_game) {
    if (img == 0 || !in_game) {
        g_player_was_alive = false;
        return;
    }

    const PlayerVitals vitals = ReadLocalPlayer(img);
    if (!vitals.readable) {
        return;
    }

    if (vitals.hp > 0 && !vitals.death_flag) {
        g_player_was_alive = true;
        return;
    }
    if (!g_player_was_alive) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool first_death = g_last_death_time.time_since_epoch().count() == 0;
    if (first_death || (now - g_last_death_time) > kDeathDebounce) {
        g_last_death_time = now;
        const std::uint32_t deaths = session_deaths.fetch_add(1, std::memory_order_relaxed) + 1;
        std::printf("[BBPort] Player died! Session deaths: %u\n", deaths);
        std::fflush(stdout);
    }
    g_player_was_alive = false;
}

} // namespace BbGameData::detail
