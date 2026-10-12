// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>

namespace UI {

class TabDebugger {
public:
    static void Render();
    static void SetDisasmAddress(uintptr_t address);
    static void SetBreakpointAddress(uintptr_t address);
    static uintptr_t GetDisasmAddress();

    static uintptr_t FindPreviousInstruction(uintptr_t addr);
    static uintptr_t FindNextInstruction(uintptr_t addr);
    static void RenderDisassemblerView(uintptr_t& cur_addr, float height = 230.0f, bool standalone = false);
};

} // namespace UI
