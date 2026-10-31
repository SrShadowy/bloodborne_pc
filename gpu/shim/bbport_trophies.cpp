// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: trophies (the runtime's sceNpTrophy calls, src/runtime_services.c). The game's unlocks
// are kept in <user>/trophy/<NP communication ID>/unlocked.txt and announced over the frame
// (BbOverlay::ShowTrophy) the first time, with the platinum once its trophies are all in, as
// shadPS4's np_trophy.cpp and trophy_ui.cpp do. Names and grades come from the game's
// trophy00.trp, decrypted once into that folder (TROP*.XML, the icons) with the user's trophy
// key: BB_TROPHY_KEY in the environment or in the port's .env (never in the repository). Without
// a key, or built without OpenSSL, the unlocks are still kept and shown by number. The names are
// in the game's language (BB_LANGUAGE). BB_TROPHY_TEST=<id>: that trophy shown once, 40 s of frames
// after the start, nothing recorded (to see the look).

#include <algorithm>
#include "shim/bb_env.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#if BB_TROPHY_DECRYPT
#include <openssl/evp.h>
#endif

#include "bbport_overlay.h"
#include "bbport_settings.h"
#include "bbport_trophies.h"
#include "bbport_trophy_data.h"

namespace {

namespace fs = std::filesystem;

std::mutex trophy_mutex;
bool set_up = false;
fs::path folder;                          // <user>/trophy/<NP communication ID>
std::vector<BbTrophy::Trophy> trophies;   // empty: names unknown (no key or no package)
std::set<int> unlocked;

// A path for the log, UTF-8 (on Windows path::c_str() is wide).
std::string Text(const fs::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

std::vector<std::uint8_t> ReadFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return file ? std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {}) : std::vector<std::uint8_t>{};
}

// BB_TROPHY_KEY from the environment, else from the .env file in the working directory (the
// port's folder, where run.sh starts the game).
std::optional<std::vector<std::uint8_t>> TrophyKey() {
    if (const char* env = bb_env("BB_TROPHY_KEY"); env && *env) {
        return BbTrophy::ParseKey(env);
    }
    std::ifstream file(".env");
    for (std::string line; std::getline(file, line);) {
        constexpr std::string_view Name = "BB_TROPHY_KEY=";
        if (line.rfind(Name, 0) == 0) {
            return BbTrophy::ParseKey(std::string_view(line).substr(Name.size()));
        }
    }
    return std::nullopt;
}

#if BB_TROPHY_DECRYPT
// AES-128-CBC without padding handling (the callers strip it).
bool Aes(bool encrypt, const std::uint8_t* key, const std::uint8_t* iv, const std::uint8_t* in, int size,
         std::uint8_t* out) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0, tail = 0;
    const bool ok = ctx && EVP_CipherInit_ex(ctx, EVP_aes_128_cbc(), nullptr, key, iv, encrypt ? 1 : 0) == 1 &&
                    EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 && EVP_CipherUpdate(ctx, out, &len, in, size) == 1 &&
                    EVP_CipherFinal_ex(ctx, out + len, &tail) == 1;
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

// The package's XML (decrypted) and icons into `folder`. True when TROP.XML is there after.
bool Extract(const fs::path& trp_path, const std::string& np_comm_id, const std::vector<std::uint8_t>& key) {
    const auto trp = ReadFile(trp_path);
    const auto entries = BbTrophy::ReadTrpEntries(trp);
    if (entries.empty()) {
        std::printf("Trophies: %s is not a trophy package\n", Text(trp_path).c_str());
        return false;
    }
    std::uint8_t id[16] = {}, file_key[16] = {};
    std::copy(np_comm_id.begin(), np_comm_id.end(), id);
    const std::uint8_t zero_iv[16] = {};
    if (!Aes(true, key.data(), zero_iv, id, 16, file_key)) {
        return false;
    }
    std::error_code ec;
    fs::create_directories(folder, ec);
    unsigned written = 0;
    for (const auto& e : entries) {
        const std::uint8_t* data = trp.data() + e.offset;
        std::string name = e.name, out;
        if (name.size() > 5 && name.ends_with(".ESFM")) {
            if (e.size < 32) {
                continue;
            }
            const int size = int((e.size - 16) / 16 * 16);
            std::vector<std::uint8_t> plain(size + 16);
            if (!Aes(false, file_key, data, data + 16, size, plain.data())) {
                continue;
            }
            plain.resize(size);
            out = BbTrophy::StripPadding(plain);
            if (out.find("<trophyconf") == std::string::npos) { // a wrong key: garbage
                std::printf("Trophies: %s did not decrypt (the trophy key does not fit)\n", name.c_str());
                return false;
            }
            name.replace(name.size() - 5, 5, ".XML");
        } else if (name.ends_with(".PNG")) {
            out.assign(reinterpret_cast<const char*>(data), e.size);
        } else {
            continue; // signatures and the rest
        }
        std::ofstream(folder / name, std::ios::binary).write(out.data(), std::streamsize(out.size()));
        ++written;
    }
    std::printf("Trophies: %u files of %s decrypted into %s\n", written, Text(trp_path.filename()).c_str(),
                Text(folder).c_str());
    return fs::exists(folder / "TROP.XML");
}
#endif

