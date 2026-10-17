// SPDX-License-Identifier: GPL-2.0-or-later
#include "view_disasm.h"
#include "modal_patch_inst.h"
#include "ui_strings.h"
#include "view_memviewer.h"
#include "ui_manager.h"
#include "breakpoint.h"
#include "mem_scanner.h"
#include "symbol_manager.h"
#include "common/decoder.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace UI {

static uintptr_t disasm_current_addr = 0x00400000;
static uintptr_t s_selected_disasm_addr = 0;
static std::string s_selected_line_copy;
static bool s_disasm_show_symbols = true;
static std::string patch_status_msg;
static std::unordered_map<uintptr_t, std::vector<uint8_t>> patched_instructions;

uintptr_t ViewDisasm::GetDisasmAddress() { return disasm_current_addr; }
void ViewDisasm::SetDisasmAddress(uintptr_t addr) { disasm_current_addr = addr; }
uintptr_t ViewDisasm::GetSelectedAddress() { return s_selected_disasm_addr; }
void ViewDisasm::SetSelectedAddress(uintptr_t addr) { s_selected_disasm_addr = addr; }
bool ViewDisasm::GetShowSymbols() { return s_disasm_show_symbols; }
void ViewDisasm::SetShowSymbols(bool show) { s_disasm_show_symbols = show; }
bool ViewDisasm::HasPatch(uintptr_t addr) { return patched_instructions.find(addr) != patched_instructions.end(); }

void ViewDisasm::ApplyPatch(uintptr_t addr, const std::vector<uint8_t>& new_bytes, const std::vector<uint8_t>& orig_bytes) {
    if (patched_instructions.find(addr) == patched_instructions.end()) patched_instructions[addr] = orig_bytes;
    if (Debugger::MemoryScanner::WriteMemory(addr, new_bytes.data(), new_bytes.size())) {
        char msg[96]; std::snprintf(msg, sizeof(msg), "Patched %zu byte(s) at 0x%012llx", new_bytes.size(), static_cast<unsigned long long>(addr));
        patch_status_msg = msg; UiManager::SetStatus(msg);
    }
}

bool ViewDisasm::RestoreInstruction(uintptr_t addr) {
    auto it = patched_instructions.find(addr);
    if (it == patched_instructions.end()) return false;
    bool ok = Debugger::MemoryScanner::WriteMemory(addr, it->second.data(), it->second.size());
    if (ok) patched_instructions.erase(it);
    return ok;
}

bool ViewDisasm::NopInstruction(uintptr_t addr) {
    uint8_t code_buf[16] = {0};
    if (!Debugger::MemoryScanner::ReadMemory(addr, code_buf, sizeof(code_buf))) return false;
    ZydisDecodedInstruction inst;
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
    if (!ZYAN_SUCCESS(Common::Decoder::Instance()->decodeInstruction(inst, operands, code_buf, sizeof(code_buf)))) return false;
    const size_t len = (inst.length > 0 && inst.length <= 15) ? inst.length : 1;
    std::vector<uint8_t> orig(code_buf, code_buf + len);
    std::vector<uint8_t> nops(len, 0x90);
    if (Debugger::MemoryScanner::WriteMemory(addr, nops.data(), len)) {
        if (patched_instructions.find(addr) == patched_instructions.end()) patched_instructions[addr] = orig;
        return true;
    }
    return false;
}

uintptr_t ViewDisasm::FindPreviousInstruction(uintptr_t addr) {
    if (addr == 0) return 0;
    for (int step = 1; step <= 15; ++step) {
        if (addr < static_cast<uintptr_t>(step)) break;
        uintptr_t candidate = addr - step;
        uint8_t buf[16];
        if (Debugger::MemoryScanner::ReadMemory(candidate, buf, sizeof(buf))) {
            ZydisDecodedInstruction inst;
            ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
            if (ZYAN_SUCCESS(Common::Decoder::Instance()->decodeInstruction(inst, operands, buf, sizeof(buf)))) {
                if (inst.length == step) return candidate;
            }
        }
    }
    return (addr >= 4) ? (addr - 4) : (addr - 1);
}

