// SPDX-License-Identifier: GPL-2.0-or-later
#include "modal_edit_watch.h"
#include "ui_strings.h"
#include "ui_manager.h"
#include "mem_scanner.h"

#include <imgui.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

namespace UI {

static bool s_open = false;
static bool s_first_frame = false;
static size_t s_index = 0;
static char s_label[64] = "";
static char s_address_buf[64] = "";
static char s_offsets_buf[128] = "";
static bool s_is_pointer = false;
static bool s_is_goldhen = false;
static int s_type = 2; // u32

static inline uintptr_t ConvertGoldHenAddress(uintptr_t addr) {
    return (addr >= 0x00400000ULL && addr < 0x800000000ULL) ? ((addr - 0x00400000ULL) + 0x800000000ULL) : addr;
}

static std::vector<int64_t> ParseOffsets(const char* str) {
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

void ModalEditWatch::Open(size_t index, const Debugger::WatchEntry& entry) {
    s_index = index;
    s_is_pointer = entry.is_pointer;
    s_type = static_cast<int>(entry.type);
    std::snprintf(s_label, sizeof(s_label), "%s", entry.label.c_str());
    std::snprintf(s_address_buf, sizeof(s_address_buf), "0x%llx", static_cast<unsigned long long>(entry.address));

    std::string offs_str;
    for (size_t i = 0; i < entry.offsets.size(); ++i) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%llx%s", static_cast<unsigned long long>(entry.offsets[i]), (i + 1 < entry.offsets.size()) ? ", " : "");
        offs_str += buf;
    }
    std::snprintf(s_offsets_buf, sizeof(s_offsets_buf), "%s", offs_str.c_str());

    s_first_frame = true;
    s_open = true;
}

bool ModalEditWatch::IsOpen() {
    return s_open;
}

void ModalEditWatch::Render() {
    if (!s_open) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        s_open = false;
        return;
    }

    if (s_first_frame) {
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowFocus();
        s_first_frame = false;
    }

