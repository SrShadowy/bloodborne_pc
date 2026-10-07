// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>

namespace UI {

enum class TabId {
    None = -1,
    Settings = 0,
    Scanner = 1,
    Watchlist = 2,
    HexView = 3,
    Debugger = 4
};

class UiManager {
public:
    static void InitStyle();
    static void Render();
    static void Tick();

    static void RequestTab(TabId id);
    static void SetStatus(const std::string& msg);
};

} // namespace UI
