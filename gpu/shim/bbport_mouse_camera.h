#pragma once

#include <cstdint>

namespace BbMouseCamera {

// Installs the 1:1 mouse camera and auto-rotation bypass hooks into the guest image.
void PatchImage(unsigned char* image, std::uint64_t size);

// True when the binary hooks were installed and Raw 1:1 Mouse Camera is enabled in settings.
bool IsActive();

// Feeds raw relative mouse motion (pixels) from SDL_EVENT_MOUSE_MOTION while mouse is captured.
void FeedRelativeMotion(float dx, float dy);

// Updates whether the player is currently using the mouse (true) or gamepad (false) so
// movement/slope camera auto-rotation can be suppressed while using the mouse.
void SetUsingMouse(bool using_mouse);

// Consumes buffered mouse motion while locked-on and emits a short synthetic right-stick flick
// when a threshold is crossed. Returns true when out_rx / out_ry (0..255) should be applied.
bool TakeTargetSwitchStep(std::uint8_t* out_rx, std::uint8_t* out_ry);

} // namespace BbMouseCamera