// TROP_XX.XML of the game's language (BB_LANGUAGE or menu_language: 1 English US, 8 Russian,
// 17 Portuguese BR; 0 .. 30), else TROP.XML.
fs::path LanguageFile() {
    int language = 1;
    const char* env = bb_env("BB_LANGUAGE");
    if (env && *env) {
        language = std::atoi(env);
    } else {
        const auto& s = BbSettings::Get();
        const int menu_lang = s.menu_language.load(std::memory_order_relaxed);
        if (menu_lang == 1) language = 17;      // Portuguese (BR)
        else if (menu_lang == 2) language = 8;  // Russian
        else language = 1;                      // English (US)
    }
    char xx[16];
    std::snprintf(xx, sizeof(xx), "TROP_%02d.XML", language);
    const fs::path lang = folder / xx;
    if (fs::exists(lang)) return lang;
    if (language == 17 && fs::exists(folder / "TROP_07.XML")) return folder / "TROP_07.XML"; // fallback PT-PT
    return fs::exists(folder / "TROP.XML") ? (folder / "TROP.XML") : lang;
}

fs::path IconPath(int id) {
    char name[32];
    std::snprintf(name, sizeof(name), "TROP%03d.PNG", id);
    const fs::path path = folder / name;
    return fs::exists(path) ? path : fs::path{};
}

void Save() {
    std::ofstream file(folder / "unlocked.txt");
    file << "# the trophies unlocked here, one id a line\n";
    for (const int id : unlocked) {
        file << id << '\n';
    }
}

void SetUp(const char* sce_sys, const char* user_dir) {
    set_up = true;
    const fs::path sys = sce_sys ? sce_sys : "";
    const auto np_comm_id = BbTrophy::NpCommId(ReadFile(sys / "npbind.dat"));
    folder = fs::path(user_dir ? user_dir : "user") / "trophy" / np_comm_id.value_or("unknown");
    std::error_code ec;
    fs::create_directories(folder, ec);
    {
        std::ifstream file(folder / "unlocked.txt");
        for (std::string line; std::getline(file, line);) {
            if (!line.empty() && line[0] != '#') {
                unlocked.insert(std::atoi(line.c_str()));
            }
        }
    }
    if (!fs::exists(folder / "TROP.XML") && np_comm_id) {
        fs::path trp = sys / "trophy" / "trophy00.trp";
        if (!fs::exists(trp)) {
            trp = sys / "trophy00.trp";
        }
        const auto key = TrophyKey();
#if BB_TROPHY_DECRYPT
        if (key && fs::exists(trp)) {
            Extract(trp, *np_comm_id, *key);
        }
#endif
        if (!key) {
            std::printf("Trophies: no trophy key (BB_TROPHY_KEY in the environment or .env): shown by number\n");
        }
    }
    const auto xml = ReadFile(LanguageFile());
    trophies = BbTrophy::ParseTrophies(std::string_view(reinterpret_cast<const char*>(xml.data()), xml.size()));
    std::printf("Trophies: %s, %zu known, %zu unlocked here (%s)\n", np_comm_id.value_or("no NP ID").c_str(),
                trophies.size(), unlocked.size(), Text(folder).c_str());
    // BB_TROPHY_TEST=<id>: that trophy's announcement once, nothing recorded (to see the look).
    if (const char* test = bb_env("BB_TROPHY_TEST"); test && *test) {
        const int id = std::atoi(test);
        const auto it = std::find_if(trophies.begin(), trophies.end(), [id](const auto& t) { return t.id == id; });
        BbOverlay::ShowTrophy(it != trophies.end() ? it->grade : '?',
                              it != trophies.end() ? it->name : "#" + std::to_string(id), IconPath(id), 40.0f);
    }
}

static int last_loaded_lang = -1;

