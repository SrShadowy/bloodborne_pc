// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the disassembler's drawing -- the toolbar (address, RIP/eboot jumps, symbols) and the
// table of instructions from the current address, with one row highlighted per state: the last RIP,
// the selected instruction, a patched one, one with a breakpoint. Row actions are in
// view_disasm_menu.cpp.
#include "view_disasm_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <imgui.h>

#include "breakpoint.h"
#include "common/decoder.h"
#include "mem_scanner.h"
#include "modal_patch_inst.h"
#include "symbol_manager.h"
#include "ui_manager.h"
#include "ui_strings.h"
#include "view_disasm.h"
#include "view_memviewer.h"

namespace UI::DisasmDetail {
namespace {

/// The colours a row can take: the last RIP, the selection, a patched instruction, a breakpoint.
constexpr ImU32 kRipRowColor = IM_COL32(80, 20, 20, 200);
constexpr ImU32 kSelectedRowColor = IM_COL32(25, 75, 140, 210);
constexpr ImU32 kPatchedRowColor = IM_COL32(20, 60, 40, 180);

constexpr ImVec4 kTitleColor{0.4f, 0.8f, 1.0f, 1.0f};
constexpr ImVec4 kRipTextColor{1.0f, 0.4f, 0.4f, 1.0f};
constexpr ImVec4 kSelectedTextColor{0.4f, 1.0f, 1.0f, 1.0f};
constexpr ImVec4 kPatchedTextColor{0.4f, 1.0f, 0.6f, 1.0f};
constexpr ImVec4 kBreakpointTextColor{1.0f, 0.25f, 0.25f, 1.0f};
constexpr ImVec4 kSymbolTextColor{1.0f, 0.85f, 0.35f, 1.0f};
constexpr ImVec4 kOkColor{0.4f, 1.0f, 0.4f, 1.0f};

/// The markers a row's address carries.
const char* RowPrefix(bool is_rip, bool is_selected, bool is_patched, bool has_bp) {
    if (is_rip) {
        return "=> ";
    }
    if (is_selected) {
        return ">  ";
    }
    if (is_patched) {
        return "*  ";
    }
    return has_bp ? "●  " : "   ";
}

void FormatAddress(char* buf, std::size_t size, std::uintptr_t address) {
    std::snprintf(buf, size, "0x%012llx", static_cast<unsigned long long>(address));
}

/// How many rows a window of `height` shows.
int RowsForHeight(float height) {
    if (height > kTableHeightLarge) {
        return kRowsLarge;
    }
    return height > kTableHeightSmall ? kRowsMedium : kRowsSmall;
}

/// "90 90 CC ": the bytes of one instruction, hex and separated.
void FormatBytes(char* buf, std::size_t size, const std::uint8_t* code, std::size_t length) {
    int pos = 0;
    buf[0] = '\0';
    for (std::size_t b = 0; b < length && pos < int(size); ++b) {
        pos += std::snprintf(buf + pos, size - std::size_t(pos), "%02X ", code[b]);
    }
}

/// One undecodable byte: "db 0x2E", clickable like a row.
void RenderBadByte(std::uintptr_t address, std::uint8_t byte, bool is_selected) {
    ImGui::TableNextRow();
    if (is_selected) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kSelectedRowColor);
    }
    char pc_text[32];
    FormatAddress(pc_text, sizeof(pc_text), address);

    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(pc_text);

    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%02X", byte);

