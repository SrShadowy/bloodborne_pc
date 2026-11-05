// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the localized-text tables share (bbport_strings_table_*.cpp). Each table lists its
// own rows with an explicit StringId, so the enum and the tables cannot drift apart the way a
// position-indexed array does.
#pragma once

#include <span>

#include "bbport_strings.h"

namespace BbStrings::detail {

/// One message in the three languages bbport ships.
struct Text {
    const char* en;
    const char* pt;
    const char* ru;
};

/// One row of a table: which message it is, and its text.
struct Entry {
    StringId id;
    Text text;
};

/// A table's rows. The whole list is spread over the bbport_strings_table_*.cpp files, one per
/// area of the settings menu.
using Table = std::span<const Entry>;

Table UpscalerTable();
Table VisualsTable();
Table OutputTable();
Table EffectsTable();
Table UiTable();
Table CameraTable();

} // namespace BbStrings::detail
