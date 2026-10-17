// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace UI {

class ViewHexGrid {
public:
    static void RenderHexGridView(uintptr_t& cur_addr, float height = 280.0f, bool standalone = false);
    static uintptr_t GetSelectedAddress();
    static bool HasSelection();
    static void SetSelectedAddress(uintptr_t addr);
};

} // namespace UI
