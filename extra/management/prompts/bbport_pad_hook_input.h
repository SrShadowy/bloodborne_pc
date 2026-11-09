// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the input conveniences layered on the pad -- motion sensors (gyro), the hold-to-walk
// modifier and the cutscene skip. Static inline, like the rest of bbport_pad_hook.h.
#pragma once

#include <stdint.h>
#include <stdlib.h>

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/// What the guest's button word calls "Options" (the start button).
#define BB_BUTTON_OPTIONS 0x8
/// What the guest's button word calls "Circle" (the cancel/back button).
#define BB_BUTTON_CIRCLE 0x2000

/// The standard gravity the accelerometer's reading is divided by, in m/s^2.
#define BB_GRAVITY 9.80665f
/// Radians to degrees, for the gyro's reading.
#define BB_RAD_TO_DEG 57.29577951308232f

// Implemented by the GPU side; absent when it is not linked in (weak, so the check is safe).
__attribute__((weak)) int bbgpu_get_disable_gyro(void);

/// Whether motion input is off: the setting, else BB_DISABLE_GYRO.
static inline int extra_pad_is_gyro_disabled(void) {
    if (bbgpu_get_disable_gyro) return bbgpu_get_disable_gyro();
    const char *env = getenv("BB_DISABLE_GYRO");
    return env && *env && *env != '0';
}

/// The pad's acceleration (in g) and angular velocity (in deg/s); still when motion is disabled.
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
            acceleration[0] = accel[0] / BB_GRAVITY;
            acceleration[1] = accel[1] / BB_GRAVITY;
            acceleration[2] = accel[2] / BB_GRAVITY;
        }
    }
    if (SDL_GamepadSensorEnabled(g, SDL_SENSOR_GYRO)) {
        float gyro[3] = {0};
        if (SDL_GetGamepadSensorData(g, SDL_SENSOR_GYRO, gyro, 3)) {
            angular_velocity[0] = gyro[0] * BB_RAD_TO_DEG;
            angular_velocity[1] = gyro[1] * BB_RAD_TO_DEG;
            angular_velocity[2] = gyro[2] * BB_RAD_TO_DEG;
        }
    }
}

/// The stick deflection a held walk modifier uses, and a plain one.
#define BB_WALK_DEFLECTION 60
#define BB_WALK_DEFLECTION_DIAGONAL 42
/// The stick's neutral value (128 is centred in 0..255).
#define BB_STICK_CENTRE 128

/// Holding Left/Right Alt or Left/Right Ctrl while moving makes the stick deflect half-way, so the
/// character walks instead of running.
static inline void extra_pad_apply_walk_modifier(uint8_t *left_x, uint8_t *left_y, const bool *k,
                                                 int move_left, int move_right, int move_up, int move_down) {
    if (!k || (!move_left && !move_right && !move_up && !move_down)) return;
    const int walk = k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT] ||
                     k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL];
    if (!walk) return;

    const int move_x = move_left || move_right;
    const int move_y = move_up || move_down;
    const int scale = (move_x && move_y) ? BB_WALK_DEFLECTION_DIAGONAL : BB_WALK_DEFLECTION;

    if (move_x) {
        *left_x = (uint8_t)(BB_STICK_CENTRE + (move_right ? scale : -scale));
    }
    if (move_y) {
        *left_y = (uint8_t)(BB_STICK_CENTRE + (move_down ? scale : -scale));
    }
}

// Implemented by the GPU side; absent when it is not linked in.
__attribute__((weak)) int bbgpu_input_blocked(void);
__attribute__((weak)) int bbgpu_video_skip(void);

/// Enter or Esc skips a cutscene or a pre-rendered movie: while gameplay input is blocked (or an
/// AvPlayer is playing) it sends Options instead of Circle.
static inline void extra_pad_apply_cutscene_skip(uint32_t *buttons, const bool *k) {
    if (!k) return;
    const int blocked = bbgpu_input_blocked ? bbgpu_input_blocked() : 0;
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER] || k[SDL_SCANCODE_ESCAPE]) {
        const int skip_video = bbgpu_video_skip && bbgpu_video_skip();
        if (skip_video || blocked) {
            *buttons &= ~BB_BUTTON_CIRCLE;
            *buttons |= BB_BUTTON_OPTIONS;
        }
    }
}

#ifdef __cplusplus
}
#endif
