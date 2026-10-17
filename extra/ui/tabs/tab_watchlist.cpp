// SPDX-License-Identifier: GPL-2.0-or-later
#include "tab_watchlist.h"
#include "modal_edit_watch.h"
#include "ui_strings.h"
#include "mem_editor.h"
#include "cutscene_detector.h"
#include "breakpoint.h"
#include "tab_debugger.h"
#include "view_memviewer.h"
#include "ui_manager.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>

namespace UI {

static inline uintptr_t ConvertGoldHenAddress(uintptr_t addr) {
    return (addr >= 0x00400000ULL && addr < 0x800000000ULL) ? ((addr - 0x00400000ULL) + 0x800000000ULL) : addr;
}

static std::vector<int64_t> ParseOffsetsString(const char* str) {
    std::vector<int64_t> result;
    if (!str) return result;
    const char* p = str;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        char* end = nullptr;
        int64_t val = std::strtoll(p, &end, 0);
        if (end != p) { result.push_back(val); p = end; } else { ++p; }
    }
    return result;
}

static bool RenderRowContextMenu(size_t i, const Debugger::WatchEntry& e, uintptr_t resolved) {
    if (!ImGui::BeginPopupContextItem()) return false;
    bool deleted = false;
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "%s", e.label.c_str());
    ImGui::Separator();
    if (ImGui::MenuItem(L("Edit Record / Offsets...", "Editar Informacoes / Offsets...", "Редактировать запись..."))) ModalEditWatch::Open(i, e);
    const uintptr_t target_act = (e.is_pointer && resolved != 0) ? resolved : e.address;
    char copy_buf[32]; std::snprintf(copy_buf, sizeof(copy_buf), "0x%012llx", static_cast<unsigned long long>(target_act));
    if (ImGui::MenuItem(L("Copy Target Address", "Copiar Endereco Alvo", "Копировать адрес"))) {
        ImGui::SetClipboardText(copy_buf); SDL_SetClipboardText(copy_buf);
        UiManager::SetStatus(std::string("Copied: ") + copy_buf);
    }
    if (ImGui::MenuItem(L("Who Writes? (Watchpoint)", "Quem Escreve? (Watchpoint)", "Кто пишет?"))) {
        Debugger::BreakpointManager::Get().SetWriteWatchpoint(target_act);
        TabDebugger::SetBreakpointAddress(target_act); TabDebugger::SetDisasmAddress(target_act);
        UiManager::RequestTab(TabId::Debugger);
    }
    if (ImGui::MenuItem(L("Inspect in Hex View", "Inspecionar na Visao Hex", "Открыть в Hex"))) ViewMemViewer::NavigateHex(target_act);
    ImGui::Separator();
    if (ImGui::MenuItem(L("Delete from Watchlist", "Excluir da Watchlist", "Удалить"))) {
        Debugger::MemoryEditor::Get().RemoveWatch(i); deleted = true;
    }
    ImGui::EndPopup();
    return deleted;
}

