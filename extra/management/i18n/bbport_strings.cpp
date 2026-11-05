// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the localized text of the in-game settings menu and the prompt help lines. The rows live
// in bbport_strings_table_*.cpp (one per area of the menu), each tagged with its StringId; this file
// resolves them for the requested language.
#include "bbport_strings.h"

#include <cstddef>
#include <iterator>

#include "bbport_settings.h"
#include "bbport_strings_internal.h"

namespace BbStrings {
namespace {

using detail::Entry;
using detail::Table;
using detail::Text;

/// Every table, in lookup order. Adding a section means adding its table here.
const Table kTables[] = {
    detail::UpscalerTable(),
    detail::VisualsTable(),
    detail::OutputTable(),
    detail::EffectsTable(),
    detail::UiTable(),
    detail::CameraTable(),
};

/// The text of `id`, or null when a table has no row for it (a string that was never written).
const Text* FindText(StringId id) {
    for (const Table& table : kTables) {
        for (const Entry& entry : table) {
            if (entry.id == id) {
                return &entry.text;
            }
        }
    }
    return nullptr;
}

/// One language's string from a row, falling back to English and then to "".
const char* Pick(const Text* text, int lang) {
    if (text == nullptr) {
        return "";
    }
    const char* picked = nullptr;
    switch (lang) {
    case BbSettings::LangPortuguese:
        picked = text->pt;
        break;
    case BbSettings::LangRussian:
        picked = text->ru;
        break;
    case BbSettings::LangEnglish:
    default:
        break;
    }
    if (picked != nullptr) {
        return picked;
    }
    return text->en != nullptr ? text->en : "";
}

/// The Game Effects checkboxes, indexed like BbSettings::Effects.
constexpr detail::Text kEffectLabels[] = {
    {"Chromatic Aberration", "Aberração Cromática", "Хроматическая аберрация"},
    {"Depth of Field (DoF)", "Profundidade de Campo (DoF)", "Глубина резкости (DoF)"},
    {"Motion Blur", "Desfoque de Movimento", "Размытие в движении"},
    {"SSAO Ambient Occlusion", "Oclusão Ambiental SSAO", "Затенение SSAO"},
    {"Game's Native AA", "Anti-Aliasing Nativo do Jogo", "Собственное сглаживание игры"},
    {"Dynamic Light Shadows", "Sombras de Luzes Dinâmicas", "Тени от динамических источников"},
    {"SSR Reflections (not in base game)", "Reflexos SSR (não existia no jogo base)", "Отражения SSR (не было в игре)"},
    {"Bloom", "Bloom (Brilho Intenso)", "Свечение (Bloom)"},
    {"Skip Intro Cutscenes", "Pular Telas Iniciais", "Пропуск заставок при запуске"},
    {"Free Camera (Cross + L3)", "Câmera Livre (Cross + L3)", "Свободная камера (Cross + L3)"},
    {"Debug Menu (requires fonts)", "Menu de Depuração (requer fontes)", "Debug menu (нужны файлы шрифтов)"},
};
static_assert(std::size(kEffectLabels) == BbSettings::EffectCount,
              "kEffectLabels must have one row per BbSettings::Effects entry");

} // namespace

const char* Get(StringId id, int lang) {
    return Pick(FindText(id), lang);
}

const char* EffectLabel(int effect_index, int lang) {
    if (effect_index < 0 || effect_index >= static_cast<int>(std::size(kEffectLabels))) {
        return "";
    }
    return Pick(&kEffectLabels[effect_index], lang);
}

} // namespace BbStrings
