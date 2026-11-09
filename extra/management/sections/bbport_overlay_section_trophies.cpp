// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the trophy section -- progress by grade, the all/unlocked/locked filter, the trophy rows
// and the settings for the in-game list and notifications.
#include "bbport_overlay_sections.h"

#include <cstdio>
#include <vector>

#include "bbport_overlay_ui.h"
#include "bbport_trophies.h"

#include <imgui.h>

namespace BbOverlaySections {
namespace {

/// Which trophies the list shows.
enum Filter : int { FilterAll = 0, FilterUnlocked = 1, FilterLocked = 2 };

/// One trophy grade: its letter, name and the two colours the section uses for it (the counters
/// and the row tags were historically drawn in slightly different shades).
struct GradeInfo {
    char letter;
    const char* name_en;
    const char* name_pt;
    const char* name_ru;
    ImVec4 counter_color;
    ImVec4 tag_color;
};

constexpr GradeInfo kGrades[] = {
    {'P', "Platinum", "Platina", "Платина", Theme::kGradePlatinum, Theme::kGradeTagPlatinum},
    {'G', "Gold", "Ouro", "Золото", Theme::kGradeGold, Theme::kGradeGold},
    {'S', "Silver", "Prata", "Серебро", Theme::kGradeSilver, Theme::kGradeSilver},
    {'B', "Bronze", "Bronze", "Бронза", Theme::kGradeBronze, Theme::kGradeBronze},
};

const GradeInfo* FindGrade(char letter) {
    for (const GradeInfo& grade : kGrades) {
        if (grade.letter == letter) {
            return &grade;
        }
    }
    return nullptr;
}

/// One grade's counter, "[P] Platinum: 3/12".
void GradeCounter(int lang, const GradeInfo& grade, std::size_t unlocked, std::size_t total) {
    ImGui::TextColored(grade.counter_color, "[%c] %s: %zu/%zu", grade.letter,
                       Tr(lang, grade.name_en, grade.name_pt, grade.name_ru), unlocked, total);
}

/// The progress bar plus the four grade counters on one row.
void TrophyProgress(const BbTrophy::TrophyStats& stats, int lang, float scale) {
    const float fraction = static_cast<float>(stats.unlocked) / static_cast<float>(stats.total);
    char progress_text[64];
    std::snprintf(progress_text, sizeof(progress_text), "%zu / %zu (%.0f%%)",
                  stats.unlocked, stats.total, fraction * 100.0f);

    ImGui::Text("%s:", Tr(lang, "Total Progress", "Progresso Total", "Общий прогресс"));
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Theme::kTrophyProgressBar);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, Size::kTrophyProgressHeight * scale), progress_text);
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(Size::kTrophyGradeSpacingX * scale, Size::kTrophyGradeSpacingY * scale));

    GradeCounter(lang, kGrades[0], stats.platinum_unlocked, stats.platinum_total);
    ImGui::SameLine();
    GradeCounter(lang, kGrades[1], stats.gold_unlocked, stats.gold_total);
    ImGui::SameLine();
    GradeCounter(lang, kGrades[2], stats.silver_unlocked, stats.silver_total);
    ImGui::SameLine();
    GradeCounter(lang, kGrades[3], stats.bronze_unlocked, stats.bronze_total);

    ImGui::PopStyleVar();
}

/// One filter button; the active one is drawn in the highlight colour.
void FilterButton(const char* label, Filter filter, Filter& current) {
    const bool active = (current == filter);
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, Theme::kButtonFilterActive);
    }
    if (ImGui::Button(label)) {
        current = filter;
    }
    if (active) {
        ImGui::PopStyleColor();
    }
}

/// "All (12)", "Unlocked (3)", "Locked (9)".
void FilterButtons(const BbTrophy::TrophyStats& stats, int lang, Filter& filter) {
    const std::size_t locked = stats.total >= stats.unlocked ? stats.total - stats.unlocked : 0;
    char all_label[64];
    char unlocked_label[64];
    char locked_label[64];
    std::snprintf(all_label, sizeof(all_label), "%s (%zu)", Tr(lang, "All", "Todos", "Все"), stats.total);
    std::snprintf(unlocked_label, sizeof(unlocked_label), "%s (%zu)",
                  Tr(lang, "Unlocked", "Conquistados", "Полученные"), stats.unlocked);
    std::snprintf(locked_label, sizeof(locked_label), "%s (%zu)",
                  Tr(lang, "Locked", "Bloqueados", "Неполученные"), locked);

    FilterButton(all_label, FilterAll, filter);
    ImGui::SameLine();
    FilterButton(unlocked_label, FilterUnlocked, filter);
    ImGui::SameLine();
    FilterButton(locked_label, FilterLocked, filter);
}

