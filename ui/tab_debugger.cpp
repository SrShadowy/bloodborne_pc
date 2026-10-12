// SPDX-License-Identifier: GPL-2.0-or-later
#include "tab_debugger.h"
#include "view_disasm.h"
#include "view_registers.h"
#include "ui_strings.h"
#include "breakpoint.h"
#include "mem_scanner.h"
#include "view_memviewer.h"
#include "ui_manager.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

namespace UI {

static char bp_addr_input[32] = "0x00400000";
static Debugger::CpuRegisters selected_regs{};
static bool has_selected_regs = false;

uintptr_t TabDebugger::GetDisasmAddress() { return ViewDisasm::GetDisasmAddress(); }
void TabDebugger::SetDisasmAddress(uintptr_t address) { ViewDisasm::SetDisasmAddress(address); }
void TabDebugger::SetBreakpointAddress(uintptr_t address) {
    std::snprintf(bp_addr_input, sizeof(bp_addr_input), "0x%llx", static_cast<unsigned long long>(address));
}
uintptr_t TabDebugger::FindPreviousInstruction(uintptr_t addr) { return ViewDisasm::FindPreviousInstruction(addr); }
uintptr_t TabDebugger::FindNextInstruction(uintptr_t addr) { return ViewDisasm::FindNextInstruction(addr); }
void TabDebugger::RenderDisassemblerView(uintptr_t& cur_addr, float height, bool standalone) {
    ViewDisasm::RenderDisassemblerView(cur_addr, height, standalone);
}

void TabDebugger::Render() {
    auto& bp = Debugger::BreakpointManager::Get();

    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "%s",
                       L("Debugger, Disassembler & Breakpoints",
                         "Depurador, Desassemblador & Breakpoints",
                         "Отладчик, дизассемблер и точки останова"));
    ImGui::Separator();

    // 1. Execution Control
    ImGui::Text("%s", L("Execution Control:", "Controle de Execucao:", "Управление выполнением:"));
    ImGui::SameLine();
    const bool paused = bp.IsPaused();
    const char* pause_btn = paused ? L("Resume Game", "Retomar Jogo", "Возобновить")
                                   : L("Pause Game", "Pausar Jogo", "Пауза");

