// SPDX-License-Identifier: GPL-2.0-or-later
#include "modal_patch_inst.h"
#include "view_disasm.h"
#include "ui_strings.h"
#include "ui_manager.h"
#include "mem_scanner.h"
#include "assembler.h"
#include "common/decoder.h"

#include <imgui.h>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <vector>

namespace UI {

static bool s_edit_modal_open = false;
static bool s_open_focus_requested = false;
static uintptr_t s_edit_target_addr = 0;
static size_t s_edit_orig_len = 0;
static std::vector<uint8_t> s_edit_orig_bytes;
static std::string s_edit_orig_disasm;
static char s_edit_asm_input[128] = "";
static std::string s_asm_error;
static char s_edit_hex_input[128] = "";
static bool s_edit_auto_nop = true;

static std::vector<uint8_t> ParseHexBytes(const char* input) {
    std::vector<uint8_t> bytes;
    std::string hex_chars;
    for (const char* p = input; *p; ++p) {
        if (std::isxdigit(static_cast<unsigned char>(*p))) hex_chars += *p;
    }
    for (size_t i = 0; i + 1 < hex_chars.size(); i += 2) {
        std::string b_str = hex_chars.substr(i, 2);
        bytes.push_back(static_cast<uint8_t>(std::strtoul(b_str.c_str(), nullptr, 16)));
    }
    return bytes;
}

void ModalPatchInst::Open(uintptr_t addr, size_t len, const uint8_t* orig_code, const std::string& disasm_text) {
    s_edit_target_addr = addr;
    s_edit_orig_len = len;
    s_edit_orig_bytes.assign(orig_code, orig_code + len);
    s_edit_orig_disasm = disasm_text;
    s_asm_error.clear();
    s_edit_asm_input[0] = '\0';
    if (!disasm_text.empty()) {
        std::snprintf(s_edit_asm_input, sizeof(s_edit_asm_input), "%s", disasm_text.c_str());
    }
    int pos = 0;
    s_edit_hex_input[0] = '\0';
    for (size_t i = 0; i < len; ++i) {
        pos += std::snprintf(s_edit_hex_input + pos, sizeof(s_edit_hex_input) - pos, "%02X ", orig_code[i]);
    }
    s_open_focus_requested = true;
    s_edit_modal_open = true;
}

void ModalPatchInst::Render() {
    if (!s_edit_modal_open) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
    if (s_open_focus_requested) {
        ImGui::SetNextWindowFocus();
        s_open_focus_requested = false;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        s_edit_modal_open = false;
        return;
    }

    const char* win_title = L("Edit Instruction Bytes###EditInstDialog",
                              "Editar Bytes da Instrucao###EditInstDialog",
                              "Редактировать байты инструкции###EditInstDialog");

    if (ImGui::Begin(win_title, &s_edit_modal_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "%s: 0x%012llx",
                           L("Edit Instruction Bytes", "Editar Bytes da Instrucao", "Редактировать байты инструкции"),
                           static_cast<unsigned long long>(s_edit_target_addr));
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled("%s: %s", L("Original", "Original", "Оригинал"), s_edit_orig_disasm.c_str());
        char orig_hex[64] = "";
        int pos = 0;
        for (uint8_t b : s_edit_orig_bytes) {
            pos += std::snprintf(orig_hex + pos, sizeof(orig_hex) - pos, "%02X ", b);
        }
        ImGui::TextDisabled("%s (%zu B): %s", L("Original Bytes", "Bytes Originais", "Исходные байты"), s_edit_orig_bytes.size(), orig_hex);
        ImGui::Spacing();

        ImGui::Text("%s:", L("Assembly (Intel x86-64)", "Assembly (Intel x86-64)", "Ассемблер (Intel x86-64)"));
        ImGui::SetNextItemWidth(300.0f);
        bool enter_pressed = ImGui::InputText("##AsmTextInput", s_edit_asm_input, sizeof(s_edit_asm_input), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        bool btn_assemble = ImGui::Button(L("Assemble", "Montar", "Собрать"));

        if (enter_pressed || btn_assemble) {
            std::vector<uint8_t> assembled;
            std::string err;
            if (Assembler::Assemble(s_edit_asm_input, s_edit_target_addr, assembled, err)) {
                s_asm_error.clear();
                int hpos = 0;
                s_edit_hex_input[0] = '\0';
                for (uint8_t b : assembled) {
                    hpos += std::snprintf(s_edit_hex_input + hpos, sizeof(s_edit_hex_input) - hpos, "%02X ", b);
                }
            } else {
                s_asm_error = err.empty() ? "Assembly failed" : err;
            }
        }

        if (!s_asm_error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[!] %s", s_asm_error.c_str());
        }

        ImGui::Spacing();
        ImGui::Text("%s:", L("New Bytes (Hex, space-separated)", "Novos Bytes (Hex, separados por espaco)", "Новые байты (Hex)"));
        ImGui::SetNextItemWidth(340.0f);
        ImGui::InputText("##NewHexBytesInput", s_edit_hex_input, sizeof(s_edit_hex_input));
        ImGui::Checkbox(L("Auto-pad with NOP (0x90) if smaller than original", "Preencher com NOP (0x90) se menor que original", "Заполнить NOP (0x90) если меньше оригинала"), &s_edit_auto_nop);

        auto parsed_bytes = ParseHexBytes(s_edit_hex_input);
        std::vector<uint8_t> preview_bytes = parsed_bytes;
        if (s_edit_auto_nop && preview_bytes.size() < s_edit_orig_len) {
            preview_bytes.resize(s_edit_orig_len, 0x90);
        }

        std::string preview_text;
        bool preview_valid = false;
        if (!preview_bytes.empty()) {
            ZydisDecodedInstruction inst;
            ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
            if (ZYAN_SUCCESS(Common::Decoder::Instance()->decodeInstruction(inst, operands, preview_bytes.data(), preview_bytes.size()))) {
                preview_text = Common::Decoder::Instance()->disassembleInst(inst, operands, s_edit_target_addr);
                if (preview_bytes.size() > inst.length) preview_text += " (+ NOPs)";
                preview_valid = true;
            }
        }

        ImGui::Spacing();
        ImGui::Text("%s:", L("Decoded Preview", "Previa Decodificada", "Предпросмотр"));
        if (preview_valid) {
            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.55f, 1.0f), "[OK] %s", preview_text.c_str());
        } else if (!parsed_bytes.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "[?] %s", L("Incomplete instruction byte sequence", "Sequencia de bytes incompleta", "Неполная последовательность"));
        } else {
            ImGui::TextDisabled("%s", L("Enter hex bytes above", "Digite bytes hex acima", "Введите байты выше"));
        }

        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button(L("Apply Patch", "Aplicar Patch", "Применить"), ImVec2(120, 28))) {
            if (!preview_bytes.empty()) {
                ViewDisasm::ApplyPatch(s_edit_target_addr, preview_bytes, s_edit_orig_bytes);
            }
            s_edit_modal_open = false;
        }
        ImGui::SameLine();
        if (ViewDisasm::HasPatch(s_edit_target_addr)) {
            if (ImGui::Button(L("Restore Original", "Restaurar Original", "Восстановить"), ImVec2(140, 28))) {
                ViewDisasm::RestoreInstruction(s_edit_target_addr);
                UiManager::SetStatus(L("Restored original bytes", "Bytes originais restaurados", "Восстановлены байты"));
                s_edit_modal_open = false;
            }
            ImGui::SameLine();
        }
        if (ImGui::Button(L("Cancel", "Cancelar", "Отмена"), ImVec2(90, 28))) {
            s_edit_modal_open = false;
        }
    }
    ImGui::End();
}

} // namespace UI
