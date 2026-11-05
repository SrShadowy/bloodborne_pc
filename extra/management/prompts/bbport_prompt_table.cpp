// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the game's key guide icons, known by their bytes. The hashes come from the game's own
// menu/common.tpf.dcx -- so they work on any release and on mods of it -- read once on a
// background thread. bbport_prompt_draw.cpp draws the port's replacements from these tables.
#include "bbport_prompt_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <miniz.h>
#include <xxhash.h>

#include "bb_env.h"

extern "C" {
// runtime_file.c
int runtime_file_host_path(const char* guest, char* out, std::size_t size);
}

namespace BbPrompts::detail {
namespace {

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

/// The game's file, and the layout of the two containers inside it.
constexpr const char* kGuestTable = "/app0/dvdroot_ps4/menu/common.tpf.dcx";
/// The DCX header: "DCX\0", "DCS\0" at 0x18, "DFLT" at 0x28, "DCA\0" at 0x44, the stream after.
constexpr std::size_t kDcxHeaderBytes = 0x50;
/// The largest payload the DCX header may name (a corrupt length must not be trusted).
constexpr u32 kMaxPayloadBytes = 256u << 20;
/// TPF (PS4): "TPF\0", count at 8, name encoding at 0xe, then 36-byte entries from 0x10.
constexpr std::size_t kTpfHeaderBytes = 0x10;
constexpr std::size_t kTpfStride = 36;
/// A TPF entry's name is NUL-terminated; nothing legitimate is longer.
constexpr std::size_t kMaxNameLength = 64;

std::once_flag s_loader_started;
std::once_flag s_loader_joined;
std::thread s_loader;

u32 Be32(const u8* p) {
    return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3];
}
u32 Le32(const u8* p) {
    return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24;
}

/// Reads a whole file into memory; empty when it cannot be read.
std::vector<u8> ReadFile(const char* path) {
    std::vector<u8> file;
    if (FILE* f = std::fopen(path, "rb")) {
        std::fseek(f, 0, SEEK_END);
        file.resize(std::size_t(std::max(0L, std::ftell(f))));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(file.data(), 1, file.size(), f) != file.size()) {
            file.clear();
        }
        std::fclose(f);
    }
    return file;
}

/// The DCX container's payload (a zlib "DFLT" stream); empty when the file is not one.
std::vector<u8> UnpackDcx(const char* path) {
    const std::vector<u8> file = ReadFile(path);
    if (file.size() < kDcxHeaderBytes || std::memcmp(file.data(), "DCX\0", 4) != 0 ||
        std::memcmp(file.data() + 0x18, "DCS\0", 4) != 0 ||
        std::memcmp(file.data() + 0x28, "DFLT", 4) != 0 ||
        std::memcmp(file.data() + 0x44, "DCA\0", 4) != 0) {
        std::printf("Button prompts: %s is not a DCX (DFLT) file\n", path);
        return {};
    }
    const u32 unpacked = Be32(file.data() + 0x1c);
    const std::size_t stream = 0x44 + Be32(file.data() + 0x48);
    if (unpacked == 0 || unpacked > kMaxPayloadBytes || stream >= file.size()) {
        return {};
    }
    std::vector<u8> tpf(unpacked);
    mz_ulong length = unpacked;
    if (mz_uncompress(tpf.data(), &length, file.data() + stream, mz_ulong(file.size() - stream)) !=
            MZ_OK ||
        length != unpacked) {
        std::printf("Button prompts: %s does not unpack\n", path);
        return {};
    }
    return tpf;
}

/// The name of the TPF entry beginning at `entry` (UTF-16 or ASCII, per the file's header).
std::string EntryName(const std::vector<u8>& tpf, const u8* entry, bool utf16) {
    const u32 name_offset = Le32(entry + 24);
    if (name_offset >= tpf.size()) {
        return {};
    }
    const std::size_t step = utf16 ? 2 : 1;
    std::string name;
    for (std::size_t p = name_offset; p + step <= tpf.size(); p += step) {
        const u32 c = utf16 ? u32(tpf[p]) | u32(tpf[p + 1]) << 8 : tpf[p];
        if (c == 0 || name.size() > kMaxNameLength) {
            break;
        }
        name.push_back(c < 0x80 ? char(c) : '?');
    }
    return name;
}

/// Where the entry's payload begins, or null when it does not fit the file.
const u8* EntryPayload(const std::vector<u8>& tpf, const u8* entry, std::size_t size) {
    const u32 offset = Le32(entry);
    if (u64(offset) + size > tpf.size()) {
        return nullptr;
    }
    return tpf.data() + offset;
}