    ImGui::TableSetColumnIndex(2);
    char label[32];
    std::snprintf(label, sizeof(label), "db 0x%02X##db_%08llx", byte,
                  static_cast<unsigned long long>(address));
    if (ImGui::Selectable(label, is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
        char line[128];
        std::snprintf(line, sizeof(line), "%s  %02X  db 0x%02X", pc_text, byte, byte);
        GetState().selected = address;
        GetState().selected_line = line;
    }
}

/// One decoded instruction: address (or its symbol), the bytes, and the text; its menu opens on
/// right click, and a double click patches it.
void RenderInstructionRow(std::uintptr_t address, const std::uint8_t* code, std::size_t length,
                          const std::string& raw_text, bool is_rip, bool is_selected,
                          bool is_patched, bool has_bp, std::uintptr_t& followed_address) {
    ImGui::TableNextRow();
    if (is_rip) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kRipRowColor);
    } else if (is_selected) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kSelectedRowColor);
    } else if (is_patched) {
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, kPatchedRowColor);
    }

    const State& state = GetState();
    const std::string text =
        state.show_symbols ? Debugger::SymbolManager::Get().AnnotateInstruction(raw_text, address)
                           : raw_text;

    char pc_text[32];
    FormatAddress(pc_text, sizeof(pc_text), address);
    char bytes_text[64];
    FormatBytes(bytes_text, sizeof(bytes_text), code, length);

    char full_line[kRowTextBytes];
    std::snprintf(full_line, sizeof(full_line), "%s  %-24s  %s", pc_text, bytes_text, text.c_str());

    const std::string exact_sym =
        state.show_symbols ? Debugger::SymbolManager::Get().GetExactName(address) : "";
    const char* prefix = RowPrefix(is_rip, is_selected, is_patched, has_bp);

    // Column 0: the address, or the symbol that starts there.
    ImGui::TableSetColumnIndex(0);
    if (is_rip) {
        ImGui::TextColored(kRipTextColor, "%s%s", prefix, pc_text);
    } else if (is_selected) {
        ImGui::TextColored(kSelectedTextColor, "%s%s", prefix, pc_text);
    } else if (is_patched) {
        ImGui::TextColored(kPatchedTextColor, "%s%s", prefix, pc_text);
    } else if (has_bp) {
        ImGui::TextColored(kBreakpointTextColor, "%s%s", prefix, pc_text);
    } else if (!exact_sym.empty()) {
        ImGui::TextColored(kSymbolTextColor, "%s<%s>", prefix, exact_sym.c_str());
    } else {
        ImGui::Text("%s%s", prefix, pc_text);
    }

    if (ImGui::IsItemHovered()) {
        const std::string resolved = Debugger::SymbolManager::Get().Resolve(address, true, kSymbolSpan);
        const char* const hint = L("Double-click to patch bytes | Right-click for options",
                                   "Clique duplo para editar bytes | Botao direito p/ opcoes",
                                   "Двойной клик: патч | ПКМ: меню");
        if (!resolved.empty()) {
            ImGui::SetTooltip("%s (0x%012llx)\n%s", resolved.c_str(),
                              static_cast<unsigned long long>(address), hint);
        } else {
            ImGui::SetTooltip("0x%012llx\n%s", static_cast<unsigned long long>(address), hint);
        }
    }

    // Column 1: the encoding.
    ImGui::TableSetColumnIndex(1);
    ImGui::TextDisabled("%s", bytes_text);

    // Column 2: the instruction; the row is the click target.
    ImGui::TableSetColumnIndex(2);
    char row_label[256];
    std::snprintf(row_label, sizeof(row_label), "%s##row_%08llx", text.c_str(),
                  static_cast<unsigned long long>(address));
    const bool clicked =
        ImGui::Selectable(row_label, is_selected,
                          ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns);
    if (clicked) {
        GetState().selected = address;
        GetState().selected_line = full_line;
    }
    if ((clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ||
        (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
        ModalPatchInst::Open(address, length, code, raw_text);
    }

    if (ImGui::BeginPopupContextItem()) {
        GetState().selected = address;
        GetState().selected_line = full_line;
        DrawInstructionMenu(address, code, length, raw_text, pc_text, bytes_text, full_line, has_bp,
                            is_patched, followed_address);
        ImGui::EndPopup();
    }
}

} // namespace

