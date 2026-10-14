// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace BbMenuPointer {

constexpr std::uintptr_t kBaseBias = 0x400000;
constexpr std::size_t kListener = 0x30;
constexpr std::size_t kCount = 0x70;
constexpr std::size_t kCursor = 0x74;
constexpr std::size_t kCursorOverride = 0x78;
constexpr std::size_t kLineLen = 0x7c;
constexpr std::size_t kLines = 0x80;
constexpr std::size_t kLineScroll = 0x84;
constexpr std::size_t kScroll = 0x24c;
constexpr std::size_t kProvider = 0x258;
constexpr std::size_t kRootType = 0xc8;
constexpr std::size_t kRootData = 0xd0;
constexpr std::size_t kWraps = 0x2b0;
constexpr std::size_t kNative = 0x88;

struct Rect {
    float x1, y1, x2, y2;
};

template <typename T>
inline T Field(const std::uint8_t* base, std::size_t off) {
    T v{};
    std::memcpy(&v, base + off, sizeof(v));
    return v;
}

void SetScaleformBase(std::uintptr_t image_base);
std::uintptr_t GuestAddr(std::uintptr_t vaddr);
bool SafeRead(std::uintptr_t addr, void* out, std::size_t len);
bool GuestCode(std::uintptr_t addr);
bool Shown(std::uintptr_t obj);
bool Hit(std::uintptr_t obj, float sx, float sy, Rect* world, float grow_px = 0.0f);
int CursorOf(const std::uint8_t* c);
int ItemAt(std::uint8_t* c, float sx, float sy, std::uint64_t* packed_out);
std::uint32_t ClickButton(std::uint8_t* c, std::uint64_t packed, float sx, float sy, std::uint32_t confirm_btn);
void ShowCursor(std::uint8_t* c, std::uint64_t packed);

} // namespace BbMenuPointer
