#pragma once

#include <cstdint>

namespace BbFrameRate {

// Applies bbhost's high-FPS animation, cloth, physics, AI, and dynamic per-frame dt
// patches to the guest eboot image at startup when running above 30 FPS (60/90/Uncap).
void PatchImage(unsigned char* image, std::uint64_t size);

} // namespace BbFrameRate
