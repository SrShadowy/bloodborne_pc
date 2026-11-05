// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the map and boss name tables -- what the HUD and the Discord presence show for the area
// the player is in and the boss they are fighting.
#include "bbport_game_data_internal.h"

#include <cstdio>

#include "bbgpu.h"
#include "bbport_scaleform_hit.h"
#include "bbport_settings.h"

namespace BbGameData::detail {
namespace {

struct MapEntry {
    int id;
    const char* pt;
    const char* en;
    const char* ru;
};

/// Map ids are area*100 + block*10 + sub (e.g. Central Yharnam is 2410).
constexpr MapEntry kMapNames[] = {
    {2100, "Sonho do Caçador", "Hunter's Dream", "Сон охотника"},
    {2110, "Antiga Oficina Abandonada", "Abandoned Old Workshop", "Старая заброшенная мастерская"},
    {2200, "Alameda de Hemwick", "Hemwick Charnel Lane", "Переулок Хемвик"},
    {2300, "Yharnam Antiga", "Old Yharnam", "Старый Ярнам"},
    {2400, "Distrito da Catedral", "Cathedral Ward", "Соборный округ"},
    {2410, "Yharnam Central", "Central Yharnam", "Центральный Ярнам"},
    {2411, "Clínica da Iosefka", "Iosefka's Clinic", "Лечебница Йозефки"},
    {2420, "Distrito Superior da Catedral", "Upper Cathedral Ward", "Верхний Соборный округ"},
    {2421, "Oficina da Igreja da Cura", "Healing Church Workshop", "Мастерская Церкви Исцеления"},
    {2422, "Altar do Desespero", "Altar of Despair", "Алтарь отчаяния"},
    {2500, "Castelo de Cainhurst", "Forsaken Cainhurst Castle", "Замок Кейнхёрст"},
    {2600, "Pesadelo de Mensis", "Nightmare of Mensis", "Кошмар Менсиса"},
    {2700, "Bosque Proibido", "Forbidden Woods", "Запретные леса"},
    {2800, "Yahar'gul, Vilarejo Invisível", "Yahar'gul, Unseen Village", "Яхар'гул, Невидимая деревня"},
    {3200, "Byrgenwerth", "Byrgenwerth", "Бюргенверт"},
    {3201, "Edifício de Conferências", "Lecture Building", "Лекционный корпус"},
    {3202, "Lago do Lado da Lua", "Moonside Lake", "Озеро Лунного отражения"},
    {3300, "Fronteira do Pesadelo", "Nightmare Frontier", "Граница кошмара"},
    {3400, "Pesadelo do Caçador", "Hunter's Nightmare", "Кошмар охотника"},
    {3500, "Salão de Pesquisa", "Research Hall", "Зал исследований"},
    {3600, "Aldeia dos Pescadores", "Fishing Hamlet", "Рыбацкая деревня"},
};

struct BossEntry {
    std::int32_t id;
    const char* pt;
    const char* en;
    const char* ru;
};

/// Boss ids repeat across the chalice dungeons (the same boss in several depth/rites), so the
/// table lists every id the gauge can report.
constexpr BossEntry kBossNames[] = {
    {1000, "Escolta do Louco", "Madman's Escort", "Эскорт безумца"},
    {1010, "Louco Esquecido", "Forgotten Madman", "Забытый безумец"},
    {6400, "Antigo Caçador Henryk", "Old Hunter Henryk", "Старый охотник Хенрик"},
    {6500, "Valtr, Mestre da Liga", "Valtr, Master of the League", "Вальтер, Мастер Лиги"},
    {6505, "Valtr, Devorador de Feras", "Valtr, Beast Eater", "Вальтер, Пожиратель чудовищ"},
    {6530, "Yamamura, o Errante", "Yamamura the Wanderer", "Скиталец Ямамура"},
    {6540, "Gêmeo Madaras Mais Jovem", "Younger Madaras Twin", "Младший близнец Мадарас"},
    {6550, "Mensis Damian, Estudioso", "Mensis Scholar Damian", "Дамиан, ученый Менсиса"},
    {6600, "Henriett", "Henriett", "Генриетта"},
    {6640, "Antal Desertor", "Defector Antal", "Дезертир Антал"},
    {209000, "Fera Sedenta de Sangue", "Blood-starved Beast", "Чудовище-кровоглот"},
    {209010, "Fera Sedenta de Sangue", "Blood-starved Beast", "Чудовище-кровоглот"},
    {210000, "Bruxa de Hemwick", "The Witch of Hemwick", "Ведьма Хемвика"},
    {212010, "Sombra de Yharnam", "Shadow of Yharnam", "Тень Ярнама"},
    {212020, "Sombra de Yharnam", "Shadow of Yharnam", "Тень Ярнама"},
    {212030, "Sombra de Yharnam", "Shadow of Yharnam", "Тень Ярнама"},
    {216000, "Guardião dos Antigos Lordes", "Keeper of the Old Lords", "Хранитель Древних Богов"},
    {218000, "Fera Prateada de Loran", "Loran Silverbeast", "Серебряное чудовище Лорана"},
    {232000, "Mártir Logarius", "Martyr Logarius", "Мученик Логариус"},
    {251000, "Ebrietas, Filha do Cosmo", "Ebrietas, Daughter of the Cosmos", "Ибраитас, дочь Космоса"},
    {251010, "Ebrietas, Filha do Cosmo", "Ebrietas, Daughter of the Cosmos", "Ибраитас, дочь Космоса"},
    {257000, "Emissário Celestial", "Celestial Emissary", "Посланник Небес"},
    {257010, "Emissário Celestial", "Celestial Emissary", "Посланник Небес"},
    {271000, "Padre Gascoigne", "Father Gascoigne", "Отец Гаскойн"},
    {272000, "Padre Gascoigne", "Father Gascoigne", "Отец Гаскойн"},
    {280000, "Fera Clerical", "Cleric Beast", "Церковное чудовище"},
    {281000, "Vigário Amélia", "Vicar Amelia", "Викарий Амелия"},
    {290000, "O Renascido", "The One Reborn", "Возродившийся"},
    {291000, "Fera Negra Paarl", "Darkbeast Paarl", "Чернотвар Паарл"},
    {304000, "Vigia Impiedoso", "Merciless Watcher", "Беспощадный страж"},
    {305000, "Descendente Pthumeriano", "Pthumerian Descendant", "Потомок птумериан"},
    {305001, "Descendente Pthumeriano", "Pthumerian Descendant", "Потомок птумериан"},
    {305002, "Ancião Pthumeriano", "Pthumerian Elder", "Старейшина птумериан"},
    {306001, "Yharnam, Rainha Pthumeriana", "Yharnam, Pthumerian Queen", "Ярнам, Птумерианская Королева"},
    {310000, "Rom, a Aranha Insensata", "Rom, the Vacuous Spider", "Ром, Праздный Паук"},
    {310010, "Rom, a Aranha Insensata", "Rom, the Vacuous Spider", "Ром, Праздный Паук"},
    {313000, "Gigante Morto-Vivo", "Undead Giant", "Неживой великан"},
    {330000, "Amygdala", "Amygdala", "Амигдала"},
    {403000, "Falhas Vivas", "Living Failures", "Живые Неудачи"},
    {450000, "Primeiro Vigário Laurence", "Laurence, the First Vicar", "Лоренс, Первый Викарий"},
    {451000, "Ludwig, o Amaldiçoado", "Ludwig the Accursed", "Людвиг Проклятый"},
    {451005, "Ludwig, a Espada Sagrada", "Ludwig, the Holy Blade", "Людвиг Священный Клинок"},
    {452000, "Lady Maria da Torre do Relógio", "Lady Maria of the Astral Clocktower", "Леди Мария из Астральной часовой башни"},
    {453000, "Órfão de Kos", "Orphan of Kos", "Сирота Коса"},
    {454000, "Órfão de Kos", "Orphan of Kos", "Сирота Коса"},
    {500000, "Fera Clerical", "Cleric Beast", "Церковное чудовище"},
    {500010, "Ludwig, a Espada Sagrada", "Ludwig, the Holy Blade", "Людвиг Священный Клинок"},
    {501000, "Cão de Guarda dos Antigos Lordes", "Watchdog of the Old Lords", "Сторожевой пес Древних Богов"},
    {502000, "Vigário Amélia", "Vicar Amelia", "Викарий Амелия"},
    {504000, "Fera Sombria de Loran", "Loran Darkbeast", "Чудовище-страшилище Лорана"},
    {504010, "Fera Abominável", "Abhorrent Beast", "Отвратительное чудовище"},
    {507000, "O Renascido", "The One Reborn", "Возродившийся"},
    {508000, "Fera Negra Paarl", "Darkbeast Paarl", "Чернотвар Паарл"},
    {508010, "Fera Sombria de Loran", "Loran Darkbeast", "Чудовище-страшилище Лорана"},
    {509000, "Fera Sangrenta", "Bloodletting Beast", "Кровопускающее чудовище"},
    {509010, "Fera Sangrenta", "Bloodletting Beast", "Кровопускающее чудовище"},
    {512000, "Amygdala", "Amygdala", "Амигдала"},
    {512010, "Amygdala", "Amygdala", "Амигдала"},
    {520000, "Lady Maria da Torre do Relógio", "Lady Maria of the Astral Clocktower", "Леди Мария из Астральной часовой башни"},
    {530000, "Órfão de Kos", "Orphan of Kos", "Сирота Коса"},
    {530010, "Órfão de Kos", "Orphan of Kos", "Сирота Коса"},
    {540000, "Presença da Lua", "Moon Presence", "Присутствие Луны"},
    {540010, "Primeiro Vigário Laurence", "Laurence, the First Vicar", "Лоренс, Первый Викарий"},
    {551000, "Ama de Leite de Mergo", "Mergo's Wet Nurse", "Кормилица Мерго"},
    {551010, "Ama de Leite de Mergo", "Mergo's Wet Nurse", "Кормилица Мерго"},
    {800000, "Presença da Lua", "Moon Presence", "Присутствие Луны"},
    {804000, "Gehrman, o Primeiro Caçador", "Gehrman, the First Hunter", "Герман, Первый Охотник"},
    {805000, "Gehrman, o Primeiro Caçador", "Gehrman, the First Hunter", "Герман, Первый Охотник"},
    {899000, "Micolash, Anfitrião do Pesadelo", "Micolash, Host of the Nightmare", "Миколаш, хозяин кошмара"},
};

/// The chalice dungeons all share one name (area 29).
constexpr int kChaliceArea = 29;
/// The map/gauge slot values pack each level of the id in one byte.
constexpr int kAreaShift = 24;
constexpr int kBlockShift = 16;
constexpr int kSubShift = 8;
constexpr int kByteMask = 0xFF;
constexpr int kBlockScale = 10;
constexpr int kAreaScale = 100;

/// The charm/dungeon gauge holds up to three boss slots of 0x14 bytes.
constexpr int kBossSlots = 3;
constexpr size_t kBossSlotStride = 0x14;
constexpr size_t kBossGaugeHandleOffset = 0x165C;
constexpr size_t kBossGaugeIdOffset = 0x1658;

/// The name from a table row for `lang`.
const char* Localized(int lang, const char* pt, const char* en, const char* ru) {
    if (lang == BbSettings::LangPortuguese) {
        return pt;
    }
    if (lang == BbSettings::LangRussian) {
        return ru;
    }
    return en;
}

/// Looks a map id up; false when the table has no row for it.
bool FindMap(int id, std::string& pt, std::string& en, std::string& ru) {
    for (const MapEntry& entry : kMapNames) {
        if (entry.id == id) {
            pt = entry.pt;
            en = entry.en;
            ru = entry.ru;
            return true;
        }
    }
    return false;
}

} // namespace

void ResolveMapNames(std::uint32_t packed, std::string& pt, std::string& en, std::string& ru) {
    const int area = static_cast<int>((packed >> kAreaShift) & kByteMask);
    const int block = static_cast<int>((packed >> kBlockShift) & kByteMask);
    const int sub = static_cast<int>((packed >> kSubShift) & kByteMask);

    if (area == kChaliceArea) {
        pt = "Masmorra do Cálice";
        en = "Chalice Dungeon";
        ru = "Подземелье Чаши";
        return;
    }

    // Most specific first: the exact sub-area, then the block, then the whole area.
    if (FindMap(area * kAreaScale + block * kBlockScale + sub, pt, en, ru)) {
        return;
    }
    if (FindMap(area * kAreaScale + block * kBlockScale, pt, en, ru)) {
        return;
    }
    if (FindMap(area * kAreaScale, pt, en, ru)) {
        return;
    }

    // An area the table does not know: show the ids so a report can name it.
    char unknown[32];
    std::snprintf(unknown, sizeof(unknown), "m%02d_%02d", area, block);
    pt = unknown;
    en = unknown;
    ru = unknown;
}

const char* NoMapName(int lang, bool loading) {
    if (lang == BbSettings::LangPortuguese) {
        return loading ? "Carregando..." : "Menu Principal";
    }
    if (lang == BbSettings::LangRussian) {
        return loading ? "Загрузка..." : "Главное меню";
    }
    return loading ? "Loading..." : "Main Menu";
}

std::string CurrentBossName(int lang) {
    const uintptr_t img = bbgpu_get_guest_image_base();
    if (img == 0) {
        return {};
    }

    uintptr_t gauge = 0;
    if (!BbMenuPointer::SafeRead(img + kBossGauge, &gauge, sizeof(gauge)) ||
        gauge < kMinValidPointer) {
        return {};
    }

    for (int slot = 0; slot < kBossSlots; ++slot) {
        const size_t at = static_cast<size_t>(slot) * kBossSlotStride;
        std::int32_t handle = -1;
        if (!BbMenuPointer::SafeRead(gauge + kBossGaugeHandleOffset + at, &handle, sizeof(handle)) ||
            handle == -1) {
            continue;
        }
        std::int32_t boss_id = -1;
        if (!BbMenuPointer::SafeRead(gauge + kBossGaugeIdOffset + at, &boss_id, sizeof(boss_id)) ||
            boss_id <= 0) {
            continue;
        }
        for (const BossEntry& boss : kBossNames) {
            if (boss.id == boss_id) {
                return Localized(lang, boss.pt, boss.en, boss.ru);
            }
        }
        return Localized(lang, "Chefe", "Boss", "Босс");
    }
    return {};
}

} // namespace BbGameData::detail