uintptr_t ViewDisasm::FindNextInstruction(uintptr_t addr) {
    uint8_t buf[16];
    if (Debugger::MemoryScanner::ReadMemory(addr, buf, sizeof(buf))) {
        ZydisDecodedInstruction inst;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
        if (ZYAN_SUCCESS(Common::Decoder::Instance()->decodeInstruction(inst, operands, buf, sizeof(buf)))) {
            const size_t len = (inst.length > 0 && inst.length <= 15) ? inst.length : 1;
            return addr + len;
        }
    }
    return addr + 1;
}

static std::vector<uintptr_t> ExtractAddressesFromText(const std::string& text) {
    std::vector<uintptr_t> results;
    size_t pos = 0;
    while ((pos = text.find("0x", pos)) != std::string::npos) {
        size_t end = pos + 2;
        while (end < text.size() && std::isxdigit(static_cast<unsigned char>(text[end]))) {
            end++;
        }
        if (end > pos + 2) {
            std::string hex_str = text.substr(pos, end - pos);
            try {
                uintptr_t val = std::strtoull(hex_str.c_str(), nullptr, 16);
                if (val >= 0x10000) {
                    if (std::find(results.begin(), results.end(), val) == results.end()) {
                        results.push_back(val);
                    }
                }
            } catch (...) {}
        }
        pos = end;
    }
    return results;
}