void TabWatchlist::Render() {
    auto& editor = Debugger::MemoryEditor::Get();
    auto& cutscene = Debugger::CutsceneDetector::Get();
    auto& bp = Debugger::BreakpointManager::Get();

    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "%s", L("Address Watchlist", "Tabela de Enderecos / Watchlist", "Таблица адресов"));
    ImGui::SameLine();
    const auto entries = editor.GetEntries();
    ImGui::TextDisabled("(%zu %s)", entries.size(), L("saved", "salvos", "сохранено"));
    ImGui::SameLine(ImGui::GetWindowWidth() - 320.0f);
    if (ImGui::SmallButton(L("Save .bbt", "Salvar .bbt", "Сохранить .bbt"))) {
        UiManager::SetStatus(editor.SaveTableBBT() ? L("Table saved to default.bbt", "Tabela salva", "Таблица сохранена")
                                                   : L("[ERROR] Failed to save", "[ERRO] Falha ao salvar", "[ОШИБКА] Ошибка"));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Load .bbt", "Carregar .bbt", "Загрузить .bbt"))) {
        UiManager::SetStatus(editor.LoadTableBBT() ? L("Table loaded from default.bbt", "Tabela carregada", "Таблица загружена")
                                                   : L("[INFO] No table found", "[INFO] Nao encontrada", "[ИНФО] Файл не найден"));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Clear", "Limpar", "Очистить"))) {
        editor.Clear(); UiManager::SetStatus(L("Watchlist cleared", "Lista limpa", "Список очищен"));
    }
    ImGui::Separator();

    if (cutscene.IsEnabled()) {
        const uintptr_t cs_addr = cutscene.GetAddress();
        ImGui::TextColored(cutscene.IsCutsceneActive() ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(0.45f, 1.0f, 0.55f, 1.0f),
                           "[*] %s: [0x%012llx] -> %s", L("Cutscene Flag", "Flag de Cutscene", "Флаг кат-сцены"),
                           static_cast<unsigned long long>(cs_addr), cutscene.IsCutsceneActive() ? "CUTSCENE (1)" : "GAMEPLAY (0)");
        ImGui::SameLine();
        if (ImGui::SmallButton(L("Unlink", "Desvincular", "Отвязать"))) cutscene.SetEnabled(false);
        ImGui::Spacing();
    }

    if (entries.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", L("No saved addresses. Add matches from 'Scanner' tab or use the field below.",
                                    "Nenhum endereco salvo. Adicione resultados da aba 'Scanner' ou use o campo abaixo.", "Нет адресов."));
        ImGui::Spacing();
    } else if (ImGui::BeginTable("WatchlistTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 280))) {
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 32.0f);
        ImGui::TableSetupColumn(L("Description", "Descricao", "Описание"), ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableSetupColumn(L("Address / Pointer", "Endereco / Ponteiro", "Адрес / Указатель"), ImGuiTableColumnFlags_WidthFixed, 160.0f);
        ImGui::TableSetupColumn(L("Type", "Tipo", "Тип"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn(L("Current Value", "Valor Atual", "Текущее"), ImGuiTableColumnFlags_WidthFixed, 105.0f);
        ImGui::TableSetupColumn(L("Freeze", "Congelar", "Заморозка"), ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn(L("Actions", "Acoes", "Действия"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& e = entries[i];
            const uintptr_t resolved = editor.ResolveAddress(e);
            ImGui::TableNextRow(); ImGui::PushID(static_cast<int>(i));

            // Col 0: [X]
            ImGui::TableSetColumnIndex(0);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.18f, 0.18f, 0.70f));
            if (ImGui::SmallButton(" X ")) { editor.RemoveWatch(i); ImGui::PopStyleColor(); ImGui::PopID(); break; }
            ImGui::PopStyleColor();

            // Col 1: Label
            ImGui::TableSetColumnIndex(1);
            ImGui::Selectable(e.label.c_str(), false, ImGuiSelectableFlags_None);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) ModalEditWatch::Open(i, e);
            if (RenderRowContextMenu(i, e, resolved)) { ImGui::PopID(); break; }

            // Col 2: Address
            ImGui::TableSetColumnIndex(2);
            char addr_str[64];
            if (e.is_pointer) {
                if (resolved != 0) {
                    std::snprintf(addr_str, sizeof(addr_str), "[P] -> 0x%010llx", static_cast<unsigned long long>(resolved));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.80f, 0.35f, 1.0f));
                } else {
                    std::snprintf(addr_str, sizeof(addr_str), "[P] -> (Null/??)");
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.45f, 0.45f, 1.0f));
                }
            } else {
                std::snprintf(addr_str, sizeof(addr_str), "0x%012llx", static_cast<unsigned long long>(e.address));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.80f, 1.0f, 1.0f));
            }
            if (ImGui::Selectable(addr_str, false, ImGuiSelectableFlags_None)) {
                const uintptr_t target_c = (e.is_pointer && resolved != 0) ? resolved : e.address;
                char copy_buf[32]; std::snprintf(copy_buf, sizeof(copy_buf), "0x%012llx", static_cast<unsigned long long>(target_c));
                ImGui::SetClipboardText(copy_buf); SDL_SetClipboardText(copy_buf);
                UiManager::SetStatus(std::string("Copied: ") + copy_buf);
            }
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) ModalEditWatch::Open(i, e);
            if (RenderRowContextMenu(i, e, resolved)) { ImGui::PopID(); break; }

            // Col 3: Type
            ImGui::TableSetColumnIndex(3);
            const char* type_names[] = { "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "Float", "Double" };
            int type_idx = static_cast<int>(e.type);
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::Combo("##type", &type_idx, type_names, IM_ARRAYSIZE(type_names))) editor.SetType(i, static_cast<Debugger::DataType>(type_idx));

            // Col 4: Value / Write
            ImGui::TableSetColumnIndex(4);
            double cur_num = 0; std::string cur_str = "-";
            if (resolved != 0) Debugger::MemoryScanner::ReadFormatted(resolved, e.type, cur_num, cur_str);
            char edit_buf[32]; std::snprintf(edit_buf, sizeof(edit_buf), "%s", cur_str.c_str());
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::InputText("##val", edit_buf, sizeof(edit_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                editor.WriteValue(i, std::strtod(edit_buf, nullptr));
            }

            // Col 5: Freeze
            ImGui::TableSetColumnIndex(5);
            bool frozen = e.frozen;
            if (ImGui::Checkbox("##frz", &frozen)) { editor.SetFrozen(i, frozen); editor.SetFreezeValue(i, cur_num); }

            // Col 6: Actions
            ImGui::TableSetColumnIndex(6);
            const uintptr_t target_act = (resolved != 0) ? resolved : e.address;
            if (ImGui::SmallButton(L("Edit", "Editar", "Правка"))) ModalEditWatch::Open(i, e);
            ImGui::SameLine();
            if (ImGui::SmallButton(L("Who Writes?", "Quem Escreve?", "Кто пишет?"))) {
                bp.SetWriteWatchpoint(target_act);
                TabDebugger::SetBreakpointAddress(target_act); TabDebugger::SetDisasmAddress(target_act);
                UiManager::RequestTab(TabId::Debugger);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Hex")) ViewMemViewer::NavigateHex(target_act);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Bottom controls bar (+ Add Address / + Add Pointer / GoldHEN mode)
    ImGui::Spacing();
    static char manual_label[32] = "Item", manual_addr[32] = "0x", manual_offsets[64] = "";
    static int manual_type = 2; static bool is_pointer_mode = false, is_goldhen = false;

    ImGui::BeginGroup();
    ImGui::Checkbox(L("Pointer (Offsets)", "Ponteiro (Offsets)", "Указатель (смещения)"), &is_pointer_mode);
    ImGui::SameLine();
    ImGui::Checkbox(L("GoldHEN / PS4", "GoldHEN / PS4", "GoldHEN / PS4"), &is_goldhen);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L("Convert PS4/GoldHEN address to PC runtime (+0x7FFC00000)",
                                                          "Converter endereco PS4/GoldHEN para o runtime do PC (+0x7FFC00000)",
                                                          "Конвертировать адрес PS4 в runtime (+0x7FFC00000)"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputTextWithHint("##m_label", L("Label", "Nome", "Имя"), manual_label, sizeof(manual_label));
    ImGui::SameLine();

    if (!is_pointer_mode) {
        ImGui::SetNextItemWidth(125.0f);
        ImGui::InputTextWithHint("##m_addr", is_goldhen ? "PS4 0x..." : "0x...", manual_addr, sizeof(manual_addr));
        ImGui::SameLine();
        if (ImGui::Button(L("Paste##wl", "Colar##wl", "Вставить##wl"))) {
            const char* clip = SDL_GetClipboardText();
            if (clip && clip[0]) std::snprintf(manual_addr, sizeof(manual_addr), "%s", clip);
        }
    } else {
        ImGui::SetNextItemWidth(115.0f);
        ImGui::InputTextWithHint("##m_base", is_goldhen ? "Base PS4 0x..." : "Base 0x...", manual_addr, sizeof(manual_addr));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputTextWithHint("##m_offs", "Offsets: 0x10, 0x20", manual_offsets, sizeof(manual_offsets));
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    const char* type_names_short[] = { "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "Float", "Double" };
    ImGui::Combo("##m_type", &manual_type, type_names_short, IM_ARRAYSIZE(type_names_short));
    ImGui::SameLine();

    auto TargetAddr = [&](uintptr_t a) { return is_goldhen ? ConvertGoldHenAddress(a) : a; };

    if (!is_pointer_mode) {
        if (ImGui::Button(L("+ Add Address", "+ Adicionar Endereco", "+ Добавить"))) {
            const uintptr_t parsed = TargetAddr(std::strtoull(manual_addr, nullptr, 0));
            if (parsed) { editor.AddWatch(manual_label, parsed, static_cast<Debugger::DataType>(manual_type)); editor.SaveTableBBT(); }
        }
    } else {
        if (ImGui::Button(L("+ Add Pointer", "+ Adicionar Ponteiro", "+ Добавить указатель"))) {
            const uintptr_t base = TargetAddr(std::strtoull(manual_addr, nullptr, 0));
            if (base) { editor.AddPointerWatch(manual_label, base, ParseOffsetsString(manual_offsets), static_cast<Debugger::DataType>(manual_type)); editor.SaveTableBBT(); }
        }
    }

    // Live preview
    if (!is_pointer_mode && is_goldhen) {
        const uintptr_t raw = std::strtoull(manual_addr, nullptr, 0);
        const uintptr_t rt = ConvertGoldHenAddress(raw);
        if (raw != 0 && raw != rt) {
            double val = 0; std::string str_val;
            Debugger::MemoryScanner::ReadFormatted(rt, static_cast<Debugger::DataType>(manual_type), val, str_val);
            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "[GoldHEN] PS4: 0x%llx -> Runtime: 0x%010llx | %s: %s",
                               static_cast<unsigned long long>(raw), static_cast<unsigned long long>(rt),
                               L("Value", "Valor", "Значение"), str_val.c_str());
        }
    } else if (is_pointer_mode) {
        const uintptr_t raw_base = std::strtoull(manual_addr, nullptr, 0);
        const uintptr_t base_p = TargetAddr(raw_base);
        if (base_p != 0) {
            Debugger::WatchEntry test_e; test_e.address = base_p; test_e.is_pointer = true;
            test_e.offsets = ParseOffsetsString(manual_offsets);
            const uintptr_t test_res = editor.ResolveAddress(test_e);
            if (test_res != 0) {
                double val = 0; std::string str_val;
                Debugger::MemoryScanner::ReadFormatted(test_res, static_cast<Debugger::DataType>(manual_type), val, str_val);
                if (is_goldhen && raw_base != base_p) {
                    ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "[GoldHEN Ptr] Base PS4: 0x%llx -> Runtime: 0x%010llx -> Target: 0x%010llx = %s",
                                       static_cast<unsigned long long>(raw_base), static_cast<unsigned long long>(base_p),
                                       static_cast<unsigned long long>(test_res), str_val.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.40f, 0.95f, 0.55f, 1.0f), "Preview: Base [0x%llx] -> Target [0x%010llx] = %s",
                                       static_cast<unsigned long long>(base_p), static_cast<unsigned long long>(test_res), str_val.c_str());
                }
            } else {
                ImGui::TextColored(ImVec4(0.90f, 0.50f, 0.40f, 1.0f), "Preview: Base [0x%llx] -> (Pointer chain unmapped or null)",
                                   static_cast<unsigned long long>(base_p));
            }
        }
    }
    ImGui::EndGroup();
}

} // namespace UI
