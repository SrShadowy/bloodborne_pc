// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: live camera field of view (FOV), distance, and height scaling on LockCamParam.
#pragma once

#include <cstdint>

namespace BbCamera {

/// Uncaps the 48-degree FOV clamp in ChrExFollowCam::Update and records the image base.
void PatchImage(unsigned char* image, std::uint64_t size);

/// Updates LockCamParam's camDistTarget, chrOrgOffset_Y, and camFovY in real time.
void Tick();

} // namespace BbCamera
