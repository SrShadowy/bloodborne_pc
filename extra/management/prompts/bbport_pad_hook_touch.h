// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the touchpad the guest sees. SDL reports touches in 0..1; the guest wants the PS4's
// 1920x943 panel, and a click at the touch's side. Linux builds read the raw evdev node of a
// DualShock/DualSense whose touchpad SDL does not expose (bbport_pad_hook_touch_evdev.h).
#pragma once

#include <stdint.h>

#include <SDL3/SDL.h>

#include "bbport_pad_hook_identity.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The guest's touch panel: 1920 wide, 943 tall (the PS4's 1920x1080 with the black bar cropped).
#define BB_TOUCHPAD_WIDTH 1920
#define BB_TOUCHPAD_HEIGHT 943
/// A touch in the right half of the panel (the panel's middle is 960).
#define BB_TOUCHPAD_RIGHT_X 1440
#define BB_TOUCHPAD_LEFT_X 480
/// Where a synthetic click touches: the panel's vertical middle.
#define BB_TOUCHPAD_CLICK_Y 471
/// The guest's BTN_TOUCHPAD bit in its button word.
#define BB_BUTTON_TOUCHPAD 0x100000
/// How long a lifted finger keeps the synthetic click alive (capacitive touch).
#define BB_TOUCH_LINGER_US 150000
/// SDL's finger indices only reach this; the guest wants 1..127.
#define BB_TOUCH_ID_MAX 127

/// The four bytes of a guest touch: its panel position and its tracking id.
typedef struct ExtraPadTouch {
    uint16_t x, y;
    uint8_t id, reserve[3];
} ExtraPadTouch;

/// The synthetic touch a click produces (as opposed to a real finger on the panel).
typedef struct ExtraSyntheticTouchState {
    int side;              ///< 0 = left, 1 = right, -1 = none
    uint8_t touch_id;      ///< tracking id (1..127)
    uint64_t linger_until; ///< monotonic timestamp (us) the touch may be kept until
} ExtraSyntheticTouchState;

static ExtraSyntheticTouchState g_extra_synthetic_touch = {-1, 1, 0};

/// Forgets any synthetic click (the pad changed, the game left the menu).
static inline void extra_pad_reset_synthetic_touch(void) {
    g_extra_synthetic_touch.side = -1;
    g_extra_synthetic_touch.linger_until = 0;
}

/// Adds the touch a touchpad click produces at `right`'s side, when there is not one already.
static inline void extra_pad_touch_click(void *touches_ptr, uint8_t *touch_count, uint32_t *buttons, int right) {
    ExtraPadTouch *touches = (ExtraPadTouch *)touches_ptr;
    *buttons |= BB_BUTTON_TOUCHPAD;
    const uint16_t x = right ? BB_TOUCHPAD_RIGHT_X : BB_TOUCHPAD_LEFT_X;
    for (int i = 0; i < *touch_count; ++i) {
        if (touches[i].x == x) return;
    }
    if (*touch_count < 2) {
        const int idx = (*touch_count)++;
        touches[idx].x = x;
        touches[idx].y = BB_TOUCHPAD_CLICK_Y;
        touches[idx].id = g_extra_synthetic_touch.touch_id;
    }
}

/// Keeps a click's touch alive while the click repeats, and lets it linger briefly after it ends
/// (the guest samples the panel at its own rate, so a one-frame click would often be missed).
static inline void extra_pad_update_synthetic_touch(void *touches_ptr, uint8_t *touch_count, uint32_t *buttons, int req_left, int req_right, uint64_t now) {
    ExtraPadTouch *touches = (ExtraPadTouch *)touches_ptr;
    if (req_right || req_left) {
        const int side = req_right ? 1 : 0;
        if (g_extra_synthetic_touch.side != side || now >= g_extra_synthetic_touch.linger_until) {
            g_extra_synthetic_touch.touch_id = (uint8_t)((g_extra_synthetic_touch.touch_id % BB_TOUCH_ID_MAX) + 1);
        }
        g_extra_synthetic_touch.side = side;
        g_extra_synthetic_touch.linger_until = now + BB_TOUCH_LINGER_US;
        *buttons |= BB_BUTTON_TOUCHPAD;
        if (*touch_count == 0) {
            *touch_count = 1;
            touches[0].x = side ? BB_TOUCHPAD_RIGHT_X : BB_TOUCHPAD_LEFT_X;
            touches[0].y = BB_TOUCHPAD_CLICK_Y;
            touches[0].id = g_extra_synthetic_touch.touch_id;
        }
    } else if (g_extra_synthetic_touch.side >= 0 && now < g_extra_synthetic_touch.linger_until) {
        *buttons &= ~BB_BUTTON_TOUCHPAD;
        if (*touch_count == 0) {
            *touch_count = 1;
            touches[0].x = g_extra_synthetic_touch.side ? BB_TOUCHPAD_RIGHT_X : BB_TOUCHPAD_LEFT_X;
            touches[0].y = BB_TOUCHPAD_CLICK_Y;
            touches[0].id = g_extra_synthetic_touch.touch_id;
        }
    } else {
        g_extra_synthetic_touch.side = -1;
    }
}