    if (paused) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.45f, 0.15f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.55f, 0.20f, 1.0f));
    }
    if (ImGui::Button(pause_btn, ImVec2(130, 26))) bp.SetPaused(!paused);
    if (paused) ImGui::PopStyleColor(2);

    ImGui::SameLine();
    if (ImGui::Button(L("Step 1 Frame", "Avancar 1 Frame", "Кадр вперед"), ImVec2(130, 26))) bp.StepFrame();
    ImGui::SameLine();
    bool auto_pause = bp.GetAutoPauseOnHit();
    if (ImGui::Checkbox(L("Auto-pause game on hit", "Pausar jogo ao detectar escrita", "Пауза при записи"), &auto_pause)) {
        bp.SetAutoPauseOnHit(auto_pause);
    }
    ImGui::Spacing();

    // 2. Memory Write Watchpoint
    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "%s",
                       L("Write Watchpoint ('Find What Writes to this Address'):",
                         "Watchpoint de Escrita ('Quem Escreve Neste Endereco?'):",
                         "Точка наблюдения записи ('Кто пишет сюда?'):"));
    if (bp.HasActiveWatchpoint()) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.26f, 0.18f, 0.50f));
        if (ImGui::BeginChild("WatchBanner", ImVec2(0, 52), true, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(0.35f, 1.0f, 0.55f, 1.0f),
                               "[*] %s: 0x%012llx",
                               L("Active Watchpoint", "Watchpoint Ativo", "Активная точка"),
                               static_cast<unsigned long long>(bp.GetWatchedAddress()));
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.20f, 0.20f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
            if (ImGui::SmallButton(L("Stop Watching", "Parar Monitoramento", "Остановить"))) {
                bp.ClearWriteWatchpoint();
            }
            ImGui::PopStyleColor(2);

            ImGui::Text("%s: 0x%016llx  |  %s: %llu",
                        L("Live Value", "Valor ao Vivo", "Текущее значение"),
                        static_cast<unsigned long long>(bp.GetLastWatchedValue()),
                        L("Changes Detected", "Alteracoes Detectadas", "Изменений"),
                        static_cast<unsigned long long>(bp.GetValueChangeCount()));
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    } else {
        ImGui::TextDisabled("%s", L("No active watchpoint. Go to 'Watchlist' and click 'Who Writes?'.",
                                    "Nenhum watchpoint ativo. Va na aba 'Watchlist' e clique em 'Quem Escreve?'.",
                                    "Нет активных точек наблюдения. Перейдите в 'Таблицу' и нажмите 'Кто пишет?'."));
    }

    // 3. Captured Hits Table
    auto hits = bp.GetHits();
    uint64_t total_actions = 0;
    for (const auto& h : hits) {
        total_actions += h.count;
    }

    ImGui::Spacing();
    ImGui::Text(L("Captured Writes (Unique: %zu | Hits: %llu):",
                  "Escritas Capturadas (Unicas: %zu | Hits: %llu):",
                  "Инструкции записи (Уникальных: %zu | Всего: %llu):"),
                hits.size(), static_cast<unsigned long long>(total_actions));
    ImGui::SameLine(ImGui::GetWindowWidth() - 120.0f);
    if (ImGui::SmallButton(L("Clear Logs", "Limpar Registros", "Очистить"))) {
        bp.ClearHits();
        has_selected_regs = false;
        UiManager::SetStatus(L("Logs cleared", "Registros limpos", "Логи очищены"));
    }

    if (ImGui::BeginTable("HitsTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable, ImVec2(0, 150))) {
        ImGui::TableSetupColumn(L("Count", "Contagem", "Счетчик"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 75.0f);
        ImGui::TableSetupColumn(L("Instruction (RIP)", "Instrucao (RIP)", "Инструкция (RIP)"), ImGuiTableColumnFlags_WidthFixed, 145.0f);
        ImGui::TableSetupColumn(L("Disassembly", "Desassembly", "Дизассемблирование"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(L("Thread ID", "Thread ID", "ID потока"), ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn(L("Timestamp", "Timestamp", "Время"), ImGuiTableColumnFlags_WidthFixed, 85.0f);
        ImGui::TableSetupColumn(L("Actions", "Acoes", "Действия"), ImGuiTableColumnFlags_WidthFixed, 220.0f);
        ImGui::TableHeadersRow();

        ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs();
        if (sort_specs && sort_specs->SpecsCount > 0) {
            const auto& spec = sort_specs->Specs[0];
            std::sort(hits.begin(), hits.end(), [&](const Debugger::WatchpointHit& a, const Debugger::WatchpointHit& b) {
                bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);
                switch (spec.ColumnIndex) {
                case 0: return ascending ? (a.count < b.count) : (a.count > b.count);
                case 1: return ascending ? (a.rip < b.rip) : (a.rip > b.rip);
                case 2: return ascending ? (a.disassembly < b.disassembly) : (a.disassembly > b.disassembly);
                case 3: return ascending ? (a.thread_id < b.thread_id) : (a.thread_id > b.thread_id);
                case 4: return ascending ? (a.timestamp < b.timestamp) : (a.timestamp > b.timestamp);
                default: return false;
                }
            });
        } else {
            std::sort(hits.begin(), hits.end(), [](const Debugger::WatchpointHit& a, const Debugger::WatchpointHit& b) {
                return a.count > b.count;
            });
        }

        for (size_t i = 0; i < hits.size(); ++i) {
            const auto& hit = hits[i];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "%llu x", static_cast<unsigned long long>(hit.count));

            ImGui::TableSetColumnIndex(1);
            char rip_str[32];
            std::snprintf(rip_str, sizeof(rip_str), "0x%012llx", static_cast<unsigned long long>(hit.rip));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.5f, 1.0f));
            if (ImGui::Selectable(rip_str, false, ImGuiSelectableFlags_None)) {
                ImGui::SetClipboardText(rip_str);
                UiManager::SetStatus(std::string("Copied: ") + rip_str);
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L("Click to copy address", "Clique para copiar o endereco", "Нажмите для копирования"));

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", hit.disassembly.c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", hit.thread_id);

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%llu ms", static_cast<unsigned long long>(hit.timestamp % 1000000));

            ImGui::TableSetColumnIndex(5);
            ImGui::PushID(static_cast<int>(i));

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.38f, 0.58f, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.50f, 0.75f, 1.0f));
            if (ImGui::SmallButton("Disasm")) {
                ViewDisasm::SetDisasmAddress(hit.rip);
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.35f, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.60f, 0.45f, 1.0f));
            if (ImGui::SmallButton("Hex")) {
                ViewMemViewer::NavigateHex(hit.rip);
            }
            ImGui::PopStyleColor(2);

            ImGui::SameLine();
            const bool has_bp = bp.HasBreakpoint(hit.rip);
            if (has_bp) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.20f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.25f, 0.25f, 1.0f));
                if (ImGui::SmallButton("Del BP")) bp.RemoveBreakpoint(hit.rip);
                ImGui::PopStyleColor(2);
            } else {
                if (ImGui::SmallButton("+BP")) bp.AddBreakpoint(hit.rip, "Watchpoint trigger");
            }

            if (hit.registers.valid) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.30f, 0.60f, 0.75f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.58f, 0.38f, 0.78f, 1.0f));
                if (ImGui::SmallButton("Regs")) {
                    selected_regs = hit.registers;
                    has_selected_regs = true;
                }
                ImGui::PopStyleColor(2);
            }

            ImGui::SameLine();
            if (ViewDisasm::HasPatch(hit.rip)) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.40f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.65f, 0.50f, 1.0f));
                if (ImGui::SmallButton(L("Restore", "Restaurar", "Вернуть"))) {
                    ViewDisasm::RestoreInstruction(hit.rip);
                    UiManager::SetStatus(L("Restored instruction", "Instrucao restaurada", "Восстановлена инструкция"));
                }
                ImGui::PopStyleColor(2);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.20f, 0.20f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.25f, 0.25f, 1.0f));
                if (ImGui::SmallButton("NOP")) {
                    ViewDisasm::NopInstruction(hit.rip);
                    UiManager::SetStatus(L("NOP applied", "NOP aplicado", "NOP применен"));
                }
                ImGui::PopStyleColor(2);
            }

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // 4. CPU Registers Panel
    auto regs = has_selected_regs ? selected_regs : bp.GetLastRegisters();
    ViewRegisters::RenderRegistersTable(regs, has_selected_regs);

    ImGui::Spacing();
    ImGui::Separator();

    // 5. Interactive x86-64 Disassembler
    uintptr_t cur_disasm = ViewDisasm::GetDisasmAddress();
    ViewDisasm::RenderDisassemblerView(cur_disasm, 240.0f, false);
    ViewDisasm::SetDisasmAddress(cur_disasm);

    ImGui::Spacing();
    ImGui::Separator();

    // 6. Software Breakpoints List
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s",
                       L("Software Breakpoints (Code Execution):",
                         "Breakpoints de Codigo (Software Breakpoints):",
                         "Точки останова кода (Software Breakpoints):"));
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("##bp_addr_input", bp_addr_input, sizeof(bp_addr_input));
    ImGui::SameLine();
    if (ImGui::Button(L("Paste##bp", "Colar##bp", "Вставить##bp"))) {
        const char* clip = SDL_GetClipboardText();
        if (clip && clip[0]) std::snprintf(bp_addr_input, sizeof(bp_addr_input), "%s", clip);
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Add Breakpoint", "Adicionar Breakpoint", "Добавить"))) {
        const uintptr_t addr = std::strtoull(bp_addr_input, nullptr, 16);
        bp.AddBreakpoint(addr, "BP manual");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", L("Hex Address", "Endereco Hex", "Hex адрес"));

    const auto bps = bp.GetBreakpoints();
    for (const auto& b : bps) {
        ImGui::BulletText("0x%012llx: %s (%s: 0x%02X)",
                          static_cast<unsigned long long>(b.address),
                          b.label.c_str(),
                          L("Original", "Original", "Оригинал"),
                          b.original_byte);
        ImGui::SameLine();
        ImGui::PushID(reinterpret_cast<void*>(b.address));
        if (ImGui::SmallButton(L("Remove", "Remover", "Удалить"))) {
            bp.RemoveBreakpoint(b.address);
        }
        ImGui::PopID();
    }
}

} // namespace UI