void ViewDisasm::RenderDisassemblerView(uintptr_t& cur_addr, float height, bool standalone) {
    auto& bp = Debugger::BreakpointManager::Get();

    if (!standalone) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s",
                           L("x86-64 Disassembler (Live View):", "Desassemblador x86-64 (Visao em Tempo Real):", "Дизассемблер x86-64 (В реальном времени):"));
    }

    ImGui::SetNextItemWidth(150.0f);
    char addr_buf[32];
    std::snprintf(addr_buf, sizeof(addr_buf), "0x%llx", static_cast<unsigned long long>(cur_addr));
    auto ToRuntime = [](uintptr_t a) {
        return (a >= 0x00400000ULL && a < 0x800000000ULL) ? ((a - 0x00400000ULL) + 0x800000000ULL) : a;
    };
    const char* input_id = standalone ? "##MemviewDisasmAddr" : "##DisasmAddr";
    if (ImGui::InputText(input_id, addr_buf, sizeof(addr_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        cur_addr = ToRuntime(std::strtoull(addr_buf, nullptr, 16));
    }
    ImGui::SameLine();
    if (ImGui::Button(standalone ? "Go##mv" : "Go##disasm")) {
        cur_addr = ToRuntime(std::strtoull(addr_buf, nullptr, 16));
    }
    ImGui::SameLine();
    if (ImGui::Button(standalone ? "Paste##mv" : "Paste##disasm")) {
        const char* clip = SDL_GetClipboardText();
        if (clip && clip[0]) cur_addr = ToRuntime(std::strtoull(clip, nullptr, 16));
    }
    ImGui::SameLine();
    if (ImGui::Button(standalone ? "RIP##mv" : "Jump to RIP")) {
        const uintptr_t rip = bp.GetLastHitRip();
        if (rip) cur_addr = rip;
    }

    ImGui::SameLine();
    if (ImGui::Button(standalone ? "<<##dpgup_mv" : "<<##dpgup")) { for (int i = 0; i < 16; ++i) cur_addr = FindPreviousInstruction(cur_addr); }
    ImGui::SetItemTooltip("%s", L("Scroll back 16 instructions", "Recuar 16 instrucoes", "Назад на 16 инструкций"));
    ImGui::SameLine();
    if (ImGui::Button(standalone ? "<##dinstup_mv" : "<##dinstup")) cur_addr = FindPreviousInstruction(cur_addr);
    ImGui::SetItemTooltip("%s", L("Scroll back 1 instruction", "Recuar 1 instrucao", "Назад на 1 инструкцию"));
    ImGui::SameLine();
    if (ImGui::Button(standalone ? ">##dinstdn_mv" : ">##dinstdn")) cur_addr = FindNextInstruction(cur_addr);
    ImGui::SetItemTooltip("%s", L("Advance 1 instruction", "Avancar 1 instrucao", "Вперед на 1 инструкцию"));
    ImGui::SameLine();
    if (ImGui::Button(standalone ? ">>##dpgdn_mv" : ">>##dpgdn")) { for (int i = 0; i < 16; ++i) cur_addr = FindNextInstruction(cur_addr); }
    ImGui::SetItemTooltip("%s", L("Advance 16 instructions", "Avancar 16 instrucoes", "Вперед на 16 инструкций"));

    ImGui::SameLine();
    ImGui::Checkbox(L("Symbols", "Simbolos", "Символы"), &s_disasm_show_symbols);
    ImGui::SetItemTooltip("%s", L("Resolve function and global variable symbols in disassembly",
                                  "Resolver nomes de funcoes e variaveis globais no desassemblador",
                                  "Разрешать символы функций и глобальных переменных"));

    if (!patch_status_msg.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[OK] %s", patch_status_msg.c_str());
    }

    uint8_t code_chunk[512];
    const bool read_ok = Debugger::MemoryScanner::ReadMemory(cur_addr, code_chunk, sizeof(code_chunk));
    if (!read_ok) {
        ImGui::TextDisabled("%s (0x%012llx)",
                            L("Cannot read memory at this address (unmapped or protected page)",
                              "Nao foi possivel ler a memoria neste endereco (pagina nao mapeada ou protegida)",
                              "Невозможно прочитать память по этому адресу"),
                            static_cast<unsigned long long>(cur_addr));
        return;
    }

    const char* child_id = standalone ? "DisasmChildMV" : "DisasmChildTab";
    ImGui::BeginChild(child_id, ImVec2(0, height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel > 0.0f) {
            int steps = std::max(1, static_cast<int>(wheel));
            for (int i = 0; i < steps; ++i) cur_addr = FindPreviousInstruction(cur_addr);
        } else if (wheel < 0.0f) {
            int steps = std::max(1, static_cast<int>(-wheel));
            for (int i = 0; i < steps; ++i) cur_addr = FindNextInstruction(cur_addr);
        }
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            if (!s_selected_line_copy.empty()) {
                ImGui::SetClipboardText(s_selected_line_copy.c_str());
                UiManager::SetStatus(L("Copied selected instruction to clipboard", "Instrucao selecionada copiada", "Инструкция скопирована"));
            }
        }
    }

    const char* table_id = standalone ? "DisasmTableMV" : "DisasmTable";
    if (ImGui::BeginTable(table_id, 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, height))) {
        ImGui::TableSetupColumn("BP", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn(L("Address", "Endereco", "Адрес"), ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn(L("Bytes", "Bytes", "Байты"), ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn(L("Instruction", "Instrucao", "Инструкция"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(L("Actions", "Acoes", "Действия"), ImGuiTableColumnFlags_WidthFixed, 175.0f);
        ImGui::TableHeadersRow();

        uintptr_t cur_pc = cur_addr;
        size_t offset = 0;
        const uintptr_t last_rip = bp.GetLastHitRip();
        int inst_count = 0;
        const int max_insts = (height > 500.0f) ? 60 : ((height > 300.0f) ? 40 : 25);

        while (offset < sizeof(code_chunk) && inst_count < max_insts) {
            ZydisDecodedInstruction inst;
            ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
            const size_t rem_size = sizeof(code_chunk) - offset;
            const ZyanStatus status = Common::Decoder::Instance()->decodeInstruction(inst, operands, code_chunk + offset, rem_size);

            if (!ZYAN_SUCCESS(status)) {
                const bool is_selected_bad = (s_selected_disasm_addr != 0 && cur_pc == s_selected_disasm_addr);
                ImGui::TableNextRow();
                if (is_selected_bad) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(25, 75, 140, 210));
                ImGui::TableSetColumnIndex(0); ImGui::TextDisabled(" . ");
                ImGui::TableSetColumnIndex(1);
                char bad_pc_str[32];
                std::snprintf(bad_pc_str, sizeof(bad_pc_str), "0x%012llx", static_cast<unsigned long long>(cur_pc));
                ImGui::Text("%s", bad_pc_str);
                ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("%02X", code_chunk[offset]);
                ImGui::TableSetColumnIndex(3);
                char db_str[32];
                std::snprintf(db_str, sizeof(db_str), "db 0x%02X##db_%08llx", code_chunk[offset], static_cast<unsigned long long>(cur_pc));
                if (ImGui::Selectable(db_str, is_selected_bad, ImGuiSelectableFlags_SpanAllColumns)) {
                    s_selected_disasm_addr = cur_pc;
                    char bad_line[128];
                    std::snprintf(bad_line, sizeof(bad_line), "%s  %02X  db 0x%02X", bad_pc_str, code_chunk[offset], code_chunk[offset]);
                    s_selected_line_copy = bad_line;
                }
                ImGui::TableSetColumnIndex(4);
                offset += 1; cur_pc += 1; inst_count++;
                continue;
            }

            const size_t inst_len = (inst.length > 0 && inst.length <= 15) ? inst.length : 1;
            const bool is_rip = (last_rip != 0 && cur_pc == last_rip);
            const bool is_selected = (s_selected_disasm_addr != 0 && cur_pc == s_selected_disasm_addr);
            const bool is_patched = (patched_instructions.find(cur_pc) != patched_instructions.end());
            const bool has_bp = bp.HasBreakpoint(cur_pc);

            ImGui::TableNextRow();
            if (is_rip) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(80, 20, 20, 200));
            } else if (is_selected) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(25, 75, 140, 210));
            } else if (is_patched) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(20, 60, 40, 180));
            }

            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(offset));
            if (has_bp) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Selectable("[●]", false, ImGuiSelectableFlags_None, ImVec2(24, 0))) bp.RemoveBreakpoint(cur_pc);
                ImGui::PopStyleColor();
            } else {
                if (ImGui::Selectable(" . ", false, ImGuiSelectableFlags_None, ImVec2(24, 0))) bp.AddBreakpoint(cur_pc, "Disasm BP");
            }
            ImGui::PopID();

            std::string raw_text = Common::Decoder::Instance()->disassembleInst(inst, operands, cur_pc);
            std::string text = s_disasm_show_symbols ? Debugger::SymbolManager::Get().AnnotateInstruction(raw_text, cur_pc) : raw_text;

            char pc_str[32];
            std::snprintf(pc_str, sizeof(pc_str), "0x%012llx", static_cast<unsigned long long>(cur_pc));

            char bytes_str[64] = "";
            int bpos = 0;
            for (size_t b = 0; b < inst_len; ++b) {
                bpos += std::snprintf(bytes_str + bpos, sizeof(bytes_str) - bpos, "%02X ", code_chunk[offset + b]);
            }

            char full_line_buf[384];
            std::snprintf(full_line_buf, sizeof(full_line_buf), "%s  %-24s  %s", pc_str, bytes_str, text.c_str());

            ImGui::TableSetColumnIndex(1);
            std::string exact_sym = s_disasm_show_symbols ? Debugger::SymbolManager::Get().GetExactName(cur_pc) : "";
            const char* prefix = is_rip ? "=> " : (is_selected ? ">  " : (is_patched ? "*  " : "   "));

            if (is_rip) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s%s", prefix, pc_str);
            } else if (is_selected) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 1.0f, 1.0f), "%s%s", prefix, pc_str);
            } else if (is_patched) {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "%s%s", prefix, pc_str);
            } else if (!exact_sym.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s<%s>", prefix, exact_sym.c_str());
            } else {
                ImGui::Text("%s%s", prefix, pc_str);
            }

            if (ImGui::IsItemHovered()) {
                std::string resolved_sym = Debugger::SymbolManager::Get().Resolve(cur_pc, true, 0x1000);
                if (!resolved_sym.empty()) {
                    ImGui::SetTooltip("%s (0x%012llx)\nDouble-click to patch bytes", resolved_sym.c_str(), static_cast<unsigned long long>(cur_pc));
                } else {
                    ImGui::SetTooltip("0x%012llx\nDouble-click to patch bytes", static_cast<unsigned long long>(cur_pc));
                }
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                ModalPatchInst::Open(cur_pc, inst_len, code_chunk + offset, raw_text);
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", bytes_str);

            ImGui::TableSetColumnIndex(3);
            char row_label[256];
            std::snprintf(row_label, sizeof(row_label), "%s##row_%08llx", text.c_str(), static_cast<unsigned long long>(cur_pc));
            bool row_selected = is_selected;
            bool row_clicked = ImGui::Selectable(row_label, row_selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns);
            if (row_clicked) {
                s_selected_disasm_addr = cur_pc;
                s_selected_line_copy = full_line_buf;
            }
            if ((row_clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ||
                (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
                ModalPatchInst::Open(cur_pc, inst_len, code_chunk + offset, raw_text);
            }

            if (ImGui::BeginPopupContextItem()) {
                s_selected_disasm_addr = cur_pc;
                s_selected_line_copy = full_line_buf;

                std::string sym_name = Debugger::SymbolManager::Get().Resolve(cur_pc, true, 0x1000);
                if (!sym_name.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s (0x%012llx)", sym_name.c_str(), static_cast<unsigned long long>(cur_pc));
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "0x%012llx", static_cast<unsigned long long>(cur_pc));
                }
                ImGui::Separator();

                if (ImGui::MenuItem(L("Copy Address", "Copiar Endereco", "Копировать адрес"), "Ctrl+C")) {
                    ImGui::SetClipboardText(pc_str);
                }
                if (ImGui::MenuItem(L("Copy Instruction", "Copiar Instrucao", "Копировать инструкцию"))) {
                    ImGui::SetClipboardText(text.c_str());
                }
                if (ImGui::MenuItem(L("Copy Hex Bytes", "Copiar Bytes Hex", "Копировать байты Hex"))) {
                    ImGui::SetClipboardText(bytes_str);
                }
                if (ImGui::MenuItem(L("Copy Full Line", "Copiar Linha Completa", "Копировать всю строку"))) {
                    ImGui::SetClipboardText(full_line_buf);
                }
                ImGui::Separator();

                if (ImGui::MenuItem(L("Follow in Hex View", "Seguir na Visao Hex", "Перейти в Hex"))) {
                    ViewMemViewer::NavigateHex(cur_pc);
                }

                std::vector<uintptr_t> targets = ExtractAddressesFromText(raw_text);
                if (!targets.empty()) {
                    ImGui::Separator();
                    for (uintptr_t target : targets) {
                        std::string t_sym = Debugger::SymbolManager::Get().Resolve(target, true, 0x1000);
                        char target_hex_lbl[128];
                        char target_disasm_lbl[128];
                        if (!t_sym.empty()) {
                            std::snprintf(target_hex_lbl, sizeof(target_hex_lbl), "%s: 0x%llx <%s>",
                                          L("Follow Target in Hex", "Seguir Alvo no Hex", "В Hex по цели"),
                                          static_cast<unsigned long long>(target), t_sym.c_str());
                            std::snprintf(target_disasm_lbl, sizeof(target_disasm_lbl), "%s: 0x%llx <%s>",
                                          L("Follow Target in Disasm", "Seguir Alvo no Disasm", "В Дизасм по цели"),
                                          static_cast<unsigned long long>(target), t_sym.c_str());
                        } else {
                            std::snprintf(target_hex_lbl, sizeof(target_hex_lbl), "%s: 0x%llx",
                                          L("Follow Target in Hex", "Seguir Alvo no Hex", "В Hex по цели"),
                                          static_cast<unsigned long long>(target));
                            std::snprintf(target_disasm_lbl, sizeof(target_disasm_lbl), "%s: 0x%llx",
                                          L("Follow Target in Disasm", "Seguir Alvo no Disasm", "В Дизасм по цели"),
                                          static_cast<unsigned long long>(target));
                        }
                        if (ImGui::MenuItem(target_hex_lbl)) {
                            ViewMemViewer::NavigateHex(target);
                        }
                        if (ImGui::MenuItem(target_disasm_lbl)) {
                            cur_addr = target;
                            ViewDisasm::SetDisasmAddress(target);
                        }
                    }
                }

                const auto regs = bp.GetLastRegisters();
                if (regs.valid) {
                    ImGui::Separator();
                    if (ImGui::BeginMenu(L("Follow Register...", "Seguir Registrador...", "Перейти по регистру..."))) {
                        auto ShowRegMenu = [&](const char* name, uint64_t val) {
                            if (val == 0) return;
                            char lbl[64];
                            std::string r_sym = Debugger::SymbolManager::Get().Resolve(val, true, 0x1000);
                            if (!r_sym.empty()) {
                                std::snprintf(lbl, sizeof(lbl), "%s: 0x%llx <%s>", name, static_cast<unsigned long long>(val), r_sym.c_str());
                            } else {
                                std::snprintf(lbl, sizeof(lbl), "%s: 0x%llx", name, static_cast<unsigned long long>(val));
                            }
                            if (ImGui::BeginMenu(lbl)) {
                                if (ImGui::MenuItem(L("Follow in Hex", "Seguir no Hex", "В Hex"))) {
                                    ViewMemViewer::NavigateHex(val);
                                }
                                if (ImGui::MenuItem(L("Follow in Disasm", "Seguir no Disasm", "В Дизасм"))) {
                                    cur_addr = val;
                                    ViewDisasm::SetDisasmAddress(val);
                                }
                                ImGui::EndMenu();
                            }
                        };

                        ShowRegMenu("RAX", regs.rax);
                        ShowRegMenu("RBX", regs.rbx);
                        ShowRegMenu("RCX", regs.rcx);
                        ShowRegMenu("RDX", regs.rdx);
                        ShowRegMenu("RSI", regs.rsi);
                        ShowRegMenu("RDI", regs.rdi);
                        ShowRegMenu("RBP", regs.rbp);
                        ShowRegMenu("RSP", regs.rsp);
                        ShowRegMenu("R8",  regs.r8);
                        ShowRegMenu("R9",  regs.r9);
                        ShowRegMenu("R10", regs.r10);
                        ShowRegMenu("R11", regs.r11);
                        ShowRegMenu("R12", regs.r12);
                        ShowRegMenu("R13", regs.r13);
                        ShowRegMenu("R14", regs.r14);
                        ShowRegMenu("R15", regs.r15);
                        ShowRegMenu("RIP", regs.rip);
                        ImGui::EndMenu();
                    }
                }

                ImGui::Separator();
                if (ImGui::MenuItem(L("Edit Instruction / Patch Bytes...", "Editar Instrucao / Patch de Bytes...", "Редактировать байты..."))) {
                    ModalPatchInst::Open(cur_pc, inst_len, code_chunk + offset, raw_text);
                }
                if (is_patched && ImGui::MenuItem(L("Restore Original Bytes", "Restaurar Bytes Originais", "Восстановить байты"))) {
                    ViewDisasm::RestoreInstruction(cur_pc);
                    UiManager::SetStatus(L("Restored original instruction", "Instrucao original restaurada", "Восстановлена инструкция"));
                }
                if (ImGui::MenuItem(L("NOP Instruction", "Preencher com NOP (0x90)", "Заполнить NOP"))) {
                    ViewDisasm::NopInstruction(cur_pc);
                    UiManager::SetStatus(L("NOP applied", "NOP aplicado", "NOP применен"));
                }
                ImGui::Separator();
                if (ImGui::MenuItem(has_bp ? L("Remove Breakpoint", "Remover Breakpoint", "Удалить точку") : L("Add Breakpoint", "Adicionar Breakpoint", "Добавить точку"))) {
                    if (has_bp) bp.RemoveBreakpoint(cur_pc); else bp.AddBreakpoint(cur_pc, "Disasm BP");
                }
                ImGui::EndPopup();
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(static_cast<int>(cur_pc & 0xFFFF));
            if (ImGui::SmallButton(L("Edit", "Editar", "Правка"))) ModalPatchInst::Open(cur_pc, inst_len, code_chunk + offset, raw_text);
            ImGui::SameLine();
            if (is_patched) {
                if (ImGui::SmallButton(L("Restore", "Restaurar", "Вернуть"))) ViewDisasm::RestoreInstruction(cur_pc);
            } else {
                if (ImGui::SmallButton("NOP")) ViewDisasm::NopInstruction(cur_pc);
            }
            ImGui::PopID();

            offset += inst_len; cur_pc += inst_len; inst_count++;
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

} // namespace UI
