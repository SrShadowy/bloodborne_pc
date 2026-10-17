// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_menu_pointer.h"

#include "bbport_overlay.h"
#include "bbport_scaleform_hit.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define GUEST_ABI __attribute__((sysv_abi))
#else
#define GUEST_ABI
#endif

namespace BbMenuPointer {
namespace {

constexpr uintptr_t kListUpdate = 0x1ecff30, kListVTable = 0x5799fb0, kPad = 0x1ec1e02;
constexpr size_t kListUpdateSlot = 3;
constexpr uint8_t kPadBytes[] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x2e, 0x0f,
                                 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};
struct Site { uintptr_t at; uint8_t op; };
constexpr Site kListUpdateCalls[] = {
    {0x1f25226, 0xe8}, {0x1f26066, 0xe8}, {0x1f26f28, 0xe8}, {0x1f72c80, 0xe8}, {0x1f108c7, 0xe9}};

constexpr uintptr_t kSetCursor = 0x1ecf6b0, kScrollBy = 0x1ed0dd0, kHideCursors = 0x1ecf0e0;
constexpr uintptr_t kMenuFlow = 0x5980e38, kRegion = 0x5528948, kInputBlockB = 0x5962878;
constexpr size_t kFlowStarted = 0xb11, kFlowMenu = 0xa88;
constexpr uintptr_t kMenuSe = 0x597fc60, kMenuSeLabel = 0x597fc68, kCursorMoveLabel = 0x4d344e4;

bool g_has_base = false;
std::atomic<int64_t> g_focus_ms{0}, g_click_ms{0};
std::atomic<bool> g_click{false}, g_left_down{false}, g_in_window{false}, g_dir_held{false};
std::atomic<uint32_t> g_press{0}, g_PendingRightAndWheel{0};
std::atomic<float> g_stage_x{0.0f}, g_stage_y{0.0f};

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool InputBlocked() {
    uintptr_t a = 0, b = 0;
    if (SafeRead(GuestAddr(kMenuFlow), &a, sizeof(a)) && a) {
        uint64_t v = 0;
        if (SafeRead(a + 0xa78, &v, sizeof(v)) && v) return true;
    }
    if (SafeRead(GuestAddr(kInputBlockB), &b, sizeof(b)) && b) {
        uint32_t v = 0;
        if (SafeRead(b + 0x2e4, &v, sizeof(v)) && v) return true;
    }
    return false;
}

bool FrontEnd() {
    if (!g_has_base) return false;
    uintptr_t flow = 0;
    uint8_t started = 1;
    return SafeRead(GuestAddr(kMenuFlow), &flow, sizeof(flow)) && flow &&
           SafeRead(flow + kFlowStarted, &started, sizeof(started)) && !started;
}

bool FlowMenuOpen() {
    if (!g_has_base) return false;
    uintptr_t flow = 0, menu = 0;
    return SafeRead(GuestAddr(kMenuFlow), &flow, sizeof(flow)) && flow &&
           SafeRead(flow + kFlowMenu, &menu, sizeof(menu)) && menu;
}

struct Seen {
    uint8_t* comp = nullptr;
    float x = 0.0f, y = 0.0f;
    int64_t ms = 0;
};
Seen g_seen[32];
int g_seen_next = 0;

Seen& GetSeen(uint8_t* c, bool* fresh) {
    const int64_t now = NowMs();
    for (Seen& s : g_seen) {
        if (s.comp == c) {
            *fresh = now - s.ms > 250;
            s.ms = now;
            return s;
        }
    }
    Seen& s = g_seen[g_seen_next++ % 32];
    s = Seen{c, 0.0f, 0.0f, now};
    *fresh = true;
    return s;
}

void NotifyCursorChanged(uint8_t* c, int now_idx) {
    const auto listener = Field<uintptr_t>(c, kListener);
    uintptr_t lvt = 0, call = 0;
    if (listener && SafeRead(listener, &lvt, sizeof(lvt)) && SafeRead(lvt + 0x10, &call, sizeof(call)) &&
        GuestCode(call)) {
        int32_t index = now_idx;
        using ListenerFn = void(GUEST_ABI*)(void*, int32_t*);
        reinterpret_cast<ListenerFn>(call)(reinterpret_cast<void*>(listener), &index);
    }
    auto* se = reinterpret_cast<int32_t*>(GuestAddr(kMenuSe));
    if (se[1] <= 0) {
        se[0] = 0;
        se[1] = 0;
        *reinterpret_cast<uintptr_t*>(GuestAddr(kMenuSeLabel)) = GuestAddr(kCursorMoveLabel);
    }
}

void PointerStep(uint8_t* c, const uint8_t* input) {
    if (!input || !*input || InputBlocked()) return;
    g_focus_ms.store(NowMs(), std::memory_order_relaxed);
    if (BbOverlay::CapturesInput() || !g_in_window.load(std::memory_order_relaxed)) return;

    const float sx = g_stage_x.load(std::memory_order_relaxed);
    const float sy = g_stage_y.load(std::memory_order_relaxed);
    bool fresh = false;
    Seen& s = GetSeen(c, &fresh);
    const bool moved = !fresh && (sx != s.x || sy != s.y);
    s.x = sx;
    s.y = sy;

    bool click = false;
    if (g_click.load(std::memory_order_relaxed)) {
        if (NowMs() - g_click_ms.load(std::memory_order_relaxed) > 250) {
            g_click.store(false, std::memory_order_relaxed);
        } else {
            click = true;
        }
    }

    if (Field<int32_t>(c, kLines) == 1 && Field<int32_t>(c, kLineLen) == 1 && Field<int32_t>(c, kCount) > 1) {
        if (!click) return;
        uintptr_t root = 0;
        const auto data = Field<uintptr_t>(c, kRootData);
        if ((Field<uint32_t>(c, kRootType) & 0x8f) != 10 || !data || !SafeRead(data + kNative, &root, sizeof(root)))
            return;
        Rect w{};
        if (root && Shown(root) && Hit(root, sx, sy, &w) && g_click.exchange(false)) {
            g_press.store(sx < (w.x1 + w.x2) * 0.5f ? 0x80u : 0x20u, std::memory_order_relaxed);
        }
        return;
    }

    if ((!moved && !click) || g_dir_held.load(std::memory_order_relaxed)) return;
    uint64_t packed = 0;
    const int want = ItemAt(c, sx, sy, &packed);
    if (want < 0) return;
    if (click) g_click.store(false, std::memory_order_relaxed);

    const int was = CursorOf(c);
    int now = was;
    if (want != was) {
        const int s1 = Field<int32_t>(c, kLineScroll), s2 = Field<int32_t>(c, kScroll);
        using SetCursorFn = int32_t(GUEST_ABI*)(uint8_t*, int);
        const int r = reinterpret_cast<SetCursorFn>(GuestAddr(kSetCursor))(c, want);
        const int d1 = s1 - Field<int32_t>(c, kLineScroll), d2 = s2 - Field<int32_t>(c, kScroll);
        if (r == 1 && (d1 || d2)) {
            using ScrollByFn = void(GUEST_ABI*)(uint8_t*, int, int);
            using HideCursorsFn = void(GUEST_ABI*)(uint8_t*);
            reinterpret_cast<ScrollByFn>(GuestAddr(kScrollBy))(c, d1, d2);
            reinterpret_cast<HideCursorsFn>(GuestAddr(kHideCursors))(c);
            ShowCursor(c, packed);
        }
        now = CursorOf(c);
    }
    if (click && now == want) {
        g_press.store(ClickButton(c, packed, sx, sy, ConfirmButton()), std::memory_order_relaxed);
    }
    if (now != was) {
        NotifyCursorChanged(c, now);
    }
}

GUEST_ABI void ListUpdateHook(uint8_t* comp, uint8_t* input) {
    PointerStep(comp, input);
    using ListUpdateFn = void(GUEST_ABI*)(uint8_t*, uint8_t*);
    reinterpret_cast<ListUpdateFn>(GuestAddr(kListUpdate))(comp, input);
}

} // namespace

