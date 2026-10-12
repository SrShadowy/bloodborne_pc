// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace UI {

class ViewMemViewer {
public:
    static bool IsOpen();
    static void SetOpen(bool open);
    static void Toggle();
    static void Render();
    static void NavigateHex(uintptr_t addr);
    static void NavigateDisasm(uintptr_t addr);
};

} // namespace UI
