// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace UI {

class ViewDisasm {
public:
    static uintptr_t GetDisasmAddress();
    static void SetDisasmAddress(uintptr_t addr);
    static uintptr_t FindPreviousInstruction(uintptr_t addr);
    static uintptr_t FindNextInstruction(uintptr_t addr);
    static void RenderDisassemblerView(uintptr_t& cur_addr, float height = 230.0f, bool standalone = false);
    static bool RestoreInstruction(uintptr_t addr);
    static bool NopInstruction(uintptr_t addr);
    static bool HasPatch(uintptr_t addr);
    static void ApplyPatch(uintptr_t addr, const std::vector<uint8_t>& new_bytes, const std::vector<uint8_t>& orig_bytes);
};

} // namespace UI