void Install(uintptr_t image_base, uint64_t image_size) {
    if (!image_base || image_size < 0x5600000) return;
    SetScaleformBase(image_base);
    g_has_base = true;
    const uintptr_t slot = GuestAddr(kListVTable) + kListUpdateSlot * 8;
    auto* sp = reinterpret_cast<uint64_t*>(slot);
    auto* pad = reinterpret_cast<uint8_t*>(GuestAddr(kPad));
    if (*sp != GuestAddr(kListUpdate) || std::memcmp(pad, kPadBytes, sizeof(kPadBytes)) != 0) return;

    for (const Site& site : kListUpdateCalls) {
        const auto* cp = reinterpret_cast<const uint8_t*>(GuestAddr(site.at));
        int32_t rel = 0;
        std::memcpy(&rel, cp + 1, sizeof(rel));
        if (cp[0] != site.op || GuestAddr(site.at) + 5 + static_cast<intptr_t>(rel) != GuestAddr(kListUpdate)) return;
    }

    const auto hook = reinterpret_cast<uint64_t>(&ListUpdateHook);
    *sp = hook;
    const uintptr_t pva = GuestAddr(kPad);
    pad[0] = 0x49;
    pad[1] = 0xbb;
    std::memcpy(pad + 2, &hook, 8);
    pad[10] = 0x41;
    pad[11] = 0xff;
    pad[12] = 0xe3;
    for (const Site& site : kListUpdateCalls) {
        const uintptr_t call = GuestAddr(site.at);
        auto* cp = reinterpret_cast<uint8_t*>(call);
        const auto rel = static_cast<int32_t>(static_cast<intptr_t>(pva) - static_cast<intptr_t>(call + 5));
        std::memcpy(cp + 1, &rel, sizeof(rel));
    }
    std::printf("Engine: Scaleform menu mouse pointer installed\n");
}

