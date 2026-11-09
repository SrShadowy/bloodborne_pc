// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the virtual keyboard's cursor and input -- the selected key, the case toggle, the gamepad
// mapping (d-pad/left stick to move, south to type, west to erase, east to cancel, shoulders to
// change case) and the callbacks the owner installed. bbport_keyboard_render.cpp draws it.
#include "bbport_keyboard_internal.h"

#include <algorithm>

#include <SDL3/SDL.h>

namespace BbVirtualKeyboard {
namespace detail {

namespace {

Callbacks g_callbacks{};
int g_selected_row = 1;
int g_selected_col = 0;
bool g_caps_lock = true;
std::uint64_t g_last_stick_tick = 0;

/// The character rows, upper/lower selected by Caps.
struct RowTable {
    const char* const* upper;
    const char* const* lower;
    int length;
};

constexpr RowTable kRows[kCharRows] = {
    {kRow0, kRow0, 12},
    {kRow1Upper, kRow1Lower, 10},
    {kRow2Upper, kRow2Lower, 10},
    {kRow3Upper, kRow3Lower, 10},
};

} // namespace

Callbacks& GetCallbacks() {
    return g_callbacks;
}

const char* const* RowKeys(int row) {
    if (row < 0 || row >= kCharRows) {
        return nullptr;
    }
    return g_caps_lock ? kRows[row].upper : kRows[row].lower;
}

int RowLength(int row) {
    if (row < 0 || row > kActionRow) {
        return 1;
    }
    return row == kActionRow ? ActCount : kRows[row].length;
}

int& SelectedRow() {
    return g_selected_row;
}

int& SelectedCol() {
    return g_selected_col;
}

bool& CapsLock() {
    return g_caps_lock;
}

void TriggerAction(int action) {
    switch (action) {
    case ActCaps:
        g_caps_lock = !g_caps_lock;
        break;
    case ActSpace:
        if (g_callbacks.on_append) g_callbacks.on_append(" ");
        break;
    case ActBackspace:
        if (g_callbacks.on_backspace) g_callbacks.on_backspace();
        break;
    case ActClear:
        if (g_callbacks.on_clear) g_callbacks.on_clear();
        break;
    case ActConfirm:
        if (g_callbacks.on_confirm) g_callbacks.on_confirm();
        break;
    case ActCancel:
        if (g_callbacks.on_cancel) g_callbacks.on_cancel();
        break;
    default:
        break;
    }
}

namespace {

/// Types the key the cursor is on (an action runs its callback).
void TriggerSelected() {
    const int row = g_selected_row;
    if (row == kActionRow) {
        TriggerAction(g_selected_col);
        return;
    }
    if (const char* const* keys = RowKeys(row)) {
        if (g_callbacks.on_append) g_callbacks.on_append(keys[g_selected_col]);
    }
}

/// Moves the cursor, wrapping vertically and keeping the column inside the new row.
void MoveCursor(int d_row, int d_col) {
    if (d_row != 0) {
        g_selected_row = (g_selected_row + d_row + kAllRows) % kAllRows;
    }
    const int len = RowLength(g_selected_row);
    if (d_col != 0) {
        g_selected_col = (g_selected_col + d_col + len) % len;
    } else {
        g_selected_col = std::clamp(g_selected_col, 0, len - 1);
    }
}

} // namespace

} // namespace detail

void SetCallbacks(const Callbacks& cb) {
    detail::GetCallbacks() = cb;
}

void Reset() {
    detail::SelectedRow() = 1;
    detail::SelectedCol() = 0;
    detail::CapsLock() = true;
}

bool HandleGamepadEvent(const SDL_Event& event) {
    using namespace detail;

    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        switch (event.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            MoveCursor(-1, 0);
            return true;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            MoveCursor(1, 0);
            return true;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            MoveCursor(0, -1);
            return true;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            MoveCursor(0, 1);
            return true;
        case SDL_GAMEPAD_BUTTON_SOUTH:
            TriggerSelected();
            return true;
        case SDL_GAMEPAD_BUTTON_WEST:
            if (GetCallbacks().on_backspace) GetCallbacks().on_backspace();
            return true;
        case SDL_GAMEPAD_BUTTON_NORTH:
            if (GetCallbacks().on_append) GetCallbacks().on_append(" ");
            return true;
        case SDL_GAMEPAD_BUTTON_EAST:
            if (GetCallbacks().on_cancel) GetCallbacks().on_cancel();
            return true;
        case SDL_GAMEPAD_BUTTON_START:
            if (GetCallbacks().on_confirm) GetCallbacks().on_confirm();
            return true;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            CapsLock() = !CapsLock();
            return true;
        default:
            break;
        }
    } else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        // The stick repeats, but not faster than the repeat interval.
        constexpr std::int16_t kDeadzone = 18000;
        constexpr std::uint64_t kRepeatMs = 180;
        const std::uint64_t now = SDL_GetTicks();
        if (now - g_last_stick_tick <= kRepeatMs) {
            return false;
        }
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            if (event.gaxis.value > kDeadzone) {
                MoveCursor(0, 1);
                g_last_stick_tick = now;
                return true;
            }
            if (event.gaxis.value < -kDeadzone) {
                MoveCursor(0, -1);
                g_last_stick_tick = now;
                return true;
            }
        } else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            if (event.gaxis.value > kDeadzone) {
                MoveCursor(1, 0);
                g_last_stick_tick = now;
                return true;
            }
            if (event.gaxis.value < -kDeadzone) {
                MoveCursor(-1, 0);
                g_last_stick_tick = now;
                return true;
            }
        }
    }
    return false;
}

} // namespace BbVirtualKeyboard
