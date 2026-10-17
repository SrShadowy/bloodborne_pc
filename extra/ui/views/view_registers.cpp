// SPDX-License-Identifier: GPL-2.0-or-later
#include "view_registers.h"
#include "ui_strings.h"

#include <imgui.h>
#include <cstdio>

namespace UI {

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

    if (ImGui::BeginTable("RegTable", 4, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("RAX: 0x%016llx", static_cast<unsigned long long>(regs.rax));
        ImGui::TableSetColumnIndex(1); ImGui::Text("RBX: 0x%016llx", static_cast<unsigned long long>(regs.rbx));
        ImGui::TableSetColumnIndex(2); ImGui::Text("RCX: 0x%016llx", static_cast<unsigned long long>(regs.rcx));
        ImGui::TableSetColumnIndex(3); ImGui::Text("RDX: 0x%016llx", static_cast<unsigned long long>(regs.rdx));

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("RSI: 0x%016llx", static_cast<unsigned long long>(regs.rsi));
        ImGui::TableSetColumnIndex(1); ImGui::Text("RDI: 0x%016llx", static_cast<unsigned long long>(regs.rdi));
        ImGui::TableSetColumnIndex(2); ImGui::Text("RBP: 0x%016llx", static_cast<unsigned long long>(regs.rbp));
        ImGui::TableSetColumnIndex(3); ImGui::Text("RSP: 0x%016llx", static_cast<unsigned long long>(regs.rsp));

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("R8 : 0x%016llx", static_cast<unsigned long long>(regs.r8));
        ImGui::TableSetColumnIndex(1); ImGui::Text("R9 : 0x%016llx", static_cast<unsigned long long>(regs.r9));
        ImGui::TableSetColumnIndex(2); ImGui::Text("R10: 0x%016llx", static_cast<unsigned long long>(regs.r10));
        ImGui::TableSetColumnIndex(3); ImGui::Text("R11: 0x%016llx", static_cast<unsigned long long>(regs.r11));

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0); ImGui::Text("R12: 0x%016llx", static_cast<unsigned long long>(regs.r12));
        ImGui::TableSetColumnIndex(1); ImGui::Text("R13: 0x%016llx", static_cast<unsigned long long>(regs.r13));
        ImGui::TableSetColumnIndex(2); ImGui::Text("R14: 0x%016llx", static_cast<unsigned long long>(regs.r14));
        ImGui::TableSetColumnIndex(3); ImGui::Text("R15: 0x%016llx", static_cast<unsigned long long>(regs.r15));

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "RIP: 0x%016llx", static_cast<unsigned long long>(regs.rip));
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("RFL: 0x%08llx", static_cast<unsigned long long>(regs.rflags));
        ImGui::TableSetColumnIndex(2);
        if (ImGui::SmallButton(L("Copy Regs", "Copiar Regs", "Копировать"))) {
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
            ImGui::SetClipboardText(reg_text);
        }
        ImGui::TableSetColumnIndex(3);
        ImGui::EndTable();
    }
}

} // namespace UI
