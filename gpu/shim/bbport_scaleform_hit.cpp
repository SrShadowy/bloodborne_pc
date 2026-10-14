// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_scaleform_hit.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csetjmp>
#include <csignal>
#include <cstring>

extern "C" __thread sigjmp_buf* runtime_fault_recover;

#if defined(_WIN32)
#define GUEST_ABI __attribute__((sysv_abi))
#else
#define GUEST_ABI
#endif

namespace BbMenuPointer {
namespace {

constexpr std::uintptr_t kChild = 0x1edb220;
constexpr std::uintptr_t kCursorName = 0x4d34044; // "Cursor"
constexpr std::uintptr_t kWorldMatrix = 0x402040;
constexpr std::uintptr_t kExecEnd = 0x54d96dc;

constexpr std::size_t kWrapperValue = 0x30;
constexpr std::size_t kWrapperInterface = 0x40;
constexpr std::size_t kWrapperType = 0x48;
constexpr std::size_t kWrapperData = 0x50;
constexpr std::size_t kParent = 0x38;
constexpr std::size_t kFlags = 0x6b;
constexpr std::uint8_t kVisible = 0x40;
constexpr std::size_t kGetBounds = 0x1f0;

std::uintptr_t g_base = 0;

struct Wrapper {
    alignas(16) std::uint8_t b[0x80]{};
    Wrapper() = default;
    Wrapper(const Wrapper&) = delete;
    Wrapper& operator=(const Wrapper&) = delete;
    ~Wrapper() {
        if (!(Field<std::uint32_t>(b, kWrapperType) & 0x40)) return;
        const auto oi = Field<std::uintptr_t>(b, kWrapperInterface);
        std::uintptr_t vt = 0, release = 0;
        if (oi && SafeRead(oi, &vt, sizeof(vt)) && SafeRead(vt + 0x18, &release, sizeof(release)) &&
            GuestCode(release)) {
            using ReleaseFn = void(GUEST_ABI*)(void*, void*, std::uintptr_t);
            reinterpret_cast<ReleaseFn>(release)(reinterpret_cast<void*>(oi), b + kWrapperValue,
                                                 Field<std::uintptr_t>(b, kWrapperData));
        }
    }
    std::uintptr_t Display() const {
        if ((Field<std::uint32_t>(b, kWrapperType) & 0x8f) != 10) return 0;
        std::uintptr_t d = 0;
        const auto data = Field<std::uintptr_t>(b, kWrapperData);
        return data && SafeRead(data + kNative, &d, sizeof(d)) ? d : 0;
    }
};

using MakeItemFn = void(GUEST_ABI*)(void*, void*, const std::uint64_t*);
using ChildFn = void(GUEST_ABI*)(void*, const void*, const char*, int);

} // namespace

void SetScaleformBase(std::uintptr_t image_base) {
    g_base = image_base;
}

std::uintptr_t GuestAddr(std::uintptr_t vaddr) {
    return g_base + (vaddr - kBaseBias);
}

bool SafeRead(std::uintptr_t addr, void* out, std::size_t len) {
    if (addr < 0x10000 || !out || len == 0) return false;
    sigjmp_buf jb;
    if (sigsetjmp(jb, 0) != 0) {
        runtime_fault_recover = nullptr;
        return false;
    }
    runtime_fault_recover = &jb;
    std::atomic_signal_fence(std::memory_order_seq_cst);
    std::memcpy(out, reinterpret_cast<const void*>(addr), len);
    std::atomic_signal_fence(std::memory_order_seq_cst);
    runtime_fault_recover = nullptr;
    return true;
}

bool GuestCode(std::uintptr_t addr) {
    if (!g_base || addr < GuestAddr(0x400000) || addr >= GuestAddr(kExecEnd)) return false;
    std::uint8_t p[4] = {};
    return SafeRead(addr, p, sizeof(p)) && p[0] == 0x55 && p[1] == 0x48 && p[2] == 0x89 && p[3] == 0xe5;
}

bool Shown(std::uintptr_t obj) {
    for (int depth = 0; obj && depth < 32; ++depth) {
        std::uint8_t flags = 0;
        if (!SafeRead(obj + kFlags, &flags, sizeof(flags)) || !(flags & kVisible)) return false;
        if (!SafeRead(obj + kParent, &obj, sizeof(obj))) return false;
    }
    return true;
}

bool Hit(std::uintptr_t obj, float sx, float sy, Rect* world, float grow_px) {
    std::uintptr_t vt = 0, get_bounds = 0;
    if (!SafeRead(obj, &vt, sizeof(vt)) || !SafeRead(vt + kGetBounds, &get_bounds, sizeof(get_bounds)) ||
        !GuestCode(get_bounds)) {
        return false;
    }
    alignas(16) float identity[8] = {1.0f, 0, 0, 0, 0, 1.0f, 0, 0};
    alignas(16) float r[4] = {};
    using GetBoundsFn = void(GUEST_ABI*)(float*, void*, const float*);
    reinterpret_cast<GetBoundsFn>(get_bounds)(r, reinterpret_cast<void*>(obj), identity);
    if (!(r[2] > r[0]) || !(r[3] > r[1])) return false;

    alignas(16) float m[8] = {1.0f, 0, 0, 0, 0, 1.0f, 0, 0};
    using WorldMatrixFn = void(GUEST_ABI*)(void*, float*);
    reinterpret_cast<WorldMatrixFn>(GuestAddr(kWorldMatrix))(reinterpret_cast<void*>(obj), m);
    if (world) {
        const float xs[2] = {r[0], r[2]}, ys[2] = {r[1], r[3]};
        world->x1 = world->y1 = 1e9f;
        world->x2 = world->y2 = -1e9f;
        for (float x : xs) {
            for (float y : ys) {
                const float wx = (m[0] * x + m[1] * y + m[3]) / 20.0f;
                const float wy = (m[4] * x + m[5] * y + m[7]) / 20.0f;
                world->x1 = std::fmin(world->x1, wx);
                world->x2 = std::fmax(world->x2, wx);
                world->y1 = std::fmin(world->y1, wy);
                world->y2 = std::fmax(world->y2, wy);
            }
        }
    }
    const float det = m[0] * m[5] - m[1] * m[4];
    if (std::fabs(det) < 1e-12f) return false;
    const float px = sx * 20.0f - m[3], py = sy * 20.0f - m[7];
    const float lx = (m[5] * px - m[1] * py) / det;
    const float ly = (-m[4] * px + m[0] * py) / det;
    const float g = grow_px * 20.0f / std::sqrt(std::fabs(det));
    return lx >= r[0] - g && lx <= r[2] + g && ly >= r[1] - g && ly <= r[3] + g;
}

int CursorOf(const std::uint8_t* c) {
    const auto o = Field<std::int32_t>(c, kCursorOverride);
    return o >= 0 ? o : Field<std::int32_t>(c, kCursor);
}

int ItemAt(std::uint8_t* c, float sx, float sy, std::uint64_t* packed_out) {
    const int count = Field<std::int32_t>(c, kCount);
    const int cells = Field<std::int32_t>(c, kLineLen);
    const int lines = Field<std::int32_t>(c, kLines);
    const int cell_scroll = Field<std::int32_t>(c, kLineScroll);
    const int line_scroll = Field<std::int32_t>(c, kScroll);
    const bool wraps = Field<std::uint8_t>(c, kWraps) != 0;
    if (count <= 0 || cells <= 0 || lines <= 0 || cells > 64 || lines > 64) return -1;
    const int stride = lines != 1 ? std::max(cells, 1) : std::max(std::max(cells, count), 1);
    const auto provider = Field<std::uintptr_t>(c, kProvider);
    std::uintptr_t pvt = 0, make = 0;
    if (!provider || !SafeRead(provider, &pvt, sizeof(pvt)) || !SafeRead(pvt + 0x18, &make, sizeof(make)) ||
        !GuestCode(make)) {
        return -1;
    }
    const auto make_fn = reinterpret_cast<MakeItemFn>(make);
    const auto child_fn = reinterpret_cast<ChildFn>(GuestAddr(kChild));
    const auto* cursor_name = reinterpret_cast<const char*>(GuestAddr(kCursorName));
    int best = -1;
    float best_d = 0.0f;
    std::uint64_t best_packed = 0;
    for (int line = 0; line < lines; ++line) {
        for (int cell = 0; cell < cells; ++cell) {
            int index = (line_scroll + line) * stride + cell_scroll + cell;
            if (wraps && count) index %= count;
            if (index < 0 || index >= count) continue;
            const std::uint64_t packed = static_cast<std::uint32_t>(cell) |
                                         (static_cast<std::uint64_t>(static_cast<std::uint32_t>(line)) << 32);
            Wrapper item;
            make_fn(item.b, reinterpret_cast<void*>(provider), &packed);
            const std::uintptr_t obj = item.Display();
            if (!obj || !Shown(obj)) continue;
            Wrapper region;
            child_fn(region.b, item.b, cursor_name, 0);
            std::uintptr_t target = region.Display();
            Rect w{};
            bool in = target ? Hit(target, sx, sy, &w) : Hit(obj, sx, sy, &w);
            for (const char* part : {"Widgets/ComboBox", "Widgets/Slider"}) {
                if (in) break;
                Wrapper widget;
                child_fn(widget.b, item.b, part, 0);
                const std::uintptr_t wobj = widget.Display();
                Rect ww{};
                if (wobj && Shown(wobj) && Hit(wobj, sx, sy, &ww)) {
                    in = true;
                    w = ww;
                }
            }
            if (in) {
                const float dx = (w.x1 + w.x2) * 0.5f - sx, dy = (w.y1 + w.y2) * 0.5f - sy;
                const float d = dx * dx + dy * dy;
                if (best < 0 || d < best_d) {
                    best = index;
                    best_d = d;
                    best_packed = packed;
                }
            }
        }
    }
    if (packed_out) *packed_out = best_packed;
    return best;
}

std::uint32_t ClickButton(std::uint8_t* c, std::uint64_t packed, float sx, float sy, std::uint32_t confirm_btn) {
    constexpr std::uint32_t kLeft = 0x80u, kRight = 0x20u;
    const auto provider = Field<std::uintptr_t>(c, kProvider);
    std::uintptr_t pvt = 0, make = 0;
    if (!provider || !SafeRead(provider, &pvt, sizeof(pvt)) || !SafeRead(pvt + 0x18, &make, sizeof(make)) ||
        !GuestCode(make)) {
        return confirm_btn;
    }
    Wrapper item;
    reinterpret_cast<MakeItemFn>(make)(item.b, reinterpret_cast<void*>(provider), &packed);
    if (!item.Display()) return confirm_btn;
    const auto child_fn = reinterpret_cast<ChildFn>(GuestAddr(kChild));
    const auto shown_hit = [&](const char* path, Rect* w, float grow) {
        Wrapper part;
        child_fn(part.b, item.b, path, 0);
        const std::uintptr_t obj = part.Display();
        return obj && Shown(obj) && Hit(obj, sx, sy, w, grow);
    };
    Rect w{};
    if (shown_hit("Widgets/Slider/LeftArrow", &w, 14.0f)) return kLeft;
    if (shown_hit("Widgets/Slider/RightArrow", &w, 14.0f)) return kRight;
    if (shown_hit("Widgets/ComboBox", &w, 0.0f)) return sx < (w.x1 + w.x2) * 0.5f ? kLeft : kRight;
    return confirm_btn;
}

void ShowCursor(std::uint8_t* c, std::uint64_t packed) {
    const auto provider = Field<std::uintptr_t>(c, kProvider);
    std::uintptr_t pvt = 0, make = 0;
    if (!provider || !SafeRead(provider, &pvt, sizeof(pvt)) || !SafeRead(pvt + 0x18, &make, sizeof(make)) ||
        !GuestCode(make)) {
        return;
    }
    Wrapper item;
    reinterpret_cast<MakeItemFn>(make)(item.b, reinterpret_cast<void*>(provider), &packed);
    Wrapper cursor;
    reinterpret_cast<ChildFn>(GuestAddr(kChild))(cursor.b, item.b,
                                                 reinterpret_cast<const char*>(GuestAddr(kCursorName)), 0);
    const auto vt = Field<std::uintptr_t>(cursor.b, 0);
    std::uintptr_t set_visible = 0;
    if (cursor.Display() && vt && SafeRead(vt + 8, &set_visible, sizeof(set_visible)) && GuestCode(set_visible)) {
        using SetVisFn = void(GUEST_ABI*)(void*, int);
        reinterpret_cast<SetVisFn>(set_visible)(cursor.b, 1);
    }
}

} // namespace BbMenuPointer
