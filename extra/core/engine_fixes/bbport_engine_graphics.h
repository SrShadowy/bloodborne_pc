#pragma once

#include <cstdint>

namespace BbEngineGraphics {

// Installs live post-processing, lighting, shadow, fog, LOD, and 4x4 light grid hooks.
void PatchImage(unsigned char* image, std::uint64_t size);

} // namespace BbEngineGraphics
