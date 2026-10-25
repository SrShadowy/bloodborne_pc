#pragma once

#include "bbport_settings.h"

namespace BbOverlaySections {

// Renders the Photo Mode controls (freeze world, hide HUD, screenshot, quick reset).
void RenderPhotoModeSection(BbSettings::Values& s, int lang, bool& dirty);

// Renders the live YEBIS post-processing, SSAO strength, shadow cascade distance, and fog sliders.
void RenderPostProcessingAndLightingSection(BbSettings::Values& s, int lang, bool& dirty);

// Renders the live Camera FOV, distance, and height sliders.
void RenderCameraSection(BbSettings::Values& s, int lang, bool& dirty);

// Renders the 4x4 Light Grid Optimization checkbox in the Performance section.
void RenderLightGridCheckbox(BbSettings::Values& s, int lang, bool& dirty);

// Renders the Raw 1:1 Mouse Camera and Hold Movement/Slope Auto-Rotation checkboxes.
void RenderRawMouseCameraControls(BbSettings::Values& s, int lang, bool& dirty);

// Renders the customizable FPS & Performance Metrics HUD section.
void RenderFpsOverlaySection(BbSettings::Values& s, int lang, bool& dirty);

// Renders the standalone FPS & Performance Metrics HUD configuration window.
void RenderFpsOverlayWindow(BbSettings::Values& s, int lang, bool& dirty);

// Renders the customizable Info HUD (OSD) section.
void RenderInfoHudSection(BbSettings::Values& s, int lang, bool& dirty);

// Renders the Discord Rich Presence (RPC) configuration section in settings menu.
void RenderDiscordRpcSection(BbSettings::Values& s, int lang, bool& dirty);

// Renders the standalone Discord Rich Presence configuration window.
void RenderDiscordRpcWindow(BbSettings::Values& s, int lang, bool& dirty);

// Renders the controller prompt layout selector dropdown (Auto, PS, Xbox, Switch, KBM).
void RenderPromptLayoutSection(BbSettings::Values& s, int lang, bool& dirty);

// Renders the Memory Tools and Memory Viewer launcher buttons.
void RenderMemoryToolsSection(float base_scale);

} // namespace BbOverlaySections