/// A trophy row: grade tag, name (or "Hidden Trophy"), lock state and the description, over a
/// card coloured by the lock state.
void TrophyRow(const BbTrophy::TrophyListItem& item, int lang, bool reveal_hidden, float scale) {
    const bool hidden = !item.unlocked && item.hidden && !reveal_hidden;
    const float item_width = ImGui::GetContentRegionAvail().x;
    const ImU32 background =
        item.unlocked ? Theme::kTrophyUnlockedBackground : Theme::kTrophyLockedBackground;
    const ImU32 border = item.unlocked ? Theme::kTrophyUnlockedBorder : Theme::kTrophyLockedBorder;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->ChannelsSplit(2);
    draw_list->ChannelsSetCurrent(1);

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(item_width, Size::kTrophyCardTopPad * scale));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Size::kTrophyCardIndent * scale);
    ImGui::BeginGroup();

    const GradeInfo* grade = FindGrade(item.grade);
    if (grade != nullptr) {
        ImGui::TextColored(grade->tag_color, "[%s]",
                           Tr(lang, grade->name_en, grade->name_pt, grade->name_ru));
    } else {
        ImGui::TextColored(Theme::kGradeUnknown, "[?]");
    }
    ImGui::SameLine();

    if (hidden) {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTrophyHiddenText);
        ImGui::TextUnformatted(Tr(lang, "Hidden Trophy", "Troféu Oculto", "Скрытый трофей"));
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTrophyNameText);
        ImGui::TextUnformatted(item.name.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::SameLine(0.0f, Size::kTrophyStatusGap * scale);
    if (item.unlocked) {
        ImGui::TextColored(Theme::kTextUnlocked, "[%s]", Tr(lang, "Unlocked", "Conquistado", "Получен"));
    } else {
        ImGui::TextColored(Theme::kTextLocked, "[%s]", Tr(lang, "Locked", "Bloqueado", "Не получен"));
    }

    ImGui::PushStyleColor(ImGuiCol_Text, Theme::kTrophyDetailText);
    if (hidden) {
        ImGui::TextUnformatted(Tr(lang, "Keep playing to unlock this trophy.",
                                  "Continue jogando para revelar este troféu.",
                                  "Продолжайте играть, чтобы открыть этот трофей."));
    } else {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + item_width - Size::kTrophyDetailRightPad * scale);
        ImGui::TextWrapped("%s", item.detail.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::PopStyleColor();

    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(item_width, Size::kTrophyCardBottomPad * scale));
    ImGui::EndGroup();

    const ImVec2 card_min = ImGui::GetItemRectMin();
    const ImVec2 card_max = ImGui::GetItemRectMax();
    draw_list->ChannelsSetCurrent(0);
    draw_list->AddRectFilled(card_min, card_max, background, Size::kTrophyCardRounding * scale);
    draw_list->AddRect(card_min, card_max, border, Size::kTrophyCardRounding * scale);
    draw_list->ChannelsMerge();
}

/// The scrolling list itself; rows filtered out by the active filter are skipped.
void TrophyList(const std::vector<BbTrophy::TrophyListItem>& trophies, int lang, bool reveal_hidden,
                Filter filter, float scale) {
    if (!ImGui::BeginChild("##trophy_scroll_region", ImVec2(0.0f, 0.0f), false,
                           ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        return;
    }
    for (const auto& item : trophies) {
        if (filter == FilterUnlocked && !item.unlocked) {
            continue;
        }
        if (filter == FilterLocked && item.unlocked) {
            continue;
        }
        TrophyRow(item, lang, reveal_hidden, scale);
        ImGui::Spacing();
    }
    ImGui::EndChild();
}

} // namespace

void RenderTrophiesSection(BbSettings::Values& s, int lang, bool& dirty, float base_scale) {
    Checkbox(Tr(lang, "Show trophy list", "Exibir lista de troféus", "Показывать список трофеев"),
             s.show_trophy_list, dirty);
    Checkbox(Tr(lang, "In-game pop-up notifications", "Notificações na tela ao desbloquear",
                 "Всплывающие уведомления на экране"),
             s.show_trophy_toasts, dirty);

    const bool show_list = s.show_trophy_list.load(std::memory_order_relaxed);
    if (show_list) {
        Checkbox(Tr(lang, "Reveal hidden trophies (spoilers)", "Revelar troféus ocultos (spoilers)",
                     "Показывать скрытые трофеи (спойлеры)"),
                 s.show_hidden_trophies, dirty);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (!show_list) {
        ImGui::TextDisabled("%s", Tr(lang, "Trophy list display is hidden. Check the option above to view it.",
                                    "A exibição da lista de troféus está desativada. Marque a opção acima para visualizá-la.",
                                    "Список трофеев скрыт. Отметьте опцию выше для отображения."));
        return;
    }

    const BbTrophy::TrophyStats stats = BbTrophy::GetTrophyStats();
    const std::vector<BbTrophy::TrophyListItem> trophies = BbTrophy::GetTrophyList();

    if (stats.total > 0) {
        TrophyProgress(stats, lang, base_scale);
    }
    ImGui::Spacing();

    // The filter is remembered across menu openings, like the list's scroll position.
    static Filter filter = FilterAll;
    FilterButtons(stats, lang, filter);
    ImGui::Spacing();

    if (trophies.empty()) {
        ImGui::TextDisabled("%s", Tr(lang, "No trophies found or trophy key is missing.\n"
                                          "Enter your Trophy Key in the launcher or in the .env file.",
                                    "Nenhum troféu encontrado ou chave de troféu ausente.\n"
                                    "Insira sua chave de troféus (Trophy Key) no inicializador ou no arquivo .env.",
                                    "Трофеи не найдены или отсутствует ключ трофеев.\n"
                                    "Укажите ключ трофеев в лаунчере или в файле .env."));
        return;
    }

    TrophyList(trophies, lang, s.show_hidden_trophies.load(std::memory_order_relaxed), filter,
               base_scale);
}

} // namespace BbOverlaySections
