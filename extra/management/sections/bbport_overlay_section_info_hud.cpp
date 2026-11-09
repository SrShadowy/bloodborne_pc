// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the on-screen info HUD (playtime, area, hunter, co-op and deaths) section.
#include "bbport_overlay_sections.h"

#include "bbport_overlay_ui.h"

#include "bbport_game_data.h"

#include <imgui.h>

namespace BbOverlaySections {
namespace {

/// Screen positions of the info HUD, indexed by info_hud_pos.
const char* const kPositionsEn[] = {"Top-Left (Default)", "Top-Right (Below FPS)", "Bottom-Left",
                                    "Bottom-Right"};
const char* const kPositionsPt[] = {"Superior Esquerdo (Padrão)", "Superior Direito (Abaixo do FPS)",
                                    "Inferior Esquerdo", "Inferior Direito"};
const char* const kPositionsRu[] = {"Сверху слева (По умолчанию)", "Сверху справа (Под FPS)",
                                    "Снизу слева", "Снизу справа"};
constexpr int kPositionCount = int(std::size(kPositionsEn));

} // namespace

void RenderInfoHudSection(BbSettings::Values& s, int lang, bool& dirty) {
    ImGui::SeparatorText(Tr(lang, "On-Screen Info HUD (OSD)", "HUD de Informações em Tela (Info OSD)",
                            "Информационный оверлей (Info OSD)"));

    Checkbox(Tr(lang, "Enable Info HUD Overlay", "Ativar HUD de Informações", "Включить оверлей информации"),
             s.show_info_hud, dirty);
    Hint(Tr(lang, "Displays real-time game information (playtime, current location, character name, and co-op status).",
             "Exibe informações do jogo em tempo real (horas de jogo, mapa atual, nome do caçador e co-op).",
             "Показывает время игры, название локации, имя охотника и статус коопа."));

    ImGui::BeginDisabled(!s.show_info_hud.load(std::memory_order_relaxed));

    ComboOptions(Tr(lang, "Screen Position", "Posição na Tela", "Позиция на экране"),
                 TrList(lang, kPositionsEn, kPositionsPt, kPositionsRu), kPositionCount,
                 s.info_hud_pos, dirty);

    Checkbox(Tr(lang, "Character Name & Level", "Nome e Nível do Personagem", "Имя и уровень охотника"),
             s.info_hud_show_name, dirty);
    Checkbox(Tr(lang, "Map / Area Name", "Nome do Mapa / Área", "Название локации"),
             s.info_hud_show_map, dirty);
    Checkbox(Tr(lang, "Playtime (Total & Session)", "Horas de Jogo (Total e Sessão)",
                 "Время игры (Всего и за сессию)"),
             s.info_hud_show_playtime, dirty);
    Checkbox(Tr(lang, "Co-op Status (Online)", "Status de Co-op (Online)", "Статус коопа (Сеть)"),
             s.info_hud_show_coop, dirty);
    Checkbox(Tr(lang, "Death Counter (Session)", "Contador de Mortes (Sessão)", "Счётчик смертей (Сессия)"),
             s.info_hud_show_deaths, dirty);

    ImGui::Spacing();
    if (ImGui::Button(Tr(lang, "Reset Death Counter", "Zerar Contador de Mortes",
                         "Сбросить счётчик смертей"))) {
        BbGameData::ResetDeathCounter();
    }
    Hint(Tr(lang, "Resets current session deaths.", "Zera o contador de mortes da sessão atual.",
             "Сбрасывает счётчик смертей текущей сессии."));

    ImGui::EndDisabled();
}

} // namespace BbOverlaySections
