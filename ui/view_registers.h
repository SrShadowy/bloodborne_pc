// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "breakpoint.h"

namespace UI {

class ViewRegisters {
public:
    static void RenderRegistersTable(const Debugger::CpuRegisters& regs, bool& has_selected_regs);
};

} // namespace UI
