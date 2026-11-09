// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: which pad is in use, and what its buttons are called. All of it is static inline so the
// C runtime files that include bbport_pad_hook.h can call it directly.
#pragma once

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Sony's USB vendor ID.
#define BB_VENDOR_SONY 0x054c
/// Nintendo's USB vendor ID.
#define BB_VENDOR_NINTENDO 0x057e

/// A PlayStation pad: by SDL's type, by vendor, or by name (DualShock, DualSense, PS3..PS5).
static inline int extra_pad_is_playstation(SDL_Gamepad *g, SDL_GamepadType type) {
    if (type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5) return 1;
    if (g) {
        if (SDL_GetGamepadVendor(g) == BB_VENDOR_SONY) return 1;
        const char *name = SDL_GetGamepadName(g);
        if (name && (SDL_strcasestr(name, "Sony") || SDL_strcasestr(name, "PlayStation") ||
                     SDL_strcasestr(name, "DualShock") || SDL_strcasestr(name, "DualSense") ||
                     SDL_strcasestr(name, "PS4") || SDL_strcasestr(name, "PS5") || SDL_strcasestr(name, "PS3"))) return 1;
    }
    return 0;
}

/// A Nintendo pad: by SDL's type, by vendor, or by name (Switch, Joy-Con, GameCube, SNES).
static inline int extra_pad_is_nintendo(SDL_Gamepad *g, SDL_GamepadType type) {
    if (type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
        type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT || type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR ||
        type == SDL_GAMEPAD_TYPE_GAMECUBE) return 1;
    if (g) {
        if (SDL_GetGamepadVendor(g) == BB_VENDOR_NINTENDO) return 1;
        const char *name = SDL_GetGamepadName(g);
        if (name && (SDL_strcasestr(name, "Nintendo") || SDL_strcasestr(name, "Switch") ||
                     SDL_strcasestr(name, "Joy-Con") || SDL_strcasestr(name, "Joycon") ||
                     SDL_strcasestr(name, "Pro Controller") || SDL_strcasestr(name, "GameCube") || SDL_strcasestr(name, "SNES"))) return 1;
    }
    return 0;
}

// The pad layouts the settings can force.
enum ExtraPadLayout {
    EXTRA_PAD_LAYOUT_AUTO = 0,      ///< follow the pad that was used last
    EXTRA_PAD_LAYOUT_PLAYSTATION = 1,
    EXTRA_PAD_LAYOUT_XBOX = 2,
    EXTRA_PAD_LAYOUT_NINTENDO = 3,
    EXTRA_PAD_LAYOUT_KEYBOARD = 4,
};

// The devices a prompt can be drawn for.
enum ExtraPadDevice {
    EXTRA_PAD_DEVICE_PLAYSTATION = 0,
    EXTRA_PAD_DEVICE_GAMEPAD = 1,
    EXTRA_PAD_DEVICE_KEYBOARD = 2,
};

/// Whether the Nintendo layout is in use: forced by the setting, or the pad's own type.
static inline int extra_pad_check_nintendo_active(int cur_layout, SDL_GamepadType prompt_type) {
    if (cur_layout == EXTRA_PAD_LAYOUT_NINTENDO) return 1;
    if (cur_layout != EXTRA_PAD_LAYOUT_AUTO) return 0;
    return prompt_type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO ||
           prompt_type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
           prompt_type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT ||
           prompt_type == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR ||
           prompt_type == SDL_GAMEPAD_TYPE_GAMECUBE;
}

/// Fills in the pad's real type (SDL reports generic ones as standard) and the device to show.
static inline void extra_pad_process_prompt_type(SDL_Gamepad *g, int *device, SDL_GamepadType *type, int pad_active, int keyboard_active) {
    const int playstation = extra_pad_is_playstation(g, *type);
    const int nintendo = extra_pad_is_nintendo(g, *type);

    if (nintendo && (*type == SDL_GAMEPAD_TYPE_STANDARD || *type == SDL_GAMEPAD_TYPE_UNKNOWN)) {
        *type = SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
    } else if (playstation && (*type == SDL_GAMEPAD_TYPE_STANDARD || *type == SDL_GAMEPAD_TYPE_UNKNOWN)) {
        *type = SDL_GAMEPAD_TYPE_PS4;
    }

    if (keyboard_active) {
        *device = EXTRA_PAD_DEVICE_KEYBOARD;
    } else if (g && (pad_active || *device < 0)) {
        *device = playstation ? EXTRA_PAD_DEVICE_PLAYSTATION : EXTRA_PAD_DEVICE_GAMEPAD;
    } else if (*device < 0) {
        *device = EXTRA_PAD_DEVICE_KEYBOARD;
    }
}

/// The device a forced layout asks for, or `default_device` when the layout is auto.
static inline int extra_pad_override_device(int cur_layout, int default_device) {
    switch (cur_layout) {
    case EXTRA_PAD_LAYOUT_PLAYSTATION: return EXTRA_PAD_DEVICE_PLAYSTATION;
    case EXTRA_PAD_LAYOUT_XBOX:
    case EXTRA_PAD_LAYOUT_NINTENDO: return EXTRA_PAD_DEVICE_GAMEPAD;
    case EXTRA_PAD_LAYOUT_KEYBOARD: return EXTRA_PAD_DEVICE_KEYBOARD;
    default: return default_device;
    }
}

/// The SDL type a forced layout stands for, or `default_type` when the layout is auto.
static inline SDL_GamepadType extra_pad_override_type(int cur_layout, SDL_GamepadType default_type) {
    switch (cur_layout) {
    case EXTRA_PAD_LAYOUT_NINTENDO: return SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
    case EXTRA_PAD_LAYOUT_XBOX: return SDL_GAMEPAD_TYPE_XBOX360;
    case EXTRA_PAD_LAYOUT_PLAYSTATION: return SDL_GAMEPAD_TYPE_PS4;
    default: return default_type;
    }
}

/// The label of a face button: A/B/X/Y, swapped for the Nintendo layout (B is south there).
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

#ifdef __cplusplus
}
#endif