    const char* title = L("Edit Watchlist Entry###ModalEditWatchDialog",
                          "Editar Entrada da Watchlist###ModalEditWatchDialog",
                          "Редактировать запись###ModalEditWatchDialog");

    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
    if (ImGui::Begin(title, &s_open, flags)) {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.55f, 1.0f), "%s [#%zu]",
                           L("Edit Address Information", "Editar Informacoes do Endereco", "Информация о записи"),
                           s_index + 1);
        ImGui::Separator();
        ImGui::Spacing();

        // 1. Description / Label
        ImGui::Text("%s:", L("Description / Name", "Descricao / Nome", "Описание / Имя"));
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("##edit_label", s_label, sizeof(s_label));

        ImGui::Spacing();

        // 2. Pointer Mode & GoldHEN Checkbox
        ImGui::Checkbox(L("Pointer with Offsets", "Ponteiro com Offsets", "Указатель со смещениями"), &s_is_pointer);
        ImGui::SameLine();
        ImGui::Checkbox(L("GoldHEN / PS4", "GoldHEN / PS4", "GoldHEN / PS4"), &s_is_goldhen);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", L("Convert PS4 address to PC runtime (+0x7FFC00000)", "Converter endereco PS4 para o runtime do PC (+0x7FFC00000)", "Конвертировать адрес PS4 в runtime"));

        ImGui::Spacing();

        // 3. Address fields
        if (!s_is_pointer) {
            ImGui::Text("%s:", L("Static Memory Address (Hex)", "Endereco de Memoria Fixo (Hex)", "Статический адрес (Hex)"));
            ImGui::SetNextItemWidth(240.0f);
            ImGui::InputText("##edit_addr", s_address_buf, sizeof(s_address_buf));
            ImGui::SameLine();
            if (ImGui::Button(L("Paste##addr", "Colar##addr", "Вставить##addr"))) {
                const char* clip = SDL_GetClipboardText();
                if (clip && clip[0]) std::snprintf(s_address_buf, sizeof(s_address_buf), "%s", clip);
            }
        } else {
            ImGui::Text("%s:", L("Base Pointer Address (Hex)", "Endereco Base do Ponteiro (Hex)", "Базовый адрес (Hex)"));
            ImGui::SetNextItemWidth(240.0f);
            ImGui::InputText("##edit_base", s_address_buf, sizeof(s_address_buf));
            ImGui::SameLine();
            if (ImGui::Button(L("Paste##base", "Colar##base", "Вставить##base"))) {
                const char* clip = SDL_GetClipboardText();
                if (clip && clip[0]) std::snprintf(s_address_buf, sizeof(s_address_buf), "%s", clip);
            }

            ImGui::Text("%s:", L("Offsets (comma separated, ex: 0x10, 0x20)", "Offsets (separados por virgula, ex: 0x10, 0x20)", "Смещения (напр: 0x10, 0x20)"));
            ImGui::SetNextItemWidth(320.0f);
            ImGui::InputText("##edit_offsets", s_offsets_buf, sizeof(s_offsets_buf));
        }

        ImGui::Spacing();

        // 4. Data Type
        ImGui::Text("%s:", L("Data Type", "Tipo de Dado", "Тип данных"));
        const char* type_names[] = { "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "Float", "Double" };
        ImGui::SetNextItemWidth(140.0f);
        ImGui::Combo("##edit_type", &s_type, type_names, IM_ARRAYSIZE(type_names));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 5. Live Preview
        uintptr_t target = 0;
        const auto& editor = Debugger::MemoryEditor::Get();
        const uintptr_t raw_addr = std::strtoull(s_address_buf, nullptr, 0);
        const uintptr_t eff_addr = s_is_goldhen ? ConvertGoldHenAddress(raw_addr) : raw_addr;

        if (s_is_pointer) {
            Debugger::WatchEntry tmp;
            tmp.address = eff_addr;
            tmp.is_pointer = true;
            tmp.offsets = ParseOffsets(s_offsets_buf);
            target = editor.ResolveAddress(tmp);
        } else {
            target = eff_addr;
        }

        if (target != 0) {
            double cur_num = 0;
            std::string cur_str;
            Debugger::MemoryScanner::ReadFormatted(target, static_cast<Debugger::DataType>(s_type), cur_num, cur_str);
            if (s_is_goldhen && raw_addr != eff_addr) {
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
                                   "[GoldHEN] PS4: 0x%llx -> Runtime: 0x%010llx | Target: 0x%010llx | %s: %s",
                                   static_cast<unsigned long long>(raw_addr), static_cast<unsigned long long>(eff_addr),
                                   static_cast<unsigned long long>(target), L("Value", "Valor", "Значение"), cur_str.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.40f, 0.95f, 0.55f, 1.0f),
                                   "[Preview] Target: 0x%010llx | %s: %s",
                                   static_cast<unsigned long long>(target),
                                   L("Live Value", "Valor Atual", "Значение"), cur_str.c_str());
            }
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f),
                               "[Preview] %s",
                               s_is_pointer ? L("Pointer chain unresolved or Null", "Cadeia de ponteiros invalida ou Nula", "Указатель не разрешен")
                                            : L("Invalid address", "Endereco invalido", "Неверный адрес"));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 6. Action buttons
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.35f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.70f, 0.45f, 1.0f));
        if (ImGui::Button(L("Save Changes", "Salvar Alteracoes", "Сохранить"), ImVec2(140, 28))) {
            Debugger::WatchEntry e;
            e.label = s_label;
            e.is_pointer = s_is_pointer;
            e.address = eff_addr;
            e.offsets = ParseOffsets(s_offsets_buf);
            e.type = static_cast<Debugger::DataType>(s_type);

            auto& ed = Debugger::MemoryEditor::Get();
            ed.UpdateEntry(s_index, e);
            ed.SaveTableBBT();

            UiManager::SetStatus(L("Watchlist entry updated and saved!", "Entrada da Watchlist atualizada e salva!", "Запись обновлена!"));
            s_open = false;
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        if (ImGui::Button(L("Cancel", "Cancelar", "Отмена"), ImVec2(100, 28))) {
            s_open = false;
        }
    }
    ImGui::End();
}

} // namespace UI
