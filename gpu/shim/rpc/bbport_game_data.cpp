// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_game_data.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "bbgpu.h"
#include "bbport_menu_pointer.h"
#include "bbport_scaleform_hit.h"
#include "bbport_settings.h"

namespace fs = std::filesystem;

namespace BbGameData {
namespace {

// Memory offsets relative to guest image base (CUSA03173 1.09 eboot)
constexpr uintptr_t kGameDataMan = 0x0553B130;
constexpr uintptr_t kCurrentMapList = 0x0553B148;
constexpr uintptr_t kMatchingState = 0x05540290;
constexpr uintptr_t kGlobalState = 0x05556678;
constexpr uintptr_t kBossGauge = 0x05562878;

std::chrono::steady_clock::time_point g_session_start{};
std::int64_t g_session_start_unix = 0;

std::atomic<int> g_active_slot{-1};
std::chrono::steady_clock::time_point g_last_save_read{};
std::string g_cached_char_name;
uint32_t g_cached_char_level = 0;
uint32_t g_cached_save_seconds = 0;

struct MapEntry {
    int id;
    const char* pt;
    const char* en;
    const char* ru;
};

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
    int32_t id;
    const char* pt;
    const char* en;
    const char* ru;
};

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

std::string Utf16LeToUtf8(const char16_t* utf16, size_t max_chars) {
    std::string utf8;
    for (size_t i = 0; i < max_chars && utf16[i] != 0; ++i) {
        char16_t c = utf16[i];
        if (c < 0x80) {
            utf8.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | ((c >> 6) & 0x1F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xE0 | ((c >> 12) & 0x0F)));
            utf8.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return utf8;
}

std::filesystem::path FindSaveDirectory() {
    static fs::path cached_dir;
    static bool checked = false;
    if (checked && !cached_dir.empty()) {
        return cached_dir;
    }

    if (const char* env_save = std::getenv("BBSC_SAVE_DIR")) {
        if (fs::is_directory(env_save)) {
            cached_dir = env_save;
            checked = true;
            return cached_dir;
        }
    }
    const char* user_dir = std::getenv("BB_GPU_USER_DIR");
    fs::path base = user_dir ? user_dir : "user";
    std::error_code ec;

    fs::path p1 = base / "savedata" / "1" / "CUSA00207" / "SPRJ0005";
    if (fs::is_directory(p1, ec)) {
        cached_dir = p1;
        checked = true;
        return cached_dir;
    }

    fs::path sdir = base / "savedata";
    if (fs::is_directory(sdir, ec)) {
        for (const auto& entry : fs::directory_iterator(sdir, ec)) {
            fs::path p = entry.path() / "CUSA00207" / "SPRJ0005";
            if (fs::is_directory(p, ec)) {
                cached_dir = p;
                checked = true;
                return cached_dir;
            }
            p = entry.path() / "CUSA03173" / "SPRJ0005";
            if (fs::is_directory(p, ec)) {
                cached_dir = p;
                checked = true;
                return cached_dir;
            }
        }
    }
    checked = true;
    return {};
}

void RefreshSaveCharacterInfo() {
    const auto now = std::chrono::steady_clock::now();
    if (g_last_save_read.time_since_epoch().count() != 0 &&
        (now - g_last_save_read) < std::chrono::milliseconds(5000)) {
        return;
    }
    g_last_save_read = now;

    fs::path dir = FindSaveDirectory();
    if (dir.empty()) {
        return;
    }

    int best_slot = g_active_slot.load(std::memory_order_relaxed);
    if (best_slot < 0 || best_slot >= 10) {
        std::error_code ec;
        fs::file_time_type best_time{};
        for (int s = 0; s < 10; ++s) {
            fs::path uf = dir / (s == 0 ? "userdata0000" : s == 1 ? "userdata0001" : s == 2 ? "userdata0002" :
                                 s == 3 ? "userdata0003" : s == 4 ? "userdata0004" : s == 5 ? "userdata0005" :
                                 s == 6 ? "userdata0006" : s == 7 ? "userdata0007" : s == 8 ? "userdata0008" : "userdata0009");
            auto t = fs::last_write_time(uf, ec);
            if (!ec && t > best_time) {
                best_time = t;
                best_slot = s;
            }
            ec.clear();
        }
        if (best_slot >= 0) {
            g_active_slot.store(best_slot, std::memory_order_relaxed);
        } else {
            best_slot = 0;
        }
    }

    fs::path meta = dir / "userdata0010";
    std::ifstream file(meta, std::ios::binary);
    if (!file.is_open()) {
        return;
    }

    constexpr size_t kStride = 0x192;
    const size_t name_offset = 0x1096 + best_slot * kStride;
    const size_t lvl_offset = name_offset + 34;
    const size_t pt_offset = lvl_offset + 4;

    file.seekg(name_offset);
    char16_t name_u16[17] = {};
    file.read(reinterpret_cast<char*>(name_u16), 32);

    file.seekg(lvl_offset);
    uint32_t lvl = 0, pt_sec = 0;
    file.read(reinterpret_cast<char*>(&lvl), sizeof(lvl));
    file.read(reinterpret_cast<char*>(&pt_sec), sizeof(pt_sec));

    std::string name_utf8 = Utf16LeToUtf8(name_u16, 16);
    if (!name_utf8.empty()) {
        g_cached_char_name = std::move(name_utf8);
        g_cached_char_level = lvl;
        g_cached_save_seconds = pt_sec;
    }
}

std::string GetCurrentBossName(int lang) {
    const uintptr_t img = bbgpu_get_guest_image_base();
    if (!img) return "";
    uintptr_t gauge = 0;
    if (!BbMenuPointer::SafeRead(img + kBossGauge, &gauge, sizeof(gauge)) || gauge < 0x10000) {
        return "";
    }
    for (int slot = 0; slot < 3; ++slot) {
        const size_t at = static_cast<size_t>(slot) * 0x14;
        int32_t handle = -1;
        if (BbMenuPointer::SafeRead(gauge + 0x165C + at, &handle, sizeof(handle)) && handle != -1) {
            int32_t boss_id = -1;
            if (BbMenuPointer::SafeRead(gauge + 0x1658 + at, &boss_id, sizeof(boss_id)) && boss_id > 0) {
                for (const auto& b : kBossNames) {
                    if (b.id == boss_id) {
                        return (lang == 1) ? b.pt : (lang == 2) ? b.ru : b.en;
                    }
                }
                return (lang == 1) ? "Chefe" : (lang == 2) ? "Босс" : "Boss";
            }
        }
    }
    return "";
}

bool GetPackedMap(uint32_t& packed) {
    const uintptr_t img = bbgpu_get_guest_image_base();
    if (!img) return false;

    uintptr_t map_list = 0;
    if (!BbMenuPointer::SafeRead(img + kCurrentMapList, &map_list, sizeof(map_list)) || map_list < 0x10000) {
        return false;
    }

    int32_t index = -1;
    uintptr_t map_array = 0;
    if (!BbMenuPointer::SafeRead(map_list + 0x20, &index, sizeof(index)) || index < 0 ||
        !BbMenuPointer::SafeRead(map_list + 0x10, &map_array, sizeof(map_array)) || map_array < 0x10000) {
        return false;
    }

    int32_t count = 0;
    uintptr_t entries = 0;
    if (!BbMenuPointer::SafeRead(map_array + 0x18, &count, sizeof(count)) || count <= 0 || index >= count ||
        !BbMenuPointer::SafeRead(map_array + 0x20, &entries, sizeof(entries)) || entries < 0x10000) {
        return false;
    }

    constexpr size_t kStride = 0xA0;
    const uintptr_t entry = entries + static_cast<size_t>(index) * kStride;
    uint32_t val = 0;
    if (!BbMenuPointer::SafeRead(entry + 8, &val, sizeof(val)) || val == 0 || val == 0xFFFFFFFF) {
        return false;
    }
    packed = val;
    return true;
}

void ResolveMapNames(uint32_t packed, std::string& pt, std::string& en, std::string& ru) {
    const int area = static_cast<int>((packed >> 24) & 0xFF);
    const int block = static_cast<int>((packed >> 16) & 0xFF);
    const int sub = static_cast<int>((packed >> 8) & 0xFF);

    if (area == 29) {
        pt = "Masmorra do Cálice";
        en = "Chalice Dungeon";
        ru = "Подземелье Чаши";
        return;
    }

    const int sub_id = area * 100 + block * 10 + sub;
    const int block_id = area * 100 + block * 10;
    const int area_id = area * 100;

    for (const auto& m : kMapNames) {
        if (m.id == sub_id) {
            pt = m.pt;
            en = m.en;
            ru = m.ru;
            return;
        }
    }
    for (const auto& m : kMapNames) {
        if (m.id == block_id) {
            pt = m.pt;
            en = m.en;
            ru = m.ru;
            return;
        }
    }
    for (const auto& m : kMapNames) {
        if (m.id == area_id) {
            pt = m.pt;
            en = m.en;
            ru = m.ru;
            return;
        }
    }

    char buf[32];
    std::snprintf(buf, sizeof(buf), "m%02d_%02d", area, block);
    pt = buf;
    en = buf;
    ru = buf;
}

void GetCoopState(int& role, int& hunters, int& max_hunters) {
    role = 0;
    hunters = 1;
    max_hunters = 3;

    const uintptr_t img = bbgpu_get_guest_image_base();
    if (!img) return;

    uintptr_t matching = 0;
    if (BbMenuPointer::SafeRead(img + kMatchingState, &matching, sizeof(matching)) && matching >= 0x10000) {
        int32_t session_state = 0;
        if (BbMenuPointer::SafeRead(matching + 0x124, &session_state, sizeof(session_state))) {
            if (session_state == 3) role = 1;      // Host
            else if (session_state == 6) role = 2; // Guest
        }
    }

    if (role != 0) {
        uintptr_t global = 0, population = 0;
        if (BbMenuPointer::SafeRead(img + kGlobalState, &global, sizeof(global)) && global >= 0x10000) {
            if (BbMenuPointer::SafeRead(global + 0x16F8, &population, sizeof(population)) && population >= 0x10000) {
                int32_t coop = 0;
                if (BbMenuPointer::SafeRead(population + 0x08, &coop, sizeof(coop))) {
                    if (coop > 0 && coop <= 4) {
                        hunters = static_cast<int>(coop) + 1;
                    }
                }
            }
        }
    }
}

} // namespace

void Init() {
    g_session_start = std::chrono::steady_clock::now();
    g_session_start_unix = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void NotifySaveSlotOpened(int slot) {
    if (slot >= 0 && slot < 10) {
        g_active_slot.store(slot, std::memory_order_relaxed);
        g_last_save_read = {};
        RefreshSaveCharacterInfo();
    }
}

uint32_t GetInGamePlaytimeSeconds() {
    uint32_t mem_seconds = 0;
    const uintptr_t img = bbgpu_get_guest_image_base();
    if (img) {
        uintptr_t gdm = 0;
        if (BbMenuPointer::SafeRead(img + kGameDataMan, &gdm, sizeof(gdm)) && gdm >= 0x10000) {
            uint32_t ms = 0;
            // GameDataMan + 0x94 is playtime_ms (0xd693a018 max).
            // Note: [[GameDataMan]+8]+0x94 is blood echoes, so we read gdm + 0x94 directly.
            if (BbMenuPointer::SafeRead(gdm + 0x94, &ms, sizeof(ms)) && ms > 0) {
                mem_seconds = ms / 1000;
            }
        }
    }
    return std::max(mem_seconds, g_cached_save_seconds);
}

std::string GetCurrentMapName(int lang) {
    uint32_t packed = 0;
    if (!GetPackedMap(packed)) {
        const uintptr_t img = bbgpu_get_guest_image_base();
        if (!img) {
            return (lang == 1) ? "Menu Principal" : (lang == 2) ? "Главное меню" : "Main Menu";
        }
        return (lang == 1) ? "Carregando..." : (lang == 2) ? "Загрузка..." : "Loading...";
    }
    std::string pt, en, ru;
    ResolveMapNames(packed, pt, en, ru);
    return (lang == 1) ? pt : (lang == 2) ? ru : en;
}

std::string GetCoopStatus(int lang) {
    int role = 0, hunters = 1, max_hunters = 3;
    GetCoopState(role, hunters, max_hunters);

    if (role == 1) {
        int coop = hunters - 1;
        if (coop > 0) {
            char buf[64];
            if (lang == 1) std::snprintf(buf, sizeof(buf), "Anfitrião (+%d Caçador%s)", coop, coop > 1 ? "es" : "");
            else if (lang == 2) std::snprintf(buf, sizeof(buf), "Хост (+%d)", coop);
            else std::snprintf(buf, sizeof(buf), "Host (+%d Hunter%s)", coop, coop > 1 ? "s" : "");
            return std::string(buf);
        }
        return (lang == 1) ? "Anfitrião (Host)" : (lang == 2) ? "Хост" : "Host";
    } else if (role == 2) {
        return (lang == 1) ? "Convidado (Co-op)" : (lang == 2) ? "Гость (Кооп)" : "Guest (Co-op)";
    }

    return (lang == 1) ? "Solo" : (lang == 2) ? "Одиночная" : "Solo";
}

PresenceSnapshot GetSnapshot() {
    PresenceSnapshot data;
    data.session_start_unix_s = g_session_start_unix;
    data.in_game_seconds = GetInGamePlaytimeSeconds();
    RefreshSaveCharacterInfo();
    data.hunter_name = g_cached_char_name;
    data.level = g_cached_char_level;

    static uint32_t last_valid_packed = 0;
    uint32_t packed = 0;
    if (GetPackedMap(packed)) {
        last_valid_packed = packed;
        data.in_game = true;
        ResolveMapNames(packed, data.map_name_pt, data.map_name_en, data.map_name_ru);
    } else if (last_valid_packed != 0 && data.in_game_seconds > 0) {
        data.in_game = true;
        ResolveMapNames(last_valid_packed, data.map_name_pt, data.map_name_en, data.map_name_ru);
    } else {
        data.in_game = false;
        data.map_name_pt = "Menu Principal";
        data.map_name_en = "Main Menu";
        data.map_name_ru = "Главное меню";
    }

    const int lang = BbSettings::Get().menu_language.load();
    data.boss_name = GetCurrentBossName(lang);
    GetCoopState(data.coop_role, data.hunters_count, data.max_hunters);
    return data;
}

} // namespace BbGameData

extern "C" void bbgpu_notify_save_slot(int slot) {
    BbGameData::NotifySaveSlotOpened(slot);
}
