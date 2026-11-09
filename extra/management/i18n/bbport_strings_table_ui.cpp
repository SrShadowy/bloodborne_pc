// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the prompt/keyboard help lines and the shared buttons (bbport_strings.cpp looks these up by StringId).
#include "bbport_strings_internal.h"

namespace BbStrings::detail {
namespace {

constexpr Entry kEntries[] = {
    {StringId::ShowFpsCorner,
     {"Show FPS in Corner", "Contador de FPS no Canto", "Счётчик FPS в углу"}},
    {StringId::PuddleReflections,
     {"Water Puddle Reflections", "Reflexos em Poças d'Água", "Отражения в лужах"}},
    {StringId::HintPuddleReflections,
     {"Disabled: fixes inverted buildings, flickering and streaks in the sky. Enabled: renders the game's planar reflections.", "Desativado: remove prédios invertidos, falhas e faixas no céu. Ativado: renderiza os reflexos planares originais.", "Выключено: убирает перевёрнутые здания, мерцание и полосы на небе. Включено: исходные плоские отражения."}},
    {StringId::DrawPipe,
     {"Draw Pipe (Two-Stage Pipeline)", "Draw Pipe (Pipeline em 2 Estágios)", "Двухстадийный конвейер (Draw Pipe)"}},
    {StringId::HintDrawPipe,
     {"Hybrid: keeps +20-30% FPS in gameplay, runs synchronously in cutscenes without glitches.", "Híbrido: mantém +20-30% FPS no gameplay e roda de forma síncrona em cutscenes sem falhas visuais.", "Гибридный: сохраняет +20-30% FPS в игре и работает синхронно в кат-сценах без визуальных сбоев."}},
    {StringId::DrawPipeModeOff,
     {"Off (Standard)", "Desligado (Modo Padrão)", "Выключен (Стандартный)"}},
    {StringId::DrawPipeModeOn,
     {"On (Always 2 stages)", "Ligado (Sempre 2 estágios)", "Включён (всегда 2 стадии)"}},
    {StringId::DrawPipeModeHybrid,
     {"Hybrid (Recommended)", "Híbrido (Recomendado)", "Гибридный (Рекомендуется)"}},
    {StringId::CloseButton,
     {"Close", "Fechar", "Закрыть"}},
    {StringId::SavedToIni,
     {"Settings saved to bbport.ini", "Configurações salvas em bbport.ini", "Настройки сохранены в bbport.ini"}},
    {StringId::PromptKeyboardHelp,
     {"Keyboard: type, Backspace = delete, Enter = OK, Esc = cancel", "Teclado: digite, Backspace = apagar, Enter = OK, Esc = cancelar", "Клавиатура: ввод, Backspace = удалить, Enter = OK, Esc = отмена"}},
    {StringId::KbSpace,
     {"Space", "Espaço", "Пробел"}},
    {StringId::KbBackspace,
     {"Delete", "Apagar", "Стереть"}},
    {StringId::KbClear,
     {"Clear", "Limpar", "Очистить"}},
    {StringId::KbConfirm,
     {"Confirm (OK)", "Confirmar (OK)", "Готово (OK)"}},
    {StringId::KbCancel,
     {"Cancel", "Cancelar", "Отмена"}},
    {StringId::KbCaps,
     {"Caps", "Caps", "Регистр"}},
    {StringId::PromptGamepadHelp,
     {"Gamepad: (X/A) Type  (Square/X) Delete  (Triangle/Y) Space  (L1/R1) Caps  (Start) OK  (Circle/B) Cancel", "Controle: (X/A) Inserir  (Quadrado/X) Apagar  (Triângulo/Y) Espaço  (L1/R1) Caps  (Start) OK  (Círculo/B) Cancelar", "Геймпад: (Крест/A) Ввод  (Квадрат/X) Удалить  (Треугольник/Y) Пробел  (L1/R1) Регистр  (Start) OK  (Круг/B) Отмена"}},
};

} // namespace

Table UiTable() {
    return kEntries;
}

} // namespace BbStrings::detail
