// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int extra_pad_is_playstation(SDL_Gamepad *g, SDL_GamepadType type) {
    if (type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5) return 1;
    if (g) {
        if (SDL_GetGamepadVendor(g) == 0x054c) return 1; /* Sony Interactive Entertainment */
        const char *name = SDL_GetGamepadName(g);
        if (name && (strcasestr(name, "Sony") || strcasestr(name, "PlayStation") || 
                     strcasestr(name, "DualShock") || strcasestr(name, "DualSense") ||
                     strcasestr(name, "PS4") || strcasestr(name, "PS5") || strcasestr(name, "PS3"))) return 1;
    }
    return 0;
}

static inline int extra_pad_is_nintendo(SDL_Gamepad *g, SDL_GamepadType type) {
    if (type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
        type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR ||
        type == SDL_GAMEPAD_TYPE_GAMECUBE) return 1;
    if (g) {
        if (SDL_GetGamepadVendor(g) == 0x057e) return 1; /* Nintendo Co., Ltd. */
        const char *name = SDL_GetGamepadName(g);
        if (name && (strcasestr(name, "Nintendo") || strcasestr(name, "Switch") || 
                     strcasestr(name, "Joy-Con") || strcasestr(name, "Joycon") ||
                     strcasestr(name, "Pro Controller") || strcasestr(name, "GameCube") || strcasestr(name, "SNES"))) return 1;
    }
    return 0;
}

static inline int extra_pad_check_nintendo_active(int cur_layout, SDL_GamepadType prompt_type) {
    if (cur_layout == 3) return 1; /* Forced Nintendo */
    if (cur_layout != 0) return 0; /* Other forced layout */
    return prompt_type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO ||
           prompt_type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
           prompt_type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT ||
           prompt_type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR ||
           prompt_type == SDL_GAMEPAD_TYPE_GAMECUBE;
}

static inline void extra_pad_process_prompt_type(SDL_Gamepad *g, int *device, SDL_GamepadType *type, int pad_active, int keyboard_active) {
    const int playstation = extra_pad_is_playstation(g, *type);
    const int nintendo = extra_pad_is_nintendo(g, *type);

    if (nintendo && (*type == SDL_GAMEPAD_TYPE_STANDARD || *type == SDL_GAMEPAD_TYPE_UNKNOWN)) {
        *type = SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
    } else if (playstation && (*type == SDL_GAMEPAD_TYPE_STANDARD || *type == SDL_GAMEPAD_TYPE_UNKNOWN)) {
        *type = SDL_GAMEPAD_TYPE_PS4;
    }

    if (keyboard_active) {
        *device = 2;
    } else if (g && (pad_active || *device < 0)) {
        *device = playstation ? 0 : 1;
    } else if (*device < 0) {
        *device = 2;
    }
}

static inline int extra_pad_override_device(int cur_layout, int default_device) {
    if (cur_layout == 1) return 0; /* PlayStation */
    if (cur_layout == 2 || cur_layout == 3) return 1; /* Xbox or Nintendo */
    if (cur_layout == 4) return 2; /* Keyboard and mouse */
    return default_device;
}

static inline SDL_GamepadType extra_pad_override_type(int cur_layout, SDL_GamepadType default_type) {
    if (cur_layout == 3) return SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
    if (cur_layout == 2) return SDL_GAMEPAD_TYPE_XBOX360;
    if (cur_layout == 1) return SDL_GAMEPAD_TYPE_PS4;
    return default_type;
}

static inline int extra_pad_resolve_button(int button, int is_nintendo, char *label, int size) {
    const char *text = NULL;
    if (is_nintendo) {
        if (button == SDL_GAMEPAD_BUTTON_SOUTH) text = "B";
        else if (button == SDL_GAMEPAD_BUTTON_EAST) text = "A";
        else if (button == SDL_GAMEPAD_BUTTON_WEST) text = "Y";
        else if (button == SDL_GAMEPAD_BUTTON_NORTH) text = "X";
        else return 0;
    } else {
        if (button == SDL_GAMEPAD_BUTTON_SOUTH) text = "A";
        else if (button == SDL_GAMEPAD_BUTTON_EAST) text = "B";
        else if (button == SDL_GAMEPAD_BUTTON_WEST) text = "X";
        else if (button == SDL_GAMEPAD_BUTTON_NORTH) text = "Y";
        else return 0;
    }
    snprintf(label, size, "%s", text);
    return 1;
}

