// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the game itself keeps in memory -- the packed id of the map being played and the
// playtime counter. The rest of the module is in bbport_game_data.cpp (session and snapshot),
// bbport_game_data_death.cpp (the death counter), bbport_game_data_save.cpp (the open save slot),
// bbport_game_data_names.cpp (map/boss names) and bbport_game_data_party.cpp (who else is here).
#include "bbport_game_data_internal.h"

#include <cstddef>

#include "bbgpu.h"
#include "bbport_scaleform_hit.h"

namespace BbGameData::detail {
namespace {

/// The in-game clock at GameDataMan + 0x94, in milliseconds (0xd693a018 max).
constexpr std::size_t kPlaytimeMs = 0x94;
constexpr std::uint32_t kMillisecondsPerSecond = 1000;

/// The map list: current index at +0x20, array at +0x10; the array holds count at +0x18 and
/// entries at +0x20, one entry per map with the packed id at +8.
constexpr std::size_t kMapListIndex = 0x20;
constexpr std::size_t kMapListArray = 0x10;
constexpr std::size_t kMapArrayCount = 0x18;
constexpr std::size_t kMapArrayEntries = 0x20;
constexpr std::size_t kMapEntryStride = 0xA0;
constexpr std::size_t kMapEntryId = 8;
constexpr std::uint32_t kPackedIdNone = 0xFFFFFFFF;

} // namespace

bool GetPackedMap(std::uint32_t& packed) {
    const uintptr_t img = bbgpu_get_guest_image_base();
    if (img == 0) {
        return false;
    }

    uintptr_t map_list = 0;
    if (!BbMenuPointer::SafeRead(img + kCurrentMapList, &map_list, sizeof(map_list)) ||
        map_list < kMinValidPointer) {
        return false;
    }

    std::int32_t index = -1;
    uintptr_t map_array = 0;
    if (!BbMenuPointer::SafeRead(map_list + kMapListIndex, &index, sizeof(index)) || index < 0 ||
        !BbMenuPointer::SafeRead(map_list + kMapListArray, &map_array, sizeof(map_array)) ||
        map_array < kMinValidPointer) {
        return false;
    }

    std::int32_t count = 0;
    uintptr_t entries = 0;
    if (!BbMenuPointer::SafeRead(map_array + kMapArrayCount, &count, sizeof(count)) || count <= 0 ||
        index >= count ||
        !BbMenuPointer::SafeRead(map_array + kMapArrayEntries, &entries, sizeof(entries)) ||
        entries < kMinValidPointer) {
        return false;
    }

    const uintptr_t entry = entries + static_cast<std::size_t>(index) * kMapEntryStride;
    std::uint32_t value = 0;
    if (!BbMenuPointer::SafeRead(entry + kMapEntryId, &value, sizeof(value)) || value == 0 ||
        value == kPackedIdNone) {
        return false;
    }
    packed = value;
    return true;
}

std::uint32_t MemoryPlaytimeSeconds() {
    const uintptr_t img = bbgpu_get_guest_image_base();
    if (img == 0) {
        return 0;
    }
    uintptr_t gdm = 0;
    if (!BbMenuPointer::SafeRead(img + kGameDataMan, &gdm, sizeof(gdm)) ||
        gdm < kMinValidPointer) {
        return 0;
    }
    // [[GameDataMan]+8]+0x94 is the blood echo count; the playtime is GameDataMan + 0x94 itself.
    std::uint32_t milliseconds = 0;
    if (!BbMenuPointer::SafeRead(gdm + kPlaytimeMs, &milliseconds, sizeof(milliseconds)) ||
        milliseconds == 0) {
        return 0;
    }
    return milliseconds / kMillisecondsPerSecond;
}

} // namespace BbGameData::detail
