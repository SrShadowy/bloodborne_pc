// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bbport_title_menu.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string.h>
#include <cstring>
#include <vector>

namespace {

bool is_title_gfx_path(const char* guest) {
    if (!guest) return false;
    const size_t len = std::strlen(guest);
    static const char* const targets[] = {
        "/menu/title.gfx",
        "/menu/title_dlc.gfx",
        "/menu/title_dlc_eu.gfx"
    };
    for (const char* t : targets) {
        const size_t tlen = std::strlen(t);
        if (len >= tlen && strcasecmp(guest + len - tlen, t) == 0) {
            return true;
        }
    }
    return false;
}

bool patch_title_swf(const std::vector<uint8_t>& src, std::vector<uint8_t>& dst) {
    if (src.size() < 32 || (std::memcmp(src.data(), "GFX", 3) != 0 && std::memcmp(src.data(), "FWS", 3) != 0)) {
        return false;
    }

    const size_t rect_bits = src[8] >> 3;
    const size_t rect_bytes = (5 + 4 * rect_bits + 7) / 8;
    size_t pos = 8 + rect_bytes + 4; // header(8) + rect + fps(2) + frame_count(2)

    while (pos + 2 <= src.size()) {
        uint16_t tag_hdr = src[pos] | (uint16_t(src[pos + 1]) << 8);
        uint16_t tag_type = tag_hdr >> 6;
        size_t tag_len = tag_hdr & 0x3F;
        size_t hdr_sz = 2;
        if (tag_len == 0x3F) {
            if (pos + 6 > src.size()) return false;
            tag_len = src[pos + 2] | (size_t(src[pos + 3]) << 8) | (size_t(src[pos + 4]) << 16) | (size_t(src[pos + 5]) << 24);
            hdr_sz = 6;
        }
        if (pos + hdr_sz + tag_len > src.size()) return false;

        if (tag_type == 39 && tag_len >= 4) { // DefineSprite
            const uint8_t* sdata = src.data() + pos + hdr_sz;
            // Check for CommandList item markers
            if (memmem(sdata, tag_len, "Item_0_0", 8) && memmem(sdata, tag_len, "Item_4_0", 8)) {
                // Item_5_0 tag payload: flags=0x26, depth=1, char=31, matrix(x=-10283, y=3037), name="Item_5_0\0"
                static const uint8_t item5_tag[] = {
                    0xbf, 0x06, 0x13, 0x00, 0x00, 0x00, // Tag 26, len 19
                    0x26,                               // Flags: HasCharacter | HasMatrix | HasName
                    0x01, 0x00,                         // Depth 1
                    0x1f, 0x00,                         // Character ID 31
                    0x1f, 0x5f, 0x54, 0x5e, 0xe8,       // Matrix (tx=-10283, ty=3037)
                    'I', 't', 'e', 'm', '_', '5', '_', '0', 0x00
                };
                constexpr size_t item5_sz = sizeof(item5_tag);

                std::vector<uint8_t> new_sprite;
                new_sprite.reserve(tag_len + item5_sz);
                new_sprite.insert(new_sprite.end(), sdata, sdata + 4); // Sprite ID (2) + Frame Count (2)
                new_sprite.insert(new_sprite.end(), item5_tag, item5_tag + item5_sz);

                size_t s_off = 4;
                while (s_off + 2 <= tag_len) {
                    uint16_t sth = sdata[s_off] | (uint16_t(sdata[s_off + 1]) << 8);
                    uint16_t stt = sth >> 6;
                    size_t stl = sth & 0x3F;
                    size_t shsz = 2;
                    if (stl == 0x3F) {
                        if (s_off + 6 > tag_len) return false;
                        stl = sdata[s_off + 2] | (size_t(sdata[s_off + 3]) << 8) | (size_t(sdata[s_off + 4]) << 16) | (size_t(sdata[s_off + 5]) << 24);
                        shsz = 6;
                    }
                    if (s_off + shsz + stl > tag_len) return false;

                    std::vector<uint8_t> sbody(sdata + s_off + shsz, sdata + s_off + shsz + stl);
                    if (stt == 26 && sbody.size() >= 3) { // PlaceObject2
                        uint16_t depth = sbody[1] | (uint16_t(sbody[2]) << 8);
                        depth += 5;
                        sbody[1] = uint8_t(depth & 0xFF);
                        sbody[2] = uint8_t((depth >> 8) & 0xFF);
                    }

                    if (shsz == 6) {
                        const uint16_t hdr = (stt << 6) | 0x3F;
                        new_sprite.push_back(uint8_t(hdr & 0xFF));
                        new_sprite.push_back(uint8_t((hdr >> 8) & 0xFF));
                        const uint32_t len32 = uint32_t(sbody.size());
                        new_sprite.insert(new_sprite.end(), reinterpret_cast<const uint8_t*>(&len32), reinterpret_cast<const uint8_t*>(&len32) + 4);
                    } else {
                        const uint16_t hdr = (stt << 6) | uint16_t(sbody.size() & 0x3F);
                        new_sprite.push_back(uint8_t(hdr & 0xFF));
                        new_sprite.push_back(uint8_t((hdr >> 8) & 0xFF));
                    }
                    new_sprite.insert(new_sprite.end(), sbody.begin(), sbody.end());
                    s_off += shsz + stl;
                }

                dst.clear();
                dst.reserve(src.size() + item5_sz);
                dst.insert(dst.end(), src.begin(), src.begin() + pos);

                // Reconstruct Sprite 32 tag header
                if (hdr_sz == 6) {
                    const uint16_t hdr = (tag_type << 6) | 0x3F;
                    dst.push_back(uint8_t(hdr & 0xFF));
                    dst.push_back(uint8_t((hdr >> 8) & 0xFF));
                    const uint32_t len32 = uint32_t(new_sprite.size());
                    dst.insert(dst.end(), reinterpret_cast<const uint8_t*>(&len32), reinterpret_cast<const uint8_t*>(&len32) + 4);
                } else {
                    const uint16_t hdr = (tag_type << 6) | uint16_t(new_sprite.size() & 0x3F);
                    dst.push_back(uint8_t(hdr & 0xFF));
                    dst.push_back(uint8_t((hdr >> 8) & 0xFF));
                }
                dst.insert(dst.end(), new_sprite.begin(), new_sprite.end());
                dst.insert(dst.end(), src.begin() + pos + hdr_sz + tag_len, src.end());

                // Update total SWF file length at bytes 4..7
                const uint32_t total_len = uint32_t(dst.size());
                std::memcpy(dst.data() + 4, &total_len, 4);
                return true;
            }
        }
        pos += hdr_sz + tag_len;
    }
    return false;
}

} // namespace

