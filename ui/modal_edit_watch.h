// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "mem_editor.h"
#include <cstddef>

namespace UI {

class ModalEditWatch {
public:
    static void Open(size_t index, const Debugger::WatchEntry& entry);
    static void Render();
    static bool IsOpen();
};

} // namespace UI
