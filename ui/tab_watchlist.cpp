// SPDX-License-Identifier: GPL-2.0-or-later
#include "tab_watchlist.h"
#include "ui_strings.h"
#include "mem_editor.h"
#include "cutscene_detector.h"
#include "breakpoint.h"
#include "tab_debugger.h"
#include "tab_hexview.h"
#include "ui_manager.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace UI {

static std::vector<int64_t> ParseOffsetsString(const char* str) {
    std::vector<int64_t> result;
    if (!str) return result;
    const char* p = str;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        char* end = nullptr;
        int64_t val = std::strtoll(p, &end, 0);
        if (end != p) {
            result.push_back(val);
            p = end;
        } else {
            ++p;
        }
    }
    return result;
}

void TabWatchlist::Render() {
    auto& editor = Debugger::MemoryEditor::Get();
    auto& cutscene = Debugger::CutsceneDetector::Get();
    auto& bp = Debugger::BreakpointManager::Get();

    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "%s",
                       L("Address Watchlist", "Tabela de Enderecos / Watchlist", "Таблица адресов"));
    ImGui::SameLine();
    const auto entries = editor.GetEntries();
    ImGui::TextDisabled("(%zu %s)", entries.size(), L("saved", "salvos", "сохранено"));

    // Table persistence buttons (.bbt / Bloodborne Table)
    ImGui::SameLine(ImGui::GetWindowWidth() - 320.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.40f, 0.35f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.52f, 0.45f, 0.95f));
    if (ImGui::SmallButton(L("Save .bbt", "Salvar .bbt", "Сохранить .bbt"))) {
        if (editor.SaveTableBBT()) {
            UiManager::SetStatus(L("Table saved to user/cheats/default.bbt",
                                   "Tabela salva em user/cheats/default.bbt",
                                   "Таблица сохранена в user/cheats/default.bbt"));
        } else {
            UiManager::SetStatus(L("[ERROR] Failed to save table",
                                   "[ERRO] Falha ao salvar tabela",
                                   "[ОШИБКА] Ошибка сохранения"));
        }
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.35f, 0.48f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.45f, 0.62f, 0.95f));
    if (ImGui::SmallButton(L("Load .bbt", "Carregar .bbt", "Загрузить .bbt"))) {
        if (editor.LoadTableBBT()) {
            UiManager::SetStatus(L("Table loaded from user/cheats/default.bbt",
                                   "Tabela carregada de user/cheats/default.bbt",
                                   "Таблица загружена из user/cheats/default.bbt"));
        } else {
            UiManager::SetStatus(L("[INFO] No table found in user/cheats/default.bbt",
                                   "[INFO] Nenhuma tabela encontrada em user/cheats/default.bbt",
                                   "[ИНФО] Файл не найден в user/cheats/default.bbt"));
        }
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    if (ImGui::SmallButton(L("Clear", "Limpar", "Очистить"))) {
        editor.Clear();
        UiManager::SetStatus(L("Watchlist cleared", "Lista limpa", "Список очищен"));
    }

    ImGui::Separator();

    // Cutscene flag status indicator
    if (cutscene.IsEnabled()) {
        const uintptr_t cs_addr = cutscene.GetAddress();
        const bool active = cutscene.IsCutsceneActive();
        const char* state_str = active ? L("CUTSCENE (1)", "CUTSCENE (1)", "КАТ-СЦЕНА (1)")
                                       : L("GAMEPLAY (0)", "GAMEPLAY (0)", "ГЕЙМПЛЕЙ (0)");
        const char* pipe_str = active ? L("Synchronous/Off", "Sincrono/Desligado", "Синхронно/Выкл")
                                      : L("Pipelined/On", "Acelerado/Ligado", "Ускорение/Вкл");

        ImGui::PushStyleColor(ImGuiCol_ChildBg, active ? ImVec4(0.35f, 0.12f, 0.12f, 0.50f) : ImVec4(0.12f, 0.30f, 0.15f, 0.50f));
        if (ImGui::BeginChild("CutsceneBanner", ImVec2(0, 36), true, ImGuiWindowFlags_NoScrollbar)) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(active ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
                               "%s: [0x%012llx] -> %s (DrawPipe %s)",
                               L("Active Cutscene Flag", "Flag de Cutscene Ativa", "Флаг кат-сcenы"),
                               static_cast<unsigned long long>(cs_addr),
                               state_str,
                               pipe_str);
            ImGui::SameLine();
            if (ImGui::SmallButton(L("Unlink", "Desvincular", "Отвязать"))) {
                cutscene.SetEnabled(false);
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    if (entries.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", L("No saved addresses. Add matches from 'Scanner' tab or use the field below.",
                                    "Nenhum endereco salvo. Adicione resultados da aba 'Scanner' ou use o campo abaixo.",
                                    "Нет сохраненных адресов. Добавьте результаты из вкладки 'Сканер' или поле ниже."));
        ImGui::Spacing();
    } else {
        if (ImGui::BeginTable("WatchlistTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 290))) {
            ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 32.0f);
            ImGui::TableSetupColumn(L("Description", "Descricao", "Описание"), ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn(L("Address / Pointer", "Endereco / Ponteiro", "Адрес / Указатель"), ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn(L("Type", "Tipo", "Тип"), ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn(L("Current Value", "Valor Atual", "Текущее"), ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn(L("Freeze", "Congelar", "Заморозка"), ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn(L("Actions", "Acoes", "Действия"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < entries.size(); ++i) {
                const auto& e = entries[i];
                const uintptr_t resolved = editor.ResolveAddress(e);
                ImGui::TableNextRow();

                ImGui::PushID(static_cast<int>(i));

                // 0. Remove button [X] at the very front
                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.18f, 0.18f, 0.70f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.22f, 0.22f, 0.95f));
                if (ImGui::SmallButton(" X ")) {
                    editor.RemoveWatch(i);
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                    break;
                }
                ImGui::PopStyleColor(2);

                // 1. Label
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", e.label.c_str());

                // 2. Address / Pointer resolution
                ImGui::TableSetColumnIndex(2);
                char addr_str[64];
                if (e.is_pointer) {
                    if (resolved != 0) {
                        std::snprintf(addr_str, sizeof(addr_str), "[P] -> 0x%010llx", static_cast<unsigned long long>(resolved));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.80f, 0.35f, 1.0f)); // Gold amber for pointers
                    } else {
                        std::snprintf(addr_str, sizeof(addr_str), "[P] -> (Null/??)");
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.45f, 0.45f, 1.0f)); // Soft red for unmapped
                    }
                } else {
                    std::snprintf(addr_str, sizeof(addr_str), "0x%012llx", static_cast<unsigned long long>(e.address));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.80f, 1.0f, 1.0f)); // Cyan
                }

                if (ImGui::Selectable(addr_str, false, ImGuiSelectableFlags_None)) {
                    const uintptr_t copy_target = (e.is_pointer && resolved != 0) ? resolved : e.address;
                    char copy_buf[32];
                    std::snprintf(copy_buf, sizeof(copy_buf), "0x%012llx", static_cast<unsigned long long>(copy_target));
                    ImGui::SetClipboardText(copy_buf);
                    SDL_SetClipboardText(copy_buf);
                    char msg[64];
                    std::snprintf(msg, sizeof(msg), "%s: %s",
                                  L("Copied", "Copiado", "Скопировано"), copy_buf);
                    UiManager::SetStatus(msg);
                }
                ImGui::PopStyleColor();

                if (ImGui::IsItemHovered()) {
                    if (e.is_pointer) {
                        char tip[256];
                        std::string chain_str;
                        for (size_t o = 0; o < e.offsets.size(); ++o) {
                            char obuf[32];
                            std::snprintf(obuf, sizeof(obuf), "+0x%llX ", static_cast<unsigned long long>(e.offsets[o]));
                            chain_str += obuf;
                        }
                        std::snprintf(tip, sizeof(tip),
                                      "Pointer Base: 0x%010llx\nOffsets: %s\nResolved: 0x%010llx\nClick to copy target address.",
                                      static_cast<unsigned long long>(e.address),
                                      chain_str.c_str(),
                                      static_cast<unsigned long long>(resolved));
                        ImGui::SetTooltip("%s", tip);
                    } else {
                        ImGui::SetTooltip("%s", L("Click to copy address", "Clique para copiar o endereco", "Нажмите для копирования"));
                    }
                }

                // 3. Type (interactive combo)
                ImGui::TableSetColumnIndex(3);
                const char* type_names[] = {
                    "u8", "u16", "u32", "u64",
                    "i8", "i16", "i32", "i64",
                    "Float", "Double"
                };
                int type_idx = static_cast<int>(e.type);
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::Combo("##type", &type_idx, type_names, IM_ARRAYSIZE(type_names))) {
                    editor.SetType(i, static_cast<Debugger::DataType>(type_idx));
                }

                // 4. Value / Edit
                ImGui::TableSetColumnIndex(4);
                double cur_num = 0;
                std::string cur_str = "-";
                if (resolved != 0) {
                    Debugger::MemoryScanner::ReadFormatted(resolved, e.type, cur_num, cur_str);
                }

                char edit_buf[32];
                std::snprintf(edit_buf, sizeof(edit_buf), "%s", cur_str.c_str());
                ImGui::SetNextItemWidth(90.0f);
                if (ImGui::InputText("##val", edit_buf, sizeof(edit_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                    const double new_val = std::strtod(edit_buf, nullptr);
                    editor.WriteValue(i, new_val);
                }

                // 5. Freeze
                ImGui::TableSetColumnIndex(5);
                bool frozen = e.frozen;
                if (ImGui::Checkbox("##frz", &frozen)) {
                    editor.SetFrozen(i, frozen);
                    editor.SetFreezeValue(i, cur_num);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", L("Freeze value (auto-rewritten 60x/sec)",
                                              "Congelar valor (reescrito 60x/seg)",
                                              "Заморозить значение (перезапись 60 раз/сек)"));
                }

                // 6. Actions (Who Writes? and Hex Inspector)
                ImGui::TableSetColumnIndex(6);
                const uintptr_t target_act = (resolved != 0) ? resolved : e.address;

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.32f, 0.50f, 0.80f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.42f, 0.65f, 0.95f));
                if (ImGui::SmallButton(L("Who Writes?", "Quem Escreve?", "Кто пишет?"))) {
                    const bool ok = bp.SetWriteWatchpoint(target_act);
                    char addr_buf[32];
                    std::snprintf(addr_buf, sizeof(addr_buf), "0x%012llx", static_cast<unsigned long long>(target_act));
                    ImGui::SetClipboardText(addr_buf);
                    SDL_SetClipboardText(addr_buf);

                    TabDebugger::SetBreakpointAddress(target_act);
                    TabDebugger::SetDisasmAddress(target_act);

                    UiManager::RequestTab(TabId::Debugger);
                    char msg[96];
                    if (ok) {
                        std::snprintf(msg, sizeof(msg), "[OK] Watching 0x%012llx", static_cast<unsigned long long>(target_act));
                    } else {
                        std::snprintf(msg, sizeof(msg), "[ERRO] Falha ao monitorar escrita em 0x%012llx", static_cast<unsigned long long>(target_act));
                    }
                    UiManager::SetStatus(msg);
                }
                ImGui::PopStyleColor(2);

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.50f, 0.80f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.58f, 0.65f, 0.95f));
                if (ImGui::SmallButton("Hex")) {
                    TabHexView::NavigateTo(target_act);
                    UiManager::RequestTab(TabId::HexView);
                }
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", L("Inspect in Hex View", "Inspecionar na Visao Hex", "Открыть в Hex"));
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    // Bottom controls bar
    ImGui::Spacing();
    static char manual_label[32] = "Item";
    static char manual_addr[32] = "0x";
    static char manual_offsets[64] = "0x8, 0x94";
    static int manual_type = 2; // u32
    static bool is_pointer_mode = false;

    ImGui::BeginGroup();
    ImGui::Checkbox(L("Pointer (Offsets)", "Ponteiro (Offsets)", "Указатель (смещения)"), &is_pointer_mode);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(95.0f);
    ImGui::InputTextWithHint("##m_label", L("Label", "Nome", "Имя"), manual_label, sizeof(manual_label));
    ImGui::SameLine();

    if (!is_pointer_mode) {
        ImGui::SetNextItemWidth(125.0f);
        ImGui::InputTextWithHint("##m_addr", "0x...", manual_addr, sizeof(manual_addr));
        ImGui::SameLine();
        if (ImGui::Button(L("Paste##wl", "Colar##wl", "Вставить##wl"))) {
            const char* clip = SDL_GetClipboardText();
            if (clip && clip[0]) {
                std::snprintf(manual_addr, sizeof(manual_addr), "%s", clip);
            }
        }
    } else {
        ImGui::SetNextItemWidth(115.0f);
        ImGui::InputTextWithHint("##m_base", "Base 0x...", manual_addr, sizeof(manual_addr));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputTextWithHint("##m_offs", "Offsets: 8, 94", manual_offsets, sizeof(manual_offsets));
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    const char* type_names_short[] = { "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "Float", "Double" };
    ImGui::Combo("##m_type", &manual_type, type_names_short, IM_ARRAYSIZE(type_names_short));
    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.50f, 0.35f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.65f, 0.45f, 0.95f));

    if (!is_pointer_mode) {
        if (ImGui::Button(L("+ Add Address", "+ Adicionar Endereco", "+ Добавить"))) {
            const uintptr_t parsed = std::strtoull(manual_addr, nullptr, 0);
            if (parsed) {
                editor.AddWatch(manual_label, parsed, static_cast<Debugger::DataType>(manual_type));
                editor.SaveTableBBT();
                UiManager::SetStatus(L("Address added to Watchlist!", "Endereco adicionado a Watchlist!", "Адрес добавлен в таблицу!"));
            }
        }
    } else {
        if (ImGui::Button(L("+ Add Pointer", "+ Adicionar Ponteiro", "+ Добавить указатель"))) {
            const uintptr_t base_parsed = std::strtoull(manual_addr, nullptr, 0);
            const auto offs = ParseOffsetsString(manual_offsets);
            if (base_parsed) {
                editor.AddPointerWatch(manual_label, base_parsed, offs, static_cast<Debugger::DataType>(manual_type));
                editor.SaveTableBBT();
                UiManager::SetStatus(L("Pointer added to Watchlist!", "Ponteiro adicionado a Watchlist!", "Указатель добавлен!"));
            }
        }
    }
    ImGui::PopStyleColor(2);

    // Live preview of pointer resolution if in pointer mode
    if (is_pointer_mode) {
        const uintptr_t base_p = std::strtoull(manual_addr, nullptr, 0);
        const auto offs = ParseOffsetsString(manual_offsets);
        if (base_p != 0) {
            Debugger::WatchEntry test_e;
            test_e.address = base_p;
            test_e.is_pointer = true;
            test_e.offsets = offs;
            const uintptr_t test_res = editor.ResolveAddress(test_e);
            if (test_res != 0) {
                double val = 0; std::string str_val;
                Debugger::MemoryScanner::ReadFormatted(test_res, static_cast<Debugger::DataType>(manual_type), val, str_val);
                ImGui::TextColored(ImVec4(0.40f, 0.95f, 0.55f, 1.0f),
                                   "Preview: Base [0x%llx] -> Target [0x%010llx] = %s",
                                   static_cast<unsigned long long>(base_p),
                                   static_cast<unsigned long long>(test_res),
                                   str_val.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.90f, 0.50f, 0.40f, 1.0f),
                                   "Preview: Base [0x%llx] -> (Pointer chain unmapped or null)",
                                   static_cast<unsigned long long>(base_p));
            }
        }
    }

    ImGui::EndGroup();
}

} // namespace UI