extern "C" int extra_title_menu_file(const char* guest, int host) {
    if (!guest || host < 0 || !is_title_gfx_path(guest)) {
        return host;
    }
    if (const char* env = std::getenv("BB_TITLE_EXIT"); env && env[0] == '0') {
        return host;
    }
    if (const char* env = std::getenv("BB_GAME_MENU"); env && env[0] == '0') {
        return host;
    }

    struct stat s{};
    if (fstat(host, &s) != 0 || s.st_size <= 0 || s.st_size > (16 << 20)) {
        return host;
    }

    std::vector<uint8_t> src(static_cast<size_t>(s.st_size));
    if (pread(host, src.data(), src.size(), 0) != static_cast<ssize_t>(src.size())) {
        return host;
    }

    // shortcut: if already patched on disk by a mod, pass host through intact
    if (::memmem(src.data(), src.size(), "Item_5_0\0", 9)) {
        return host;
    }

    std::vector<uint8_t> patched;
    if (!patch_title_swf(src, patched)) {
        return host;
    }

    int fixed = memfd_create("title_menu.gfx", MFD_CLOEXEC);
    if (fixed < 0 || write(fixed, patched.data(), patched.size()) != static_cast<ssize_t>(patched.size()) || lseek(fixed, 0, SEEK_SET) != 0) {
        if (fixed >= 0) close(fixed);
        return host;
    }

    close(host);
    static std::atomic<bool> logged{false};
    if (!logged.exchange(true)) {
        std::printf("Runtime: %s: title menu command list expanded to 6 rows in memory for Exit Game\n", guest);
    }
    return fixed;
}
