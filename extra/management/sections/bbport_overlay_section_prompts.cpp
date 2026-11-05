// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the button prompt layout selector -- which glyphs (PlayStation, Xbox, Switch, keyboard)
// the game's prompts are drawn with.
#include "bbport_overlay_sections.h"

#include "bbport_overlay_ui.h"

#include <imgui.h>

namespace BbOverlaySections {
namespace {

/// The layouts, in bbport.ini's prompt_layout order (BbSettings::PromptLayout).
const char* const kLayoutsEn[] = {"Auto (Detect Active Device)", "PlayStation (Original Default)",
                                 "Xbox (A / B / X / Y)", "Nintendo Switch (B / A / Y / X)",
                                 "Keyboard & Mouse"};
const char* const kLayoutsPt[] = {"Automático (Detectar Dispositivo)", "PlayStation (Padrão Original)",
                                 "Xbox (A / B / X / Y)", "Nintendo Switch (B / A / Y / X)",
                                 "Teclado & Mouse"};
const char* const kLayoutsRu[] = {"Авто (Определять устройство)", "PlayStation (По умолчанию)",
                                 "Xbox (A / B / X / Y)", "Nintendo Switch (B / A / Y / X)",
                                 "Клавиатура и мышь"};
constexpr int kLayoutCount = int(std::size(kLayoutsEn));

} // namespace

void RenderPromptLayoutSection(BbSettings::Values& s, int lang, bool& dirty) {
    ComboOptions(Tr(lang, "Button Prompts Layout", "Layout dos Botões (Prompts)",
                    "Раскладка подсказок кнопок"),
                 TrList(lang, kLayoutsEn, kLayoutsPt, kLayoutsRu), kLayoutCount,
                 s.prompt_layout, dirty);
    Hint(Tr(lang, "Choose which button glyphs appear in-game (Examine, Roll, Attack). Auto dynamically "
                  "switches between your active gamepad and keyboard.",
             "Escolhe quais ícones de botões aparecem no jogo (Examinar, Esquiva, Ataque). O modo "
             "Automático alterna dinamicamente entre seu controle e teclado.",
             "Выбирает подсказки кнопок в игре. Автоматический режим переключается между активным "
             "геймпадом и клавиатурой."));
}

} // namespace BbOverlaySections
