// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Game Effects and Misc sections (bbport_strings.cpp looks these up by StringId).
#include "bbport_strings_internal.h"

namespace BbStrings::detail {
namespace {

constexpr Entry kEntries[] = {
    {StringId::SectionGameEffects,
     {"Game Effects", "Efeitos do Jogo", "Эффекты игры"}},
    {StringId::LodCombo,
     {"Model Detail (LOD)", "Detalhes dos Modelos (LOD)", "Детализация моделей"}},
    {StringId::LodMax,
     {"Maximum (-2)", "Máximo (-2)", "Максимальная (-2)"}},
    {StringId::LodDefault,
     {"Game Default", "Padrão do Jogo", "Как в игре"}},
    {StringId::LodLower,
     {"Lower (1)", "Mais Baixo (1)", "Ниже (1)"}},
    {StringId::LodMin,
     {"Minimum (2)", "Mínimo (2)", "Минимальная (2)"}},
    {StringId::HintGameEffects,
     {"Model LOD, Chromatic Aberration, Depth of Field, SSAO and Native AA apply immediately in real time. Other startup patches apply after restart.", "LOD dos Modelos, Aberração Cromática, Profundidade de Campo, SSAO e AA Nativo aplicam-se imediatamente em tempo real. Outros patches exigem reinício.", "Детализация моделей (LOD), хроматическая аберрация, глубина резкости (DoF), SSAO и сглаживание применяются сразу в реальном времени."}},
    {StringId::HintSpecialControls,
     {"Free camera: hold Cross and press L3 (keyboard: Space + Z). Debug menu: left touchpad / Tab (needs DbgFont14h.ccm and DbgFont14h.tpf in dvdroot_ps4/font from Nexus #253). Right touchpad: Backspace.", "Câmera livre: segure Cross e pressione L3 (teclado: Espaço + Z). Menu de debug: touchpad esquerdo / Tab (requer fontes DbgFont14h no dvdroot_ps4/font). Touchpad direito: Backspace.", "Свободная камера: удерживайте Cross и нажимайте L3 (клавиатура: Space + Z). Debug menu: левый touchpad / Tab. Нужны DbgFont14h.ccm и DbgFont14h.tpf в dvdroot_ps4/font из мода Nexus #253. Правый touchpad: Backspace."}},
    {StringId::RestartWarning,
     {"Changes will apply after restarting the game", "As alterações serão aplicadas após reiniciar o jogo", "Изменения применятся после перезапуска игры"}},
    {StringId::ApplyAndRestart,
     {"Apply and Restart Game", "Aplicar e Reiniciar o Jogo", "Применить и перезапустить игру"}},
    {StringId::SectionMisc,
     {"Miscellaneous", "Diversos", "Прочее"}},
};

} // namespace

Table EffectsTable() {
    return kEntries;
}

} // namespace BbStrings::detail
