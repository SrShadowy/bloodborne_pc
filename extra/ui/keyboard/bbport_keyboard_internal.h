// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the virtual keyboard's files share. bbport_keyboard.cpp owns the cursor and the
// gamepad handling, bbport_keyboard_render.cpp draws the dialog. Only this module needs this
// header.
#pragma once

#include "bbport_keyboard.h"

#include <imgui.h>

namespace BbVirtualKeyboard::detail {

// ---- The key grid ----

/// The character rows, upper and lower case. Row 0 has no case.
inline constexpr const char* kRow0[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "_"};
inline constexpr const char* kRow1Upper[] = {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"};
inline constexpr const char* kRow1Lower[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"};
inline constexpr const char* kRow2Upper[] = {"A", "S", "D", "F", "G", "H", "J", "K", "L", "'"};
inline constexpr const char* kRow2Lower[] = {"a", "s", "d", "f", "g", "h", "j", "k", "l", "'"};
inline constexpr const char* kRow3Upper[] = {"Z", "X", "C", "V", "B", "N", "M", ",", ".", "/"};
inline constexpr const char* kRow3Lower[] = {"z", "x", "c", "v", "b", "n", "m", ",", ".", "/"};

/// The character rows, plus the row of actions below them.
inline constexpr int kCharRows = 4;
inline constexpr int kAllRows = 5;
/// The row that holds the actions (caps, space, backspace, clear, confirm, cancel).
inline constexpr int kActionRow = 4;

/// The actions, in the order they are drawn.
enum ActionKey {
    ActCaps = 0,
    ActSpace,
    ActBackspace,
    ActClear,
    ActConfirm,
    ActCancel,
    ActCount
};

// ---- State and behaviour (bbport_keyboard.cpp) ----

/// The callbacks the owner installed.
Callbacks& GetCallbacks();

/// The keys of a character row: the lower-case set while Caps is off.
const char* const* RowKeys(int row);

/// How many keys a row holds.
int RowLength(int row);

/// The row and column the cursor is on (the renderer draws them highlighted).
int& SelectedRow();
int& SelectedCol();

/// Whether Caps is on.
bool& CapsLock();

/// Runs an action key.
void TriggerAction(int action);

// ---- Drawing (bbport_keyboard_render.cpp) ----

/// The character rows, with the cursor's key highlighted.
void RenderCharRows(const ImVec2& key_size, float scale);

/// The action row (each key as wide as its label needs).
void RenderActionRow(const ImVec2& key_size, float scale, int lang);

} // namespace BbVirtualKeyboard::detail
