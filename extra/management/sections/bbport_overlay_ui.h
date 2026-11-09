// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the in-game settings sections share -- the localized text pickers, the widgets
// bound to an atomic setting, and the palette/sizes the overlay is drawn with
// (bbport_overlay_section_*.cpp).
#pragma once

#include <atomic>

#include "bbport_settings.h"

#include <imgui.h>

namespace BbOverlaySections {

/// The text for `lang` (BbSettings::Language): English, Portuguese (Brazil) or Russian.
/// Russian falls back to English when `ru` is null.
const char* Tr(int lang, const char* en, const char* pt, const char* ru = nullptr);

/// The option list for `lang`, for combos whose entries have to change language together.
const char* const* TrList(int lang, const char* const* en, const char* const* pt,
                          const char* const* ru);

/// A checkbox bound to a setting; marks `dirty` when the user changes it.
void Checkbox(const char* label, std::atomic<bool>& value, bool& dirty);
/// A slider bound to a setting; marks `dirty` when the user changes it.
void Slider(const char* label, std::atomic<float>& value, float lo, float hi, bool& dirty);

/// A combo over `options` (one of the `count` entries, the current one selected) bound to a
/// setting. The stored value is clamped, so a hand-edited bbport.ini cannot index out of range.
/// `width` <= 0 keeps ImGui's default width. Marks `dirty` when the user picks another entry.
void ComboOptions(const char* label, const char* const* options, int count,
                  std::atomic<int>& value, bool& dirty, float width = 0.0f);

/// A "(?)" after the previous item, `text` in its tooltip.
void Hint(const char* text);

/// The overlay's own palette: ImVec4 where ImGui styles want a colour, ImU32 in draw lists.
namespace Theme {
// Buttons that toggle a window: "on" is the green pressed look, "off" the blue idle one.
inline constexpr ImVec4 kButtonOn{0.24f, 0.45f, 0.40f, 1.0f};
inline constexpr ImVec4 kButtonOnHovered{0.30f, 0.55f, 0.48f, 1.0f};
inline constexpr ImVec4 kButtonOff{0.18f, 0.32f, 0.42f, 1.0f};
inline constexpr ImVec4 kButtonOffHovered{0.24f, 0.42f, 0.54f, 1.0f};
// A window-opening button inside a settings row (FPS overlay), and the Discord one.
inline constexpr ImVec4 kButtonAction{0.18f, 0.35f, 0.48f, 1.0f};
inline constexpr ImVec4 kButtonActionHovered{0.24f, 0.45f, 0.58f, 1.0f};
inline constexpr ImVec4 kButtonPrimary{0.24f, 0.36f, 0.68f, 1.0f};
inline constexpr ImVec4 kButtonPrimaryHovered{0.32f, 0.46f, 0.82f, 1.0f};
// The selected entry of a filter row (trophies).
inline constexpr ImVec4 kButtonFilterActive{0.18f, 0.38f, 0.52f, 1.0f};

// Discord connection state: disabled, connected, waiting for the client.
inline constexpr ImVec4 kStatusOff{0.5f, 0.5f, 0.5f, 1.0f};
inline constexpr ImVec4 kStatusOnline{0.3f, 1.0f, 0.4f, 1.0f};
inline constexpr ImVec4 kStatusPending{1.0f, 0.75f, 0.2f, 1.0f};

// Text.
inline constexpr ImVec4 kTextHeading{0.4f, 0.75f, 1.0f, 1.0f};
inline constexpr ImVec4 kTextPrimary{0.88f, 0.90f, 0.92f, 1.0f};
inline constexpr ImVec4 kTextSecondary{0.72f, 0.74f, 0.78f, 1.0f};
inline constexpr ImVec4 kTextMuted{0.70f, 0.72f, 0.75f, 1.0f};
inline constexpr ImVec4 kTextTitle{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr ImVec4 kTextUnlocked{0.35f, 0.95f, 0.45f, 1.0f};
inline constexpr ImVec4 kTextLocked{0.60f, 0.60f, 0.60f, 0.8f};

// Trophy grades.
inline constexpr ImVec4 kGradePlatinum{0.78f, 0.94f, 1.0f, 1.0f};
inline constexpr ImVec4 kGradeGold{1.0f, 0.84f, 0.0f, 1.0f};
inline constexpr ImVec4 kGradeSilver{0.80f, 0.82f, 0.85f, 1.0f};
inline constexpr ImVec4 kGradeBronze{0.85f, 0.55f, 0.30f, 1.0f};
inline constexpr ImVec4 kGradeUnknown{0.7f, 0.7f, 0.7f, 1.0f};
/// The "[Platinum]" tag a trophy row starts with (a touch brighter than the counter's colour).
inline constexpr ImVec4 kGradeTagPlatinum{0.70f, 0.92f, 1.0f, 1.0f};

// The Discord activity card, styled after the Discord client's own widget.
inline constexpr ImVec4 kCardBackground{0.12f, 0.13f, 0.15f, 1.0f};
inline constexpr ImU32 kCardIconBackground = IM_COL32(35, 38, 44, 255);
inline constexpr ImU32 kCardIconBorder = IM_COL32(88, 101, 242, 220);
inline constexpr ImU32 kCardIconText = IM_COL32(235, 237, 239, 255);

// Trophy rows: unlocked rows read green, locked ones stay neutral.
inline constexpr ImU32 kTrophyUnlockedBackground = IM_COL32(28, 48, 36, 170);
inline constexpr ImU32 kTrophyLockedBackground = IM_COL32(22, 24, 28, 140);
inline constexpr ImU32 kTrophyUnlockedBorder = IM_COL32(70, 180, 100, 120);
inline constexpr ImU32 kTrophyLockedBorder = IM_COL32(255, 255, 255, 18);
inline constexpr ImU32 kTrophyNameText = IM_COL32(250, 250, 250, 255);
inline constexpr ImU32 kTrophyHiddenText = IM_COL32(160, 160, 160, 200);
inline constexpr ImU32 kTrophyDetailText = IM_COL32(185, 190, 195, 230);
inline constexpr ImU32 kTrophyProgressBar = IM_COL32(0, 180, 240, 230);
} // namespace Theme

/// Sizes of the section windows and their widgets, in ImGui pixels at scale 1.
namespace Size {
inline constexpr float kFpsWindowWidth = 540.0f;
inline constexpr float kFpsWindowHeight = 620.0f;
inline constexpr float kDiscordWindowWidth = 520.0f;
inline constexpr float kDiscordWindowHeight = 580.0f;

/// The four preset buttons share one row, with kPresetGap left at its right.
inline constexpr float kPresetGap = 18.0f;
inline constexpr float kPresetCount = 4.0f;
inline constexpr float kPresetButtonHeight = 40.0f;
inline constexpr float kPresetLabelWidth = 0.0f;
/// A button that spans the panel, leaving room for the "(?)" that follows it.
inline constexpr float kFullRowRightPad = 32.0f;
inline constexpr float kActionButtonHeight = 28.0f;
inline constexpr float kCloseButtonWidth = 120.0f;
inline constexpr float kComboWidth = 260.0f;
inline constexpr float kClientIdFieldWidth = 190.0f;
/// Gap between two widgets that are not a form row (SameLine spacing).
inline constexpr float kSameLineGap = 20.0f;
inline constexpr float kMemoryToolsButtonHeight = 32.0f;
inline constexpr float kMemoryViewerButtonHeight = 28.0f;

// Discord activity preview card.
inline constexpr float kActivityCardHeight = 120.0f;
inline constexpr float kActivityIconSize = 50.0f;
inline constexpr float kActivityIconRounding = 8.0f;
inline constexpr float kActivityIconTextOffset = 14.0f;
inline constexpr float kActivityIconTextTop = 16.0f;

// Trophy list.
inline constexpr float kTrophyProgressHeight = 18.0f;
inline constexpr float kTrophyGradeSpacingX = 14.0f;
inline constexpr float kTrophyGradeSpacingY = 4.0f;
inline constexpr float kTrophyCardRounding = 5.0f;
inline constexpr float kTrophyCardTopPad = 4.0f;
inline constexpr float kTrophyCardBottomPad = 6.0f;
inline constexpr float kTrophyCardIndent = 10.0f;
inline constexpr float kTrophyStatusGap = 16.0f;
inline constexpr float kTrophyDetailRightPad = 24.0f;
} // namespace Size

} // namespace BbOverlaySections
