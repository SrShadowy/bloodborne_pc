// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the x86-64 disassembler view's state and navigation -- where the table starts, which
// instruction was clicked (and copied), what was patched, and stepping from one instruction to the
// next. view_disasm_render.cpp draws it and view_disasm_menu.cpp holds the row's context menu.
#include "view_disasm.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <imgui.h>

#include "breakpoint.h"
#include "common/decoder.h"
#include "mem_scanner.h"
#include "ui_manager.h"
#include "view_disasm_internal.h"
#include "view_memviewer.h"
#include "ui_strings.h"

namespace UI {
namespace {

using DisasmDetail::kMaxInstructionBytes;

DisasmDetail::State g_state;

} // namespace

namespace DisasmDetail {

State& GetState() {
    return g_state;
}

std::uintptr_t ToRuntime(std::uintptr_t address) {
    return (address >= kEbootBase && address < kRuntimeBase) ? ((address - kEbootBase) + kRuntimeBase)
                                                             : address;
}

std::size_t InstructionLength(const void* instruction) {
    if (!instruction) {
        return 1;
    }
    std::uint8_t code[kMaxInstructionBytes + 1] = {};
    std::copy_n(static_cast<const std::uint8_t*>(instruction), kMaxInstructionBytes, code);
    ZydisDecodedInstruction inst;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(Common::Decoder::Instance()->decodeInstruction(inst, operands, code,
                                                                    kMaxInstructionBytes))) {
        return 1;
    }
    return (inst.length > 0 && inst.length <= kMaxInstructionBytes) ? inst.length : 1;
}

} // namespace DisasmDetail

using DisasmDetail::GetState;
using DisasmDetail::kCodeChunkBytes;
using DisasmDetail::kEbootBase;
using DisasmDetail::kMaxInstructionBytes;
using DisasmDetail::kMinFollowable;
using DisasmDetail::kRowTextBytes;
using DisasmDetail::kRuntimeBase;
using DisasmDetail::kSymbolSpan;
using DisasmDetail::ToRuntime;

uintptr_t ViewDisasm::GetDisasmAddress() {
    return GetState().current;
}

void ViewDisasm::SetDisasmAddress(uintptr_t addr) {
    GetState().current = addr;
}

uintptr_t ViewDisasm::GetSelectedAddress() {
    return GetState().selected;
}

void ViewDisasm::SetSelectedAddress(uintptr_t addr) {
    GetState().selected = addr;
}

bool ViewDisasm::GetShowSymbols() {
    return GetState().show_symbols;
}

void ViewDisasm::SetShowSymbols(bool show) {
    GetState().show_symbols = show;
}

bool ViewDisasm::HasPatch(uintptr_t addr) {
    return GetState().patched.find(addr) != GetState().patched.end();
}

void ViewDisasm::ApplyPatch(uintptr_t addr, const std::vector<uint8_t>& new_bytes,
                            const std::vector<uint8_t>& orig_bytes) {
    auto& patched = GetState().patched;
    if (patched.find(addr) == patched.end()) {
        patched[addr] = orig_bytes;
    }
    if (Debugger::MemoryScanner::WriteMemory(addr, new_bytes.data(), new_bytes.size())) {
        char msg[96];
        std::snprintf(msg, sizeof(msg), "Patched %zu byte(s) at 0x%012llx", new_bytes.size(),
                      static_cast<unsigned long long>(addr));
        GetState().patch_status = msg;
        UiManager::SetStatus(msg);
    }
}

bool ViewDisasm::RestoreInstruction(uintptr_t addr) {
    auto& patched = GetState().patched;
    auto it = patched.find(addr);
    if (it == patched.end()) {
        return false;
    }
    const bool ok = Debugger::MemoryScanner::WriteMemory(addr, it->second.data(), it->second.size());
    if (ok) {
        patched.erase(it);
    }
    return ok;
}

bool ViewDisasm::NopInstruction(uintptr_t addr) {
    std::uint8_t code[kCodeChunkBytes] = {};
    if (!Debugger::MemoryScanner::ReadMemory(addr, code, sizeof(code))) {
        return false;
    }
    ZydisDecodedInstruction inst;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(Common::Decoder::Instance()->decodeInstruction(inst, operands, code,
                                                                    sizeof(code)))) {
        return false;
    }
    const std::size_t len = DisasmDetail::InstructionLength(code);
    std::vector<uint8_t> orig(code, code + len);
    std::vector<uint8_t> nops(len, 0x90);
    if (!Debugger::MemoryScanner::WriteMemory(addr, nops.data(), len)) {
        return false;
    }
    auto& patched = GetState().patched;
    if (patched.find(addr) == patched.end()) {
        patched[addr] = orig;
    }
    return true;
}

uintptr_t ViewDisasm::FindPreviousInstruction(uintptr_t addr) {
    if (addr == 0) {
        return 0;
    }
    for (std::size_t step = 1; step <= kMaxInstructionBytes; ++step) {
        if (addr < step) {
            break;
        }
        const uintptr_t candidate = addr - step;
        std::uint8_t code[kMaxInstructionBytes + 1] = {};
        if (Debugger::MemoryScanner::ReadMemory(candidate, code, kMaxInstructionBytes) &&
            DisasmDetail::InstructionLength(code) == step) {
            return candidate;
        }
    }
    return addr >= 4 ? addr - 4 : addr - 1;
}

uintptr_t ViewDisasm::FindNextInstruction(uintptr_t addr) {
    std::uint8_t code[kMaxInstructionBytes + 1] = {};
    if (!Debugger::MemoryScanner::ReadMemory(addr, code, kMaxInstructionBytes)) {
        return addr + 1;
    }
    return addr + DisasmDetail::InstructionLength(code);
}

void ViewDisasm::RenderDisassemblerView(uintptr_t& cur_addr, float height, bool standalone) {
    std::uint8_t code[DisasmDetail::kCodeChunkBytes];
    if (!DisasmDetail::RenderToolbar(cur_addr, standalone, code)) {
        return;
    }

    const char* child_id = standalone ? "DisasmChildMV" : "DisasmChildTab";
    ImGui::BeginChild(child_id, ImVec2(0, height), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel > 0.0f) {
            const int steps = std::max(1, static_cast<int>(wheel));
            for (int i = 0; i < steps; ++i) {
                cur_addr = ViewDisasm::FindPreviousInstruction(cur_addr);
            }
        } else if (wheel < 0.0f) {
            const int steps = std::max(1, static_cast<int>(-wheel));
            for (int i = 0; i < steps; ++i) {
                cur_addr = ViewDisasm::FindNextInstruction(cur_addr);
            }
        }
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        if (!GetState().selected_line.empty()) {
            ImGui::SetClipboardText(GetState().selected_line.c_str());
            UiManager::SetStatus(L("Copied selected instruction to clipboard",
                                   "Instrucao selecionada copiada", "Инструкция скопирована"));
        }
    }

    DisasmDetail::RenderTable(cur_addr, code, height, standalone);
    ImGui::EndChild();
}

} // namespace UI