// Synthetic touch structures and capacitive touch linger simulation
typedef struct ExtraPadTouch {
    uint16_t x, y;
    uint8_t id, reserve[3];
} ExtraPadTouch;

typedef struct ExtraSyntheticTouchState {
    int side;              /* 0 = left, 1 = right, -1 = none */
    uint8_t touch_id;      /* tracking ID (1..127) */
    uint64_t linger_until; /* monotonic timestamp (us) for capacitive touch linger */
} ExtraSyntheticTouchState;

static ExtraSyntheticTouchState g_extra_synthetic_touch = {-1, 1, 0};

static inline void extra_pad_reset_synthetic_touch(void) {
    g_extra_synthetic_touch.side = -1;
    g_extra_synthetic_touch.linger_until = 0;
}

static inline void extra_pad_touch_click(void *touches_ptr, uint8_t *touch_count, uint32_t *buttons, int right) {
    ExtraPadTouch *touches = (ExtraPadTouch *)touches_ptr;
    *buttons |= 0x100000; /* BTN_TOUCHPAD */
    const uint16_t x = right ? 1440 : 480;
    for (int i = 0; i < *touch_count; ++i) {
        if (touches[i].x == x) return;
    }
    if (*touch_count < 2) {
        const int idx = (*touch_count)++;
        touches[idx].x = x;
        touches[idx].y = 471;
        touches[idx].id = g_extra_synthetic_touch.touch_id;
    }
}

static inline void extra_pad_update_synthetic_touch(void *touches_ptr, uint8_t *touch_count, uint32_t *buttons, int req_left, int req_right, uint64_t now) {
    ExtraPadTouch *touches = (ExtraPadTouch *)touches_ptr;
    if (req_right || req_left) {
        const int side = req_right ? 1 : 0;
        if (g_extra_synthetic_touch.side != side || now >= g_extra_synthetic_touch.linger_until) {
            g_extra_synthetic_touch.touch_id = (uint8_t)((g_extra_synthetic_touch.touch_id % 127) + 1);
        }
        g_extra_synthetic_touch.side = side;
        g_extra_synthetic_touch.linger_until = now + 150000; /* 150ms capacitive touch linger */
        *buttons |= 0x100000; /* BTN_TOUCHPAD */
        if (*touch_count == 0) {
            *touch_count = 1;
            touches[0].x = side ? 1440 : 480;
            touches[0].y = 471;
            touches[0].id = g_extra_synthetic_touch.touch_id;
        }
    } else if (g_extra_synthetic_touch.side >= 0 && now < g_extra_synthetic_touch.linger_until) {
        *buttons &= ~0x100000; /* BTN_TOUCHPAD */
        if (*touch_count == 0) {
            *touch_count = 1;
            touches[0].x = g_extra_synthetic_touch.side ? 1440 : 480;
            touches[0].y = 471;
            touches[0].id = g_extra_synthetic_touch.touch_id;
        }
    } else {
        g_extra_synthetic_touch.side = -1;
    }
}

static inline void extra_pad_process_touchpad(SDL_Gamepad *g, void *touches_ptr, uint8_t *touch_count,
                                              uint32_t *buttons, int *req_left, int *req_right, uint64_t now) {
    (void)buttons;
    (void)now;
    if (!g) return;
    ExtraPadTouch *touches = (ExtraPadTouch *)touches_ptr;

    if (SDL_GetNumGamepadTouchpads(g) > 0) {
        const int fingers = SDL_GetNumGamepadTouchpadFingers(g, 0);
        for (int finger = 0; finger < fingers && *touch_count < 2; ++finger) {
            bool down = false;
            float x = 0.0f, y = 0.0f;
            if (SDL_GetGamepadTouchpadFinger(g, 0, finger, &down, &x, &y, NULL) && down) {
                uint16_t px = (uint16_t)(x <= 0.0f ? 0 : x >= 1.0f ? 1919 : (int)(x * 1919.0f + 0.5f));
                uint16_t py = (uint16_t)(y <= 0.0f ? 0 : y >= 1.0f ? 942 : (int)(y * 942.0f + 0.5f));
                int idx = (*touch_count)++;
                touches[idx].x = px;
                touches[idx].y = py;
                touches[idx].id = (uint8_t)finger;
            }
        }
    }

    if (SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_TOUCHPAD)) {
        int has_left = 0, has_right = 0;
        for (int i = 0; i < *touch_count; ++i) {
            if (touches[i].x >= 960) has_right = 1;
            else has_left = 1;
        }
        if (has_right && !has_left) {
            *req_right = 1;
            *req_left = 0;
        } else {
            *req_left = 1;
            *req_right = 0;
        }
    }
}

