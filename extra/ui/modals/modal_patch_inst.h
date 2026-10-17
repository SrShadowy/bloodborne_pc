// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace UI {

class ModalPatchInst {
public:
    static void Open(uintptr_t addr, size_t len, const uint8_t* orig_code, const std::string& disasm_text);
    static void Render();
};

} // namespace UI
