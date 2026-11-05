// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the button-prompt files share, and the game's key guide icon table.
// bbport_prompt_table.cpp reads the game's file and hashes those icons by their bytes;
// bbport_prompt_draw.cpp draws the port's replacements; bbport_button_prompts.cpp is the public
// API over both. Only this module needs this header.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <utility>
#include <vector>

#include "bbport_button_prompts.h"

namespace BbPrompts::detail {

/// How an icon arranges the inputs it shows.
enum class Layout {
    Single, ///< one input
    Column, ///< two inputs, one over the other (the d-pad's up and down)
    Row,    ///< two inputs side by side (left and right)
    Cross,  ///< up, left, down, right: a key cluster (W over A S D)
    Stick,  ///< a stick: its four keys, the mouse (look) or the pad's stick
};

/// One of the game's 32x32 icons and the inputs whose labels replace it.
struct IconDef {
    const char* name;
    Layout layout;
    std::array<const char*, 4> inputs; ///< runtime_pad.c input names
};

/// One texture of the game's HUD (ingametop.gfx) that packs key guide icons side by side.
struct AtlasDef {
    const char* name;
    std::uint32_t width, height, bytes; ///< BC7, one level
    std::array<std::pair<std::uint32_t, std::uint32_t>, 3> cells;
    std::array<const char*, 3> icons;
};

// The icons are known by what they show, not by their names: KG_OK is the circle and KG_Cancel
// the cross (the Japanese layout; a release whose confirm button is the cross shows KG_Cancel for
// OK). KG_R_U and KG_R_L are the triangle and the square (the right cluster's up and left).
// The order is the public numbering (index + 1).
inline constexpr IconDef kIcons[] = {
    {"KG_OK", Layout::Single, {"circle"}},
    {"KG_Cancel", Layout::Single, {"cross"}},
    {"KG_R_U", Layout::Single, {"triangle"}},
    {"KG_R_L", Layout::Single, {"square"}},
    {"KG_L1", Layout::Single, {"l1"}},
    {"KG_L2", Layout::Single, {"l2"}},
    {"KG_R1", Layout::Single, {"r1"}},
    {"KG_R2", Layout::Single, {"r2"}},
    {"KG_L3", Layout::Single, {"l3"}},
    {"KG_R3", Layout::Single, {"r3"}},
    {"KG_Start", Layout::Single, {"options"}},
    {"KG_TP_L", Layout::Single, {"touchpad"}},
    {"KG_TP_R", Layout::Single, {"touchpad_right"}},
    {"KG_L_U", Layout::Single, {"up"}},
    {"KG_L_D", Layout::Single, {"down"}},
    {"KG_L_L", Layout::Single, {"left"}},
    {"KG_L_R", Layout::Single, {"right"}},
    {"KG_L_UD", Layout::Column, {"up", "down"}},
    {"KG_L_LR", Layout::Row, {"left", "right"}},
    {"KG_L_UDLR", Layout::Cross, {"up", "left", "down", "right"}},
    {"KG_LS", Layout::Stick, {"move_up", "move_left", "move_down", "move_right"}},
    {"KG_RS", Layout::Stick, {"look_up", "look_left", "look_down", "look_right"}},
};

// Atlases of the HUD (ingametop.gfx) with icons in them, the same as the icons' textures (the
// drawing 3 pixels in from the cell's edges): MENU_Common_00091 has the circle, the cross and L3
// in a row (the lamps', gravestones' and items' "Examine" and "Pick up" prompts).
inline constexpr AtlasDef kAtlases[] = {
    {"MENU_Common_00091", 1024, 512, 1024 * 512, {{{658, 120}, {691, 120}, {723, 120}}},
     {"KG_OK", "KG_Cancel", "KG_L3"}},
};

constexpr std::size_t kIconCount = std::size(kIcons);
constexpr std::size_t kAtlasCount = std::size(kAtlases);
/// A 32x32 BC7 texture: one byte a pixel, one level.
constexpr std::size_t kGameIconBytes = std::size_t(GameIconSize) * GameIconSize;

/// 0: the entry is not in the game's file. Indexed like kIcons / kAtlases.
extern std::array<std::uint64_t, kIconCount> g_icon_hashes;
extern std::array<std::uint64_t, kAtlasCount> g_atlas_hashes;
/// The cells of each atlas whose icons were found.
extern std::array<std::vector<AtlasCell>, kAtlasCount> g_atlas_cells;
/// The game's own icon bytes (IconBytes, BB_PROMPT_TRACE).
extern std::array<std::array<std::uint8_t, kGameIconBytes>, kIconCount> g_icon_bytes;
/// Set once the loader thread has finished (AtlasCells waits for it).
extern std::atomic<bool> g_table_ready;

/// BB_BUTTON_PROMPTS is not 0.
bool Switch();

/// Reads the game's icon table (bbport_prompt_table.cpp).
void LoadTable();

/// Starts the loader thread, once.
void StartLoader();

/// Waits for LoadTable() to finish, starting it if nothing did.
void WaitTable();

/// Loads the embedded font the labels are set in (bbport_prompt_canvas.cpp); false: no labels.
bool InitFont();

/// Draws `icon` for the device in use into `out` (IconSize * IconSize RGBA8 pixels); false keeps
/// the game's own icon.
bool DrawIcon(const IconDef& icon, std::uint32_t* out);

} // namespace BbPrompts::detail
