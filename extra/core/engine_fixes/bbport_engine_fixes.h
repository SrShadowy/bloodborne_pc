#pragma once

#include <cstdint>

namespace BbEngineFixes {

// Installs high-FPS play-time fractional accumulator, 30 Hz player durability/fall-height
// step limiter, and 2x engine heap/menu pool headroom patches.
void PatchImage(unsigned char* image, std::uint64_t size);

} // namespace BbEngineFixes
