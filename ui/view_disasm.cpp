// SPDX-License-Identifier: GPL-2.0-or-later
#include "view_disasm.h"
#include "modal_patch_inst.h"
#include "ui_strings.h"
#include "view_memviewer.h"
#include "ui_manager.h"
#include "breakpoint.h"
#include "mem_scanner.h"
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
static std::string patch_status_msg;
static std::unordered_map<uintptr_t, std::vector<uint8_t>> patched_instructions;

uintptr_t ViewDisasm::GetDisasmAddress() { return disasm_current_addr; }
void ViewDisasm::SetDisasmAddress(uintptr_t addr) { disasm_current_addr = addr; }
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

    if (!patch_status_msg.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[OK] %s", patch_status_msg.c_str());
    }

    uint8_t code_chunk[256];
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
        const int max_insts = (height > 300.0f) ? 40 : 25;

        while (offset < sizeof(code_chunk) && inst_count < max_insts) {
            ZydisDecodedInstruction inst;
            ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
            const size_t rem_size = sizeof(code_chunk) - offset;
            const ZyanStatus status = Common::Decoder::Instance()->decodeInstruction(inst, operands, code_chunk + offset, rem_size);

            if (!ZYAN_SUCCESS(status)) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextDisabled(" . ");
                ImGui::TableSetColumnIndex(1); ImGui::Text("0x%012llx", static_cast<unsigned long long>(cur_pc));
                ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("%02X", code_chunk[offset]);
                ImGui::TableSetColumnIndex(3); ImGui::TextDisabled("db 0x%02X", code_chunk[offset]);
                ImGui::TableSetColumnIndex(4);
                offset += 1; cur_pc += 1; inst_count++;
                continue;
            }

            const size_t inst_len = (inst.length > 0 && inst.length <= 15) ? inst.length : 1;
            const bool is_rip = (last_rip != 0 && cur_pc == last_rip);
            const bool is_patched = (patched_instructions.find(cur_pc) != patched_instructions.end());
            const bool has_bp = bp.HasBreakpoint(cur_pc);

            ImGui::TableNextRow();
            if (is_rip) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(80, 20, 20, 180));
            else if (is_patched) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(20, 60, 40, 180));

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

            std::string text = Common::Decoder::Instance()->disassembleInst(inst, operands, cur_pc);

            ImGui::TableSetColumnIndex(1);
            char pc_str[32];
            std::snprintf(pc_str, sizeof(pc_str), "0x%012llx", static_cast<unsigned long long>(cur_pc));
            if (is_rip) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "=> %s", pc_str);
            else if (is_patched) ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "*  %s", pc_str);
            else ImGui::TextUnformatted(pc_str);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                ModalPatchInst::Open(cur_pc, inst_len, code_chunk + offset, text);
            }

            ImGui::TableSetColumnIndex(2);
            char bytes_str[64] = "";
            int bpos = 0;
            for (size_t b = 0; b < inst_len; ++b) {
                bpos += std::snprintf(bytes_str + bpos, sizeof(bytes_str) - bpos, "%02X ", code_chunk[offset + b]);
            }
            ImGui::TextDisabled("%s", bytes_str);

            ImGui::TableSetColumnIndex(3);
            char row_label[128];
            std::snprintf(row_label, sizeof(row_label), "%s##row_%08llx", text.c_str(), static_cast<unsigned long long>(cur_pc));
            bool row_clicked = ImGui::Selectable(row_label, false, ImGuiSelectableFlags_AllowDoubleClick);
            if ((row_clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ||
                (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
                ModalPatchInst::Open(cur_pc, inst_len, code_chunk + offset, text);
            }

            if (ImGui::BeginPopupContextItem()) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "0x%012llx", static_cast<unsigned long long>(cur_pc));
                ImGui::Separator();
                if (ImGui::MenuItem(L("Copy Address", "Copiar Endereco", "Копировать адрес"))) ImGui::SetClipboardText(pc_str);
                if (ImGui::MenuItem(L("Copy Instruction", "Copiar Instrucao", "Копировать инструкцию"))) ImGui::SetClipboardText(text.c_str());
                if (ImGui::MenuItem(L("Copy Hex Bytes", "Copiar Bytes Hex", "Копировать байты Hex"))) ImGui::SetClipboardText(bytes_str);
                if (ImGui::MenuItem(L("Copy Full Line", "Copiar Linha Completa", "Копировать всю строку"))) {
                    char full_line[256];
                    std::snprintf(full_line, sizeof(full_line), "%s  %-24s  %s", pc_str, bytes_str, text.c_str());
                    ImGui::SetClipboardText(full_line);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(L("Edit Instruction / Patch Bytes...", "Editar Instrucao / Patch de Bytes...", "Редактировать байты..."))) {
                    ModalPatchInst::Open(cur_pc, inst_len, code_chunk + offset, text);
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
                if (ImGui::MenuItem(L("Follow in Hex View", "Seguir na Visao Hex", "Перейти в Hex"))) ViewMemViewer::NavigateHex(cur_pc);
                ImGui::EndPopup();
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::PushID(static_cast<int>(cur_pc & 0xFFFF));
            if (ImGui::SmallButton(L("Edit", "Editar", "Правка"))) ModalPatchInst::Open(cur_pc, inst_len, code_chunk + offset, text);
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
