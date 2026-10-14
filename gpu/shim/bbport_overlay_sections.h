#pragma once

#include "bbport_settings.h"

namespace BbOverlaySections {

// Renders the live YEBIS post-processing, SSAO strength, shadow cascade distance, and fog sliders.
void RenderPostProcessingAndLightingSection(BbSettings::Values& s, int lang, bool& dirty);

// Renders the live Camera FOV, distance, and height sliders.
void RenderCameraSection(BbSettings::Values& s, int lang, bool& dirty);

// Renders the 4x4 Light Grid Optimization checkbox in the Performance section.
void RenderLightGridCheckbox(BbSettings::Values& s, int lang, bool& dirty);

// Renders the Raw 1:1 Mouse Camera and Hold Movement/Slope Auto-Rotation checkboxes.
void RenderRawMouseCameraControls(BbSettings::Values& s, int lang, bool& dirty);

} // namespace BbOverlaySections