uint32_t ConfirmButton() {
    int32_t region = -1;
    if (!g_has_base || !SafeRead(GuestAddr(kRegion), &region, sizeof(region))) return 0x4000u;
    return region == 5 ? 0x2000u : 0x4000u;
}

uint32_t BackButton() { return ConfirmButton() == 0x2000u ? 0x4000u : 0x2000u; }

bool InMenu() {
    if (!g_has_base) return false;
    return FrontEnd() || FlowMenuOpen() || (NowMs() - g_focus_ms.load(std::memory_order_relaxed) < 400);
}

void SetMouseStagePos(float sx, float sy, bool in_window) {
    g_stage_x.store(sx, std::memory_order_relaxed);
    g_stage_y.store(sy, std::memory_order_relaxed);
    g_in_window.store(in_window, std::memory_order_relaxed);
}

void NotifyLeftClick(bool down) {
    g_left_down.store(down, std::memory_order_relaxed);
    if (down) {
        g_click_ms.store(NowMs(), std::memory_order_relaxed);
        g_click.store(true, std::memory_order_relaxed);
    }
}

void NotifyRightClick() {
    g_PendingRightAndWheel.fetch_or(BackButton(), std::memory_order_relaxed);
}

void NotifyWheel(float notches) {
    if (notches > 0.0f) g_PendingRightAndWheel.fetch_or(0x10u, std::memory_order_relaxed);
    else if (notches < 0.0f) g_PendingRightAndWheel.fetch_or(0x40u, std::memory_order_relaxed);
}

uint32_t StepPad(uint32_t current_buttons, uint8_t lx, uint8_t ly) {
    const bool dir = (current_buttons & 0xf0u) != 0 || lx < 64 || lx > 192 || ly < 64 || ly > 192;
    g_dir_held.store(dir, std::memory_order_relaxed);
    if (!InMenu() || BbOverlay::CapturesInput()) {
        g_press.store(0, std::memory_order_relaxed);
        g_PendingRightAndWheel.store(0, std::memory_order_relaxed);
        return 0;
    }
    static uint32_t held = 0;
    static int hold_polls = 0;
    static uint32_t repeat = 0;
    static float rx = 0.0f, ry = 0.0f;

    const float sx = g_stage_x.load(std::memory_order_relaxed);
    const float sy = g_stage_y.load(std::memory_order_relaxed);
    const uint32_t pressed_now = g_press.exchange(0, std::memory_order_relaxed);
    const uint32_t extra = g_PendingRightAndWheel.exchange(0, std::memory_order_relaxed);
    const uint32_t want = pressed_now | extra;

    if (pressed_now & 0xa0u) {
        repeat = pressed_now & 0xa0u;
        rx = sx;
        ry = sy;
    }
    if (repeat && (!g_left_down.load(std::memory_order_relaxed) || std::fabs(sx - rx) > 24.0f ||
                   std::fabs(sy - ry) > 24.0f)) {
        repeat = 0;
    }
    if (want) {
        held = want;
        hold_polls = 8;
    }
    uint32_t out = repeat;
    if (hold_polls > 0) {
        --hold_polls;
        out |= held;
    }
    return out;
}

} // namespace BbMenuPointer