bool RenderToolbar(std::uintptr_t& address, bool standalone,
                   std::uint8_t (&code)[kCodeChunkBytes]) {
    auto& bp = Debugger::BreakpointManager::Get();

    if (!standalone) {
        ImGui::TextColored(kTitleColor, "%s",
                           L("x86-64 Disassembler (Live View):",
                             "Desassemblador x86-64 (Visao em Tempo Real):",
                             "Дизассемблер x86-64 (В реальном времени):"));
    }

    ImGui::SetNextItemWidth(150.0f);
    char addr_buf[32];
    std::snprintf(addr_buf, sizeof(addr_buf), "0x%llx", static_cast<unsigned long long>(address));
    if (ImGui::InputText(standalone ? "##MemviewDisasmAddr" : "##DisasmAddr", addr_buf,
                         sizeof(addr_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        address = ToRuntime(std::strtoull(addr_buf, nullptr, 16));
    }

    ImGui::SameLine();
    if (ImGui::SmallButton(standalone ? "RIP##mv" : "Jump to RIP")) {
        const uintptr_t rip = bp.GetLastHitRip();
        if (rip) {
            address = rip;
        }
    }
    ImGui::SetItemTooltip("%s", L("Jump to the address of the latest captured execution/write",
                                  "Pular para o endereco da ultima execucao/escrita capturada",
                                  "Перейти к адресу последнего события"));

    ImGui::SameLine();
    if (ImGui::SmallButton(standalone ? "eboot##mv_d" : "eboot##disasm")) {
        address = ToRuntime(kEbootBase);
    }
    ImGui::SetItemTooltip("%s", L("Jump to game executable base (0x00400000)",
                                  "Pular para a base do executavel do jogo (0x00400000)",
                                  "Перейти к базе исполняемого файла игры (0x00400000)"));

    ImGui::SameLine();
    ImGui::Checkbox(L("Symbols", "Simbolos", "Символы"), &GetState().show_symbols);
    ImGui::SetItemTooltip("%s", L("Resolve function and global variable symbols in disassembly",
                                  "Resolver nomes de funcoes e variaveis globais no desassemblador",
                                  "Разрешать символы функций и глобальных переменных"));

    if (!GetState().patch_status.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kOkColor, "[OK] %s", GetState().patch_status.c_str());
    }

    if (Debugger::MemoryScanner::ReadMemory(address, code, sizeof(code))) {
        return true;
    }
    ImGui::TextDisabled("%s (0x%012llx)",
                        L("Cannot read memory at this address (unmapped or protected page)",
                          "Nao foi possivel ler a memoria neste endereco (pagina nao mapeada ou protegida)",
                          "Невозможно прочитать память по этому адресу"),
                        static_cast<unsigned long long>(address));
    return false;
}

void RenderTable(std::uintptr_t& address, std::uint8_t* code, float height, bool standalone) {
    if (!ImGui::BeginTable(standalone ? "DisasmTableMV" : "DisasmTable", 3,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                           ImVec2(0, height))) {
        return;
    }
    ImGui::TableSetupColumn(L("Address", "Endereco", "Адрес"), ImGuiTableColumnFlags_WidthFixed,
                            160.0f);
    ImGui::TableSetupColumn(L("Bytes", "Bytes", "Байты"), ImGuiTableColumnFlags_WidthFixed, 130.0f);
    ImGui::TableSetupColumn(L("Instruction", "Instrucao", "Инструкция"),
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    const auto& bp = Debugger::BreakpointManager::Get();
    const std::uintptr_t last_rip = bp.GetLastHitRip();
    const State& state = GetState();
    const int max_rows = RowsForHeight(height);

    std::uintptr_t pc = address;
    std::size_t offset = 0;
    for (int row = 0; row < max_rows && offset < kCodeChunkBytes; ++row) {
        ZydisDecodedInstruction inst;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
        const ZyanStatus status =
            Common::Decoder::Instance()->decodeInstruction(inst, operands, code + offset,
                                                           kCodeChunkBytes - offset);
        const bool is_selected = state.selected != 0 && pc == state.selected;
        if (!ZYAN_SUCCESS(status)) {
            RenderBadByte(pc, code[offset], is_selected);
            offset += 1;
            pc += 1;
            continue;
        }

        const std::size_t inst_len = InstructionLength(code + offset);
        const std::string raw_text = Common::Decoder::Instance()->disassembleInst(inst, operands, pc);
        RenderInstructionRow(pc, code + offset, inst_len, raw_text, last_rip != 0 && pc == last_rip,
                             is_selected, state.patched.find(pc) != state.patched.end(),
                             bp.HasBreakpoint(pc), address);
        offset += inst_len;
        pc += inst_len;
    }
    ImGui::EndTable();
}

} // namespace UI::DisasmDetail
