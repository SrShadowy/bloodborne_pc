// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UI {

class Assembler {
public:
    static bool Assemble(const std::string& asm_text, uintptr_t target_addr,
                         std::vector<uint8_t>& out_bytes, std::string& out_error);
};

} // namespace UI