__attribute__((weak)) int bbgpu_get_disable_gyro(void);

static inline int extra_pad_is_gyro_disabled(void) {
    if (bbgpu_get_disable_gyro) return bbgpu_get_disable_gyro();
    const char *env = getenv("BB_DISABLE_GYRO");
    return env && *env && *env != '0';
}

static inline void extra_pad_read_sensors(SDL_Gamepad *g, float acceleration[3], float angular_velocity[3]) {
    if (!g) return;
    if (extra_pad_is_gyro_disabled()) {
        acceleration[0] = 0.0f; acceleration[1] = 1.0f; acceleration[2] = 0.0f;
        angular_velocity[0] = angular_velocity[1] = angular_velocity[2] = 0.0f;
        return;
    }
    if (SDL_GamepadSensorEnabled(g, SDL_SENSOR_ACCEL)) {
        float accel[3] = {0};
        if (SDL_GetGamepadSensorData(g, SDL_SENSOR_ACCEL, accel, 3)) {
            acceleration[0] = accel[0] / 9.80665f;
            acceleration[1] = accel[1] / 9.80665f;
            acceleration[2] = accel[2] / 9.80665f;
        }
    }
    if (SDL_GamepadSensorEnabled(g, SDL_SENSOR_GYRO)) {
        float gyro[3] = {0};
        if (SDL_GetGamepadSensorData(g, SDL_SENSOR_GYRO, gyro, 3)) {
            angular_velocity[0] = gyro[0] * (180.0f / 3.141592653589793f);
            angular_velocity[1] = gyro[1] * (180.0f / 3.141592653589793f);
            angular_velocity[2] = gyro[2] * (180.0f / 3.141592653589793f);
        }
    }
}

/* Hold Walk modifier: holding Left/Right Alt or Left/Right Ctrl with movement keys
 * sets half-analog deflection so the character walks smoothly instead of running. */
static inline void extra_pad_apply_walk_modifier(uint8_t *left_x, uint8_t *left_y, const bool *k,
                                                 int move_left, int move_right, int move_up, int move_down) {
    if (!k || (!move_left && !move_right && !move_up && !move_down)) return;
    const int walk = k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT] ||
                     k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL];
    if (!walk) return;

    const int move_x = move_left || move_right;
    const int move_y = move_up || move_down;
    const int scale = (move_x && move_y) ? 42 : 60;

    if (move_x) {
        *left_x = (uint8_t)(128 + (move_right ? scale : -scale));
    }
    if (move_y) {
        *left_y = (uint8_t)(128 + (move_down ? scale : -scale));
    }
}

__attribute__((weak)) int bbgpu_input_blocked(void);
__attribute__((weak)) int bbgpu_video_skip(void);

/* Cutscene skip helper: if gameplay input is blocked (cutscene or pre-rendered movie playing),
 * pressing Enter or Esc emits BTN_OPTIONS (0x8) and triggers video skip if an AvPlayer is active. */
static inline void extra_pad_apply_cutscene_skip(uint32_t *buttons, const bool *k) {
    if (!k) return;
    const int blocked = bbgpu_input_blocked ? bbgpu_input_blocked() : 0;
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER] || k[SDL_SCANCODE_ESCAPE]) {
        if (bbgpu_video_skip && bbgpu_video_skip()) {
            *buttons &= ~0x2000; /* BTN_CIRCLE */
            *buttons |= 0x8;    /* BTN_OPTIONS */
        } else if (blocked) {
            *buttons &= ~0x2000; /* BTN_CIRCLE */
            *buttons |= 0x8;    /* BTN_OPTIONS */
        }
    }
}

#ifdef __cplusplus
}
#endif