void EnsureLoaded() {
    const auto& s = BbSettings::Get();
    const int cur_lang = s.menu_language.load(std::memory_order_relaxed);

    if (set_up && !trophies.empty() && cur_lang == last_loaded_lang) {
        return;
    }

    if (!set_up) {
        SetUp(nullptr, "user");
    }

    if (folder.empty() || !fs::exists(folder / "TROP.XML")) {
        const fs::path root = "user/trophy";
        std::error_code ec;
        if (fs::is_directory(root, ec)) {
            for (const auto& entry : fs::directory_iterator(root, ec)) {
                if (entry.is_directory(ec) &&
                    (fs::exists(entry.path() / "TROP.XML", ec) || fs::exists(entry.path() / "unlocked.txt", ec))) {
                    folder = entry.path();
                    break;
                }
            }
        }
    }

    if (!folder.empty() && (trophies.empty() || cur_lang != last_loaded_lang)) {
        const auto xml = ReadFile(LanguageFile());
        if (!xml.empty()) {
            trophies = BbTrophy::ParseTrophies(std::string_view(reinterpret_cast<const char*>(xml.data()), xml.size()));
            last_loaded_lang = cur_lang;
        }
    }

    if (!folder.empty() && fs::exists(folder / "unlocked.txt")) {
        std::ifstream file(folder / "unlocked.txt");
        for (std::string line; std::getline(file, line);) {
            if (!line.empty() && line[0] != '#') {
                unlocked.insert(std::atoi(line.c_str()));
            }
        }
    }
}

} // namespace

namespace BbTrophy {

std::vector<TrophyListItem> GetTrophyList() {
    std::scoped_lock lock{trophy_mutex};
    EnsureLoaded();
    std::vector<TrophyListItem> list;
    list.reserve(trophies.size());
    for (const auto& t : trophies) {
        TrophyListItem item;
        item.id = t.id;
        item.grade = t.grade;
        item.hidden = t.hidden;
        item.unlocked = (unlocked.find(t.id) != unlocked.end());
        item.name = !t.name.empty() ? t.name : ("Trophy #" + std::to_string(t.id));
        item.detail = t.detail;
        item.icon_path = IconPath(t.id);
        list.push_back(std::move(item));
    }
    if (list.empty() && !unlocked.empty()) {
        for (int id : unlocked) {
            TrophyListItem item;
            item.id = id;
            item.grade = '?';
            item.hidden = false;
            item.unlocked = true;
            item.name = "Trophy #" + std::to_string(id);
            item.detail = "";
            item.icon_path = IconPath(id);
            list.push_back(std::move(item));
        }
    }
    return list;
}

TrophyStats GetTrophyStats() {
    std::scoped_lock lock{trophy_mutex};
    EnsureLoaded();
    TrophyStats stats{};
    stats.total = trophies.size();
    for (const auto& t : trophies) {
        const bool is_unl = (unlocked.find(t.id) != unlocked.end());
        if (is_unl) stats.unlocked++;
        switch (t.grade) {
        case 'P':
            stats.platinum_total++;
            if (is_unl) stats.platinum_unlocked++;
            break;
        case 'G':
            stats.gold_total++;
            if (is_unl) stats.gold_unlocked++;
            break;
        case 'S':
            stats.silver_total++;
            if (is_unl) stats.silver_unlocked++;
            break;
        case 'B':
            stats.bronze_total++;
            if (is_unl) stats.bronze_unlocked++;
            break;
        default:
            break;
        }
    }
    if (stats.total == 0) {
        stats.total = unlocked.size();
        stats.unlocked = unlocked.size();
    }
    return stats;
}

} // namespace BbTrophy

/// The runtime's sceNpTrophyRegisterContext: the game's sce_sys folder (host path) and the user
/// folder. Once.
extern "C" void bbgpu_trophy_setup(const char* sce_sys, const char* user_dir) {
    std::scoped_lock lock{trophy_mutex};
    if (!set_up) {
        SetUp(sce_sys, user_dir);
    }
}

/// The runtime's sceNpTrophyUnlockTrophy: announces what is new; returns the platinum unlocked
/// along with it, else -1.
extern "C" int bbgpu_trophy_unlock(int id) {
    std::scoped_lock lock{trophy_mutex};
    if (!set_up) {
        SetUp(nullptr, nullptr);
    }
    const auto announce = BbTrophy::Unlock(trophies, unlocked, id);
    if (announce.empty()) {
        return -1; // unlocked in an earlier session: the game asks again, nothing to show
    }
    Save();
    int platinum = -1;
    for (const int t : announce) {
        const auto it = std::find_if(trophies.begin(), trophies.end(), [t](const auto& x) { return x.id == t; });
        const char grade = it != trophies.end() ? it->grade : '?';
        const std::string name = it != trophies.end() && !it->name.empty() ? it->name : "#" + std::to_string(t);
        std::printf("Trophies: unlocked %d (%c) %s\n", t, grade, name.c_str());
        BbOverlay::ShowTrophy(grade, name, IconPath(t));
        if (t != id) {
            platinum = t;
        }
    }
    return platinum;
}
