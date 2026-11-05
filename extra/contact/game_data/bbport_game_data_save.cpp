// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the character of the open save slot -- the name, level and playtime read from
// userdata0010, and which slot the game has open.
#include "bbport_game_data_internal.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>

namespace BbGameData::detail {
namespace {

namespace fs = std::filesystem;

/// How often the save metadata is re-read (a slot switch forces an immediate read).
constexpr auto kSaveRefreshInterval = std::chrono::milliseconds(5000);

/// userdata0010 holds one fixed-size record per slot; these are the fields inside a record.
constexpr size_t kSlotRecordStride = 0x192;
constexpr size_t kFirstSlotNameOffset = 0x1096;
constexpr size_t kLevelOffsetFromName = 34;
constexpr size_t kPlaytimeOffsetFromLevel = 4;
constexpr size_t kNameBufferChars = 17;
constexpr size_t kNameReadBytes = 32;

/// BBSC_SAVE_DIR overrides the search, for installations that keep the save elsewhere.
constexpr const char* kSaveDirEnv = "BBSC_SAVE_DIR";
/// The user directory the launcher passes to the emulator.
constexpr const char* kUserDirEnv = "BB_GPU_USER_DIR";
constexpr const char* kDefaultUserDir = "user";
/// Save folders seen in the wild, relative to <user>/savedata/<slot>.
constexpr const char* kSaveDataDir = "savedata";
constexpr const char* kSaveProductDirs[] = {"CUSA00207", "CUSA03173"};
constexpr const char* kSaveFileDir = "SPRJ0005";
/// The first slot directory is often the launcher's savedata/1; probe it before iterating.
constexpr const char* kFirstSlotIndex = "1";
constexpr const char* kSlotFilePrefix = "userdata";
constexpr int kSlotFileDigits = 4;

/// UTF-16LE (the save's character names) to UTF-8, stopping at the first NUL.
std::string Utf16LeToUtf8(const char16_t* utf16, size_t max_chars) {
    std::string utf8;
    for (size_t i = 0; i < max_chars && utf16[i] != 0; ++i) {
        const char16_t c = utf16[i];
        if (c < 0x80) {
            utf8.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | ((c >> 6) & 0x1F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xE0 | ((c >> 12) & 0x0F)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return utf8;
}

/// The directory holding SPRJ0005 (the per-slot save files), searched once and then cached.
fs::path FindSaveDirectory() {
    static fs::path cached_dir;
    static bool checked = false;
    if (checked) {
        return cached_dir;
    }

    if (const char* env_save = std::getenv(kSaveDirEnv); env_save != nullptr && fs::is_directory(env_save)) {
        cached_dir = env_save;
        checked = true;
        return cached_dir;
    }

    const char* user_dir = std::getenv(kUserDirEnv);
    const fs::path base = user_dir != nullptr ? user_dir : kDefaultUserDir;
    std::error_code ec;

    // The usual layout: <user>/savedata/1/<product>/SPRJ0005, any product.
    for (const char* product : kSaveProductDirs) {
        fs::path candidate = base / kSaveDataDir / kFirstSlotIndex / product / kSaveFileDir;
        if (fs::is_directory(candidate, ec)) {
            cached_dir = candidate;
            checked = true;
            return cached_dir;
        }
    }

    // A launcher that numbered the slots differently: walk them.
    const fs::path save_data = base / kSaveDataDir;
    if (fs::is_directory(save_data, ec)) {
        for (const auto& entry : fs::directory_iterator(save_data, ec)) {
            for (const char* product : kSaveProductDirs) {
                const fs::path candidate = entry.path() / product / kSaveFileDir;
                if (fs::is_directory(candidate, ec)) {
                    cached_dir = candidate;
                    checked = true;
                    return cached_dir;
                }
            }
        }
    }

    checked = true;
    return {};
}

/// "userdata0031": the file that belongs to slot `slot` in the save directory.
fs::path SlotFile(const fs::path& dir, int slot) {
    char name[32];
    std::snprintf(name, sizeof(name), "%s%0*d", kSlotFilePrefix, kSlotFileDigits, slot);
    return dir / name;
}

/// The most recently written slot, used until the game reports which one it opened.
int NewestSlot(const fs::path& dir) {
    std::error_code ec;
    fs::file_time_type newest{};
    int best_slot = -1;
    for (int slot = 0; slot < kSaveSlotCount; ++slot) {
        const auto written = fs::last_write_time(SlotFile(dir, slot), ec);
        if (!ec && written > newest) {
            newest = written;
            best_slot = slot;
        }
    }
    return best_slot;
}

/// Reads the name/level/playtime record of `slot` from userdata0010.
bool ReadSlotRecord(const fs::path& dir, int slot, std::string& name, std::uint32_t& level,
                    std::uint32_t& playtime) {
    std::ifstream file(SlotFile(dir, kSlotMetadataIndex), std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    const size_t name_offset = kFirstSlotNameOffset + slot * kSlotRecordStride;
    const size_t level_offset = name_offset + kLevelOffsetFromName;
    const size_t playtime_offset = level_offset + kPlaytimeOffsetFromLevel;

    char16_t name_utf16[kNameBufferChars] = {};
    file.seekg(name_offset);
    file.read(reinterpret_cast<char*>(name_utf16), kNameReadBytes);

    file.seekg(level_offset);
    std::uint32_t level_value = 0;
    std::uint32_t playtime_value = 0;
    file.read(reinterpret_cast<char*>(&level_value), sizeof(level_value));
    file.read(reinterpret_cast<char*>(&playtime_value), sizeof(playtime_value));

    std::string decoded = Utf16LeToUtf8(name_utf16, kNameReadBytes / sizeof(char16_t));
    if (decoded.empty()) {
        return false;
    }
    name = std::move(decoded);
    level = level_value;
    playtime = playtime_value;
    return true;
}

} // namespace

void RefreshSaveCharacter() {
    const auto now = std::chrono::steady_clock::now();
    if (last_save_read.time_since_epoch().count() != 0 &&
        (now - last_save_read) < kSaveRefreshInterval) {
        return;
    }
    last_save_read = now;

    const fs::path dir = FindSaveDirectory();
    if (dir.empty()) {
        return;
    }

    int slot = active_save_slot.load(std::memory_order_relaxed);
    if (slot < 0 || slot >= kSaveSlotCount) {
        slot = NewestSlot(dir);
        if (slot >= 0) {
            active_save_slot.store(slot, std::memory_order_relaxed);
        } else {
            slot = 0;
        }
    }

    std::string name;
    std::uint32_t level = 0;
    std::uint32_t playtime = 0;
    if (ReadSlotRecord(dir, slot, name, level, playtime)) {
        cached_char_name = std::move(name);
        cached_char_level = level;
        cached_save_seconds = playtime;
    }
}

std::uint32_t SavePlaytimeSeconds() {
    return cached_save_seconds;
}

void SetActiveSaveSlot(int slot) {
    if (slot < 0 || slot >= kSaveSlotCount) {
        return;
    }
    active_save_slot.store(slot, std::memory_order_relaxed);
    last_save_read = {};
    RefreshSaveCharacter();
}

} // namespace BbGameData::detail