/// Matches one TPF entry against the icon and atlas tables.
void TakeEntry(const std::vector<u8>& tpf, const u8* entry, const std::string& name, u32* found) {
    for (std::size_t k = 0; k < kAtlasCount; ++k) {
        const AtlasDef& atlas = kAtlases[k];
        const u8* payload = EntryPayload(tpf, entry, atlas.bytes);
        if (name == atlas.name && payload != nullptr) {
            g_atlas_hashes[k] = XXH3_64bits(payload, atlas.bytes);
        }
    }
    for (std::size_t k = 0; k < kIconCount; ++k) {
        const u8* payload = EntryPayload(tpf, entry, kGameIconBytes);
        if (name != kIcons[k].name || payload == nullptr) {
            continue;
        }
        g_icon_hashes[k] = XXH3_64bits(payload, kGameIconBytes);
        std::memcpy(g_icon_bytes[k].data(), payload, kGameIconBytes);
        ++*found;
        if (const char* trace = bb_env("BB_PROMPT_TRACE"); trace && trace[0] == '1') {
            std::printf("Prompt trace: %s at %#x in the TPF, format %#x %ux%u, hash %016llx\n",
                        kIcons[k].name, Le32(entry), entry[8], u32(entry[12]) | u32(entry[13]) << 8,
                        u32(entry[14]) | u32(entry[15]) << 8,
                        (unsigned long long)g_icon_hashes[k]);
        }
    }
}

/// For each atlas, the cells whose icon was found (and so can be replaced).
u32 MapAtlasCells() {
    u32 atlases = 0;
    for (std::size_t k = 0; k < kAtlasCount; ++k) {
        if (g_atlas_hashes[k] == 0) {
            continue;
        }
        const AtlasDef& atlas = kAtlases[k];
        for (std::size_t c = 0; c < atlas.cells.size(); ++c) {
            for (std::size_t i = 0; i < kIconCount; ++i) {
                if (g_icon_hashes[i] != 0 && std::string_view{kIcons[i].name} == atlas.icons[c]) {
                    g_atlas_cells[k].push_back(
                        {atlas.cells[c].first, atlas.cells[c].second, u32(i + 1)});
                }
            }
        }
        atlases += g_atlas_cells[k].empty() ? 0 : 1;
    }
    return atlases;
}

} // namespace

std::array<u64, kIconCount> g_icon_hashes{}; // 0: not in the game's file
std::array<u64, kAtlasCount> g_atlas_hashes{};
std::array<std::vector<AtlasCell>, kAtlasCount> g_atlas_cells{};
std::array<std::array<u8, kGameIconBytes>, kIconCount> g_icon_bytes{};
std::atomic<bool> g_table_ready{false};

bool Switch() {
    static const bool on = !bb_env_off("BB_BUTTON_PROMPTS");
    return on;
}

void LoadTable() {
    char path[1024];
    if (runtime_file_host_path(kGuestTable, path, sizeof(path)) != 0) {
        std::printf("Button prompts: %s not found: the game's icons stay\n", kGuestTable);
        return;
    }
    const std::vector<u8> tpf = UnpackDcx(path);
    if (tpf.size() < kTpfHeaderBytes || std::memcmp(tpf.data(), "TPF\0", 4) != 0) {
        return;
    }
    const u32 count = Le32(tpf.data() + 8);
    const bool utf16 = tpf[0xe] == 1 || tpf[0xe] == 2;

    u32 found = 0;
    for (u32 i = 0; i < count && kTpfHeaderBytes + (i + 1) * kTpfStride <= tpf.size(); ++i) {
        const u8* entry = tpf.data() + kTpfHeaderBytes + i * kTpfStride;
        const std::string name = EntryName(tpf, entry, utf16);
        if (!name.empty()) {
            TakeEntry(tpf, entry, name, &found);
        }
    }

    const u32 atlases = MapAtlasCells();
    std::printf("Button prompts: %u of %zu HUD icon atlases found\n", atlases, kAtlasCount);
    // Not Enabled(): it waits for this thread.
    std::printf("Button prompts: %u of the game's %zu key guide icons found (%s)\n", found, kIconCount,
                found != 0 ? "drawn for the keyboard and other gamepads; BB_BUTTON_PROMPTS=0: off"
                           : "off");
}

void StartLoader() {
    std::call_once(s_loader_started, [] {
        if (!Switch()) {
            return;
        }
        InitFont();
        s_loader = std::thread{LoadTable};
    });
}

void WaitTable() {
    std::call_once(s_loader_joined, [] {
        StartLoader();
        if (s_loader.joinable()) {
            s_loader.join();
        }
        g_table_ready = true;
    });
}

} // namespace BbPrompts::detail