/// One click's side, from the touches on the panel, or from the first finger when there are none.
static inline void extra_pad_click_side(const ExtraPadTouch *touches, uint8_t touch_count, uint16_t first_x, int *req_left, int *req_right) {
    int has_left = 0, has_right = 0;
    for (int i = 0; i < touch_count; ++i) {
        if (touches[i].x >= BB_TOUCHPAD_WIDTH / 2) has_right = 1;
        else has_left = 1;
    }
    if (!has_left && !has_right) {
        if (first_x >= BB_TOUCHPAD_WIDTH / 2) has_right = 1;
        else has_left = 1;
    }
    *req_right = has_right && !has_left;
    *req_left = !*req_right;
}

#ifdef __linux__
// Reads the raw node when SDL has no touchpad API for the pad; needs the panel state above.
#include "bbport_pad_hook_touch_evdev.h"
#endif

/// Adds the pad's real touches and its click to the guest's panel state.
static inline void extra_pad_process_touchpad(SDL_Gamepad *g, void *touches_ptr, uint8_t *touch_count,
                                              uint32_t *buttons, int *req_left, int *req_right, uint64_t now) {
    if (!g) return;
    ExtraPadTouch *touches = (ExtraPadTouch *)touches_ptr;

    if (SDL_GetNumGamepadTouchpads(g) > 0) {
        const int fingers = SDL_GetNumGamepadTouchpadFingers(g, 0);
        for (int finger = 0; finger < fingers && *touch_count < 2; ++finger) {
            bool down = false;
            float x = 0.0f, y = 0.0f;
            if (SDL_GetGamepadTouchpadFinger(g, 0, finger, &down, &x, &y, NULL) && down) {
                uint16_t px = (uint16_t)(x <= 0.0f ? 0 : x >= 1.0f ? BB_TOUCHPAD_WIDTH - 1 : (int)(x * (BB_TOUCHPAD_WIDTH - 1) + 0.5f));
                uint16_t py = (uint16_t)(y <= 0.0f ? 0 : y >= 1.0f ? BB_TOUCHPAD_HEIGHT - 1 : (int)(y * (BB_TOUCHPAD_HEIGHT - 1) + 0.5f));
                int idx = (*touch_count)++;
                touches[idx].x = px;
                touches[idx].y = py;
                touches[idx].id = (uint8_t)finger;
            }
        }
    }
#ifdef __linux__
    else if (extra_pad_is_playstation(g, SDL_GetGamepadType(g))) {
        extra_pad_poll_evdev_touchpad(touches_ptr, touch_count, buttons, req_left, req_right, now);
    }
#else
    (void)now; // only the evdev path below needs a timestamp
#endif

    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_TOUCHPAD)) {
        *buttons |= BB_BUTTON_TOUCHPAD;
        extra_pad_click_side(touches, *touch_count, 0, req_left, req_right);
    }
}

/// Whether the pad's touchpad is the port's own (not SDL's), so the launcher can say so.
static int g_extra_pad_custom_touchpad = 0;
static inline void extra_pad_set_has_custom_touchpad(int custom) {
    g_extra_pad_custom_touchpad = custom;
}
static inline int extra_pad_has_custom_touchpad(void) {
    return g_extra_pad_custom_touchpad;
}

#ifdef __cplusplus
}
#endif
