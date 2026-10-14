// SPDX-License-Identifier: GPL-2.0-or-later
// Scaleform in-game menu mouse pointer (hover, left-click decide/arrows, right-click back, wheel)
// ported from BBHost (menu_pointer.cpp).
#pragma once

#include <cstdint>

namespace BbMenuPointer {

void Install(uintptr_t image_base, uint64_t image_size);

// True while the player is on the title screen or any in-game Scaleform menu/list has focus.
bool InMenu();

// Region-aware menu confirm/back ScePad buttons (Cross=0x4000 / Circle=0x2000).
uint32_t ConfirmButton();
uint32_t BackButton();

// Window mouse events in 1920x1080 Scaleform stage coordinates.
void SetMouseStagePos(float sx, float sy, bool in_window);
void NotifyLeftClick(bool down);
void NotifyRightClick();
void NotifyWheel(float notches);

// Called from runtime_pad.c on each pad poll: records device directional state and returns
// any active menu button bits (Confirm, Back, Left/Right spinner step, Up/Down wheel).
uint32_t StepPad(uint32_t current_buttons, uint8_t lx, uint8_t ly);

} // namespace BbMenuPointer
