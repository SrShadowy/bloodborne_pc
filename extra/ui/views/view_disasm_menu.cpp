// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the disassembler row's context menu -- copy the row, follow a target the instruction names
// (or a register left by the last hit) in the hex viewer or here, patch the bytes, NOP them, or set
// a breakpoint. It runs inside the row's ImGui popup (view_disasm_render.cpp).
#include "view_disasm_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <imgui.h>

#include "breakpoint.h"
#include "mem_scanner.h"
#include "modal_patch_inst.h"
#include "symbol_manager.h"
#include "ui_manager.h"
#include "ui_strings.h"
#include "view_disasm.h"
#include "view_memviewer.h"

namespace UI::DisasmDetail {
namespace {

constexpr ImVec4 kMenuSymbolColor{1.0f, 0.85f, 0.35f, 1.0f};
constexpr ImVec4 kMenuAddressColor{1.0f, 0.8f, 0.2f, 1.0f};

/// "name: 0xADDR <symbol>", or "name: 0xADDR" when the address has no symbol.
std::string TargetLabel(const char* action, std::uintptr_t address) {
    const std::string symbol = Debugger::SymbolManager::Get().Resolve(address, true, kSymbolSpan);
    char buf[160];
    if (!symbol.empty()) {
        std::snprintf(buf, sizeof(buf), "%s: 0x%llx <%s>", action,
                      static_cast<unsigned long long>(address), symbol.c_str());
    } else {
        std::snprintf(buf, sizeof(buf), "%s: 0x%llx", action,
                      static_cast<unsigned long long>(address));
    }
    return buf;
}

/// Every "0x..." in `text` that could be an address (skips small ones: offsets and immediates).
std::vector<std::uintptr_t> AddressesIn(const std::string& text) {
    std::vector<std::uintptr_t> results;
    std::size_t pos = 0;
    while ((pos = text.find("0x", pos)) != std::string::npos) {
        std::size_t end = pos + 2;
        while (end < text.size() && std::isxdigit(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        if (end > pos + 2) {
            const auto value = std::strtoull(text.substr(pos, end - pos).c_str(), nullptr, 16);
            if (value >= kMinFollowable &&
                std::find(results.begin(), results.end(), value) == results.end()) {
                results.push_back(static_cast<std::uintptr_t>(value));
            }
        }
        pos = end;
    }
    return results;
}

/// One register's submenu: follow its value in the hex viewer or here.
void RegisterMenu(const char* name, std::uint64_t value, std::uintptr_t& followed_address) {
    if (value == 0) {
        return;
    }
    const std::string label = TargetLabel(name, static_cast<std::uintptr_t>(value));
    if (!ImGui::BeginMenu(label.c_str())) {
        return;
    }
    if (ImGui::MenuItem(L("Follow in Hex", "Seguir no Hex", "В Hex"))) {
        ViewMemViewer::NavigateHex(static_cast<std::uintptr_t>(value));
    }
    if (ImGui::MenuItem(L("Follow in Disasm", "Seguir no Disasm", "В Дизасм"))) {
        followed_address = static_cast<std::uintptr_t>(value);
        ViewDisasm::SetDisasmAddress(followed_address);
    }
    ImGui::EndMenu();
}

/// The registers of the last hit, each with its follow menu (only ones that are non-zero).
void RegistersMenu(std::uintptr_t& followed_address) {
    const auto regs = Debugger::BreakpointManager::Get().GetLastRegisters();
    if (!regs.valid) {
        return;
    }
    ImGui::Separator();
    if (!ImGui::BeginMenu(L("Follow Register...", "Seguir Registrador...", "Перейти по регистру..."))) {
        return;
    }
    RegisterMenu("RAX", regs.rax, followed_address);
    RegisterMenu("RBX", regs.rbx, followed_address);
    RegisterMenu("RCX", regs.rcx, followed_address);
    RegisterMenu("RDX", regs.rdx, followed_address);
    RegisterMenu("RSI", regs.rsi, followed_address);
    RegisterMenu("RDI", regs.rdi, followed_address);
    RegisterMenu("RBP", regs.rbp, followed_address);
    RegisterMenu("RSP", regs.rsp, followed_address);
    RegisterMenu("R8", regs.r8, followed_address);
    RegisterMenu("R9", regs.r9, followed_address);
    RegisterMenu("R10", regs.r10, followed_address);
    RegisterMenu("R11", regs.r11, followed_address);
    RegisterMenu("R12", regs.r12, followed_address);
    RegisterMenu("R13", regs.r13, followed_address);
    RegisterMenu("R14", regs.r14, followed_address);
    RegisterMenu("R15", regs.r15, followed_address);
    RegisterMenu("RIP", regs.rip, followed_address);
    ImGui::EndMenu();
}

/// The targets an instruction names, each followable in the hex viewer or here.
void TargetMenus(const std::string& raw_text, std::uintptr_t& followed_address) {
    const std::vector<std::uintptr_t> targets = AddressesIn(raw_text);
    if (targets.empty()) {
        return;
    }
    ImGui::Separator();
    for (const std::uintptr_t target : targets) {
        const std::string hex_label = TargetLabel(L("Follow Target in Hex", "Seguir Alvo no Hex",
                                                    "В Hex по цели"),
                                                  target);
        const std::string disasm_label = TargetLabel(L("Follow Target in Disasm",
                                                       "Seguir Alvo no Disasm", "В Дизасм по цели"),
                                                     target);
        if (ImGui::MenuItem(hex_label.c_str())) {
            ViewMemViewer::NavigateHex(target);
        }
        if (ImGui::MenuItem(disasm_label.c_str())) {
            followed_address = target;
            ViewDisasm::SetDisasmAddress(target);
        }
    }
}

} // namespace

void DrawInstructionMenu(std::uintptr_t address, const std::uint8_t* code, std::size_t inst_len,
                         const std::string& raw_text, const std::string& pc_text,
                         const std::string& bytes_text, const std::string& full_line, bool has_bp,
                         bool is_patched, std::uintptr_t& followed_address) {
    const std::string symbol = Debugger::SymbolManager::Get().Resolve(address, true, kSymbolSpan);
    if (!symbol.empty()) {
        ImGui::TextColored(kMenuSymbolColor, "%s (0x%012llx)", symbol.c_str(),
                           static_cast<unsigned long long>(address));
    } else {
        ImGui::TextColored(kMenuAddressColor, "0x%012llx", static_cast<unsigned long long>(address));
    }
    ImGui::Separator();

    const auto copy = [](const char* label, const char* shortcut, const std::string& value) {
        if (ImGui::MenuItem(label, shortcut)) {
            ImGui::SetClipboardText(value.c_str());
        }
    };
    copy(L("Copy Address", "Copiar Endereco", "Копировать адрес"), "Ctrl+C", pc_text);
    copy(L("Copy Instruction", "Copiar Instrucao", "Копировать инструкцию"), nullptr, raw_text);
    copy(L("Copy Hex Bytes", "Copiar Bytes Hex", "Копировать байты Hex"), nullptr, bytes_text);
    copy(L("Copy Full Line", "Copiar Linha Completa", "Копировать всю строку"), nullptr, full_line);
    ImGui::Separator();

    if (ImGui::MenuItem(L("Follow in Hex View", "Seguir na Visao Hex", "Перейти в Hex"))) {
        ViewMemViewer::NavigateHex(address);
    }
    TargetMenus(raw_text, followed_address);
    RegistersMenu(followed_address);

    ImGui::Separator();
    if (ImGui::MenuItem(L("Edit Instruction / Patch Bytes...",
                         "Editar Instrucao / Patch de Bytes...", "Редактировать байты..."))) {
        ModalPatchInst::Open(address, inst_len, code, raw_text);
    }
    if (is_patched && ImGui::MenuItem(L("Restore Original Bytes", "Restaurar Bytes Originais",
                                        "Восстановить байты"))) {
        ViewDisasm::RestoreInstruction(address);
        UiManager::SetStatus(L("Restored original instruction", "Instrucao original restaurada",
                               "Восстановлена инструкция"));
    }
    if (ImGui::MenuItem(L("NOP Instruction", "Preencher com NOP (0x90)", "Заполнить NOP"))) {
        ViewDisasm::NopInstruction(address);
        UiManager::SetStatus(L("NOP applied", "NOP aplicado", "NOP применен"));
    }

    ImGui::Separator();
    auto& bp = Debugger::BreakpointManager::Get();
    const char* const breakpoint_label =
        has_bp ? L("Remove Breakpoint", "Remover Breakpoint", "Удалить точку")
               : L("Add Breakpoint", "Adicionar Breakpoint", "Добавить точку");
    if (ImGui::MenuItem(breakpoint_label)) {
        if (has_bp) {
            bp.RemoveBreakpoint(address);
        } else {
            bp.AddBreakpoint(address, "Disasm BP");
        }
    }
}

} // namespace UI::DisasmDetail
