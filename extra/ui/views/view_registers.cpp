// SPDX-License-Identifier: GPL-2.0-or-later
#include "view_registers.h"
#include "ui_strings.h"
#include "ui_manager.h"
#include "view_memviewer.h"
#include "view_disasm.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include <cstdio>

namespace UI {

static void SetClipboardSafe(const char* text) {
    if (!text) return;
    ImGui::SetClipboardText(text);
    SDL_SetClipboardText(text);
    SDL_SetPrimarySelectionText(text);
#if !defined(_WIN32)
    FILE* pipe = popen("wl-copy 2>/dev/null || xclip -selection clipboard 2>/dev/null", "w");
    if (pipe) {
        fputs(text, pipe);
        pclose(pipe);
    }
#endif
}

void ViewRegisters::RenderRegistersTable(const Debugger::CpuRegisters& regs, bool& has_selected_regs) {
    if (!ImGui::CollapsingHeader(L("CPU Registers (x86-64)", "Registradores da CPU (x86-64)", "Регистры CPU (x86-64)"), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    if (!regs.valid) {
        ImGui::TextDisabled("%s", L("No register snapshot available. Registers are captured when watchpoint triggers.",
                                    "Nenhum snapshot de registradores disponivel. Registradores sao capturados nos watchpoints.",
                                    "Нет данных регистров. Регистры сохраняются при срабатывании точек останова."));
        return;
    }

    if (has_selected_regs) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                           L("[Viewing snapshot from selected Hit]",
                             "[Visualizando snapshot do Hit selecionado]",
                             "[Просмотр снимка выбранного события]"));
        ImGui::SameLine();
        if (ImGui::SmallButton(L("Show Latest", "Mostrar Recente", "Последние"))) {
            has_selected_regs = false;
        }
    }

    auto RenderRegItem = [](const char* name, uint64_t val, bool is_rip = false) {
        char label[48];
        std::snprintf(label, sizeof(label), "%s: 0x%016llx", name, static_cast<unsigned long long>(val));
        if (is_rip) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", label);
        } else {
            ImGui::TextUnformatted(label);
        }

        if (ImGui::BeginPopupContextItem()) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s: 0x%016llx", name, static_cast<unsigned long long>(val));
            ImGui::Separator();
            char val_str[32];
            std::snprintf(val_str, sizeof(val_str), "0x%016llx", static_cast<unsigned long long>(val));
            if (ImGui::MenuItem(L("Copy Value", "Copiar Valor", "Копировать значение"))) {
                SetClipboardSafe(val_str);
                char status[64];
                std::snprintf(status, sizeof(status), "%s %s", name, L("copied", "copiado", "скопирован"));
                UiManager::SetStatus(status);
            }
            if (val >= 0x10000) {
                if (ImGui::MenuItem(L("Follow in Hex View", "Seguir na Visao Hex", "В Hex"))) {
                    ViewMemViewer::NavigateHex(val);
                }
                if (ImGui::MenuItem(L("Follow in Disasm View", "Seguir no Desassemblador", "В Дизасм"))) {
                    ViewMemViewer::NavigateDisasm(val);
                }
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s (0x%llx)\n%s", name, static_cast<unsigned long long>(val),
                              L("Right-click to copy or follow in Hex/Disasm",
                                "Botao direito para copiar ou seguir no Hex/Disasm",
                                "ПКМ для копирования или перехода"));
        }
    };

    if (ImGui::BeginTable("RegTable", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); RenderRegItem("RAX", regs.rax);
        ImGui::TableSetColumnIndex(1); RenderRegItem("RBX", regs.rbx);
        ImGui::TableSetColumnIndex(2); RenderRegItem("RCX", regs.rcx);
        ImGui::TableSetColumnIndex(3); RenderRegItem("RDX", regs.rdx);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); RenderRegItem("RSI", regs.rsi);
        ImGui::TableSetColumnIndex(1); RenderRegItem("RDI", regs.rdi);
        ImGui::TableSetColumnIndex(2); RenderRegItem("RBP", regs.rbp);
        ImGui::TableSetColumnIndex(3); RenderRegItem("RSP", regs.rsp);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); RenderRegItem("R8 ", regs.r8);
        ImGui::TableSetColumnIndex(1); RenderRegItem("R9 ", regs.r9);
        ImGui::TableSetColumnIndex(2); RenderRegItem("R10", regs.r10);
        ImGui::TableSetColumnIndex(3); RenderRegItem("R11", regs.r11);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); RenderRegItem("R12", regs.r12);
        ImGui::TableSetColumnIndex(1); RenderRegItem("R13", regs.r13);
        ImGui::TableSetColumnIndex(2); RenderRegItem("R14", regs.r14);
        ImGui::TableSetColumnIndex(3); RenderRegItem("R15", regs.r15);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        RenderRegItem("RIP", regs.rip, true);

        ImGui::TableSetColumnIndex(1);
        char rfl_label[32];
        std::snprintf(rfl_label, sizeof(rfl_label), "RFL: 0x%08llx", static_cast<unsigned long long>(regs.rflags));
        ImGui::TextUnformatted(rfl_label);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("RFLAGS: 0x%08llx", static_cast<unsigned long long>(regs.rflags));
        }

        ImGui::TableSetColumnIndex(2);
        static double s_last_copy_regs_time = 0.0;
        const bool recently_copied = (ImGui::GetTime() - s_last_copy_regs_time < 1.5);

        if (recently_copied) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.65f, 0.25f, 1.0f));
        }
        const char* btn_text = recently_copied ?
            L("[Copied!]", "[Copiado!]", "[Скопировано!]") :
            L("Copy Regs", "Copiar Regs", "Копировать");

        if (ImGui::SmallButton(btn_text)) {
            char reg_text[512];
            std::snprintf(reg_text, sizeof(reg_text),
                          "RAX=%016llx RBX=%016llx RCX=%016llx RDX=%016llx\n"
                          "RSI=%016llx RDI=%016llx RBP=%016llx RSP=%016llx\n"
                          "R8 =%016llx R9 =%016llx R10=%016llx R11=%016llx\n"
                          "R12=%016llx R13=%016llx R14=%016llx R15=%016llx\n"
                          "RIP=%016llx RFL=%08llx\n",
                          static_cast<unsigned long long>(regs.rax), static_cast<unsigned long long>(regs.rbx),
                          static_cast<unsigned long long>(regs.rcx), static_cast<unsigned long long>(regs.rdx),
                          static_cast<unsigned long long>(regs.rsi), static_cast<unsigned long long>(regs.rdi),
                          static_cast<unsigned long long>(regs.rbp), static_cast<unsigned long long>(regs.rsp),
                          static_cast<unsigned long long>(regs.r8),  static_cast<unsigned long long>(regs.r9),
                          static_cast<unsigned long long>(regs.r10), static_cast<unsigned long long>(regs.r11),
                          static_cast<unsigned long long>(regs.r12), static_cast<unsigned long long>(regs.r13),
                          static_cast<unsigned long long>(regs.r14), static_cast<unsigned long long>(regs.r15),
                          static_cast<unsigned long long>(regs.rip), static_cast<unsigned long long>(regs.rflags));
            SetClipboardSafe(reg_text);
            UiManager::SetStatus(L("CPU registers copied to clipboard", "Registradores copiados para a area de transferencia", "Регистры скопированы в буфер обмена"));
            s_last_copy_regs_time = ImGui::GetTime();
        }
        if (recently_copied) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", L("Click to copy all registers (RAX-R15, RIP, RFL) to clipboard",
                                      "Clique para copiar todos os registradores (RAX-R15, RIP, RFL) para a area de transferencia",
                                      "Скопировать все регистры в буфер обмена"));
        }

        ImGui::TableSetColumnIndex(3);
        ImGui::EndTable();
    }
}

} // namespace UI

