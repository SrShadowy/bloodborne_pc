// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_discord_rpc.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "bbport_game_data.h"
#include "bbport_settings.h"
#include "bbport_threads.h"
#include "common/thread.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace BbDiscordRpc {
namespace {

constexpr const char* kClientId = "1560890616811102219";
constexpr const char* kShadps4ClientId = "1139939140494971051";

#pragma pack(push, 1)
struct DiscordIpcHeader {
    uint32_t opcode = 0;
    uint32_t length = 0;
};
#pragma pack(pop)

std::thread g_worker_thread;
std::mutex g_mutex;
std::condition_variable g_cv;
std::atomic<bool> g_stop_flag{false};
std::atomic<bool> g_connected{false};
std::atomic<bool> g_running{false};

std::string EscapeJsonString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (char c : input) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                out += buf;
            } else {
                out += c;
            }
            break;
        }
    }
    return out;
}

#ifndef _WIN32
std::vector<std::string> GetPossibleSocketPaths() {
    std::vector<std::string> paths;
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    const char* tmp = std::getenv("TMPDIR");
    if (!tmp) tmp = "/tmp";

    uid_t uid = getuid();
    std::string run_user = "/run/user/" + std::to_string(uid);

    std::vector<std::string> bases;
    if (xdg && *xdg) {
        bases.push_back(xdg);
        bases.push_back(std::string(xdg) + "/app/com.discordapp.Discord");
        bases.push_back(std::string(xdg) + "/app/com.discordapp.DiscordCanary");
    }
    bases.push_back(run_user);
    bases.push_back(run_user + "/app/com.discordapp.Discord");
    bases.push_back(run_user + "/app/com.discordapp.DiscordCanary");
    bases.push_back(tmp);

    for (const auto& base : bases) {
        for (int i = 0; i < 10; ++i) {
            paths.push_back(base + "/discord-ipc-" + std::to_string(i));
        }
    }
    return paths;
}

int ConnectDiscordSocket() {
    const auto paths = GetPossibleSocketPaths();
    for (const auto& path : paths) {
        struct stat st{};
        if (stat(path.c_str(), &st) != 0 || !S_ISSOCK(st.st_mode)) {
            continue;
        }

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) continue;

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        int res = connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        if (res == 0) {
            if (flags >= 0) fcntl(fd, F_SETFL, flags);
            return fd;
        }

        if (errno == EINPROGRESS) {
            struct pollfd pfd{};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            int poll_res = poll(&pfd, 1, 200); // 200ms timeout
            if (poll_res > 0 && (pfd.revents & POLLOUT)) {
                int so_error = 0;
                socklen_t len = sizeof(so_error);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
                if (so_error == 0) {
                    if (flags >= 0) fcntl(fd, F_SETFL, flags);
                    return fd;
                }
            }
        }

        close(fd);
    }
    return -1;
}

bool SendData(int fd, uint32_t opcode, const std::string& json) {
    DiscordIpcHeader hdr{opcode, static_cast<uint32_t>(json.size())};
    const char* hdr_ptr = reinterpret_cast<const char*>(&hdr);
    size_t hdr_sent = 0;
    while (hdr_sent < sizeof(hdr)) {
        ssize_t res = send(fd, hdr_ptr + hdr_sent, sizeof(hdr) - hdr_sent, MSG_NOSIGNAL);
        if (res <= 0) return false;
        hdr_sent += res;
    }
    size_t json_sent = 0;
    while (json_sent < json.size()) {
        ssize_t res = send(fd, json.data() + json_sent, json.size() - json_sent, MSG_NOSIGNAL);
        if (res <= 0) return false;
        json_sent += res;
    }
    return true;
}

bool ReadFrame(int fd, uint32_t& opcode, std::string& json) {
    DiscordIpcHeader hdr{};
    char* hdr_ptr = reinterpret_cast<char*>(&hdr);
    size_t hdr_read = 0;
    while (hdr_read < sizeof(hdr)) {
        ssize_t res = recv(fd, hdr_ptr + hdr_read, sizeof(hdr) - hdr_read, 0);
        if (res <= 0) return false;
        hdr_read += res;
    }
    opcode = hdr.opcode;
    json.resize(hdr.length);
    size_t read_bytes = 0;
    while (read_bytes < hdr.length) {
        ssize_t res = recv(fd, json.data() + read_bytes, hdr.length - read_bytes, 0);
        if (res <= 0) return false;
        read_bytes += res;
    }
    return true;
}

bool PerformHandshake(int fd) {
    const std::string& cid_setting = BbSettings::Get().discord_client_id;
    const std::string cid = cid_setting.empty() ? kClientId : cid_setting;
    std::string handshake_json = "{\"v\":1,\"client_id\":\"" + cid + "\"}";
    if (!SendData(fd, 0, handshake_json)) {
        return false;
    }
    uint32_t op = 0;
    std::string reply;
    if (!ReadFrame(fd, op, reply)) {
        return false;
    }
    return (op == 1);
}
#endif

#ifdef _WIN32
HANDLE ConnectDiscordPipe() {
    for (int i = 0; i < 10; ++i) {
        std::string pipe_name = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
        HANDLE h = CreateFileA(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
            return h;
        }
    }
    return INVALID_HANDLE_VALUE;
}

bool SendDataWin(HANDLE pipe, uint32_t opcode, const std::string& json) {
    DiscordIpcHeader hdr{opcode, static_cast<uint32_t>(json.size())};
    DWORD written = 0;
    if (!WriteFile(pipe, &hdr, sizeof(hdr), &written, nullptr) || written != sizeof(hdr)) {
        return false;
    }
    if (!WriteFile(pipe, json.data(), static_cast<DWORD>(json.size()), &written, nullptr) || written != json.size()) {
        return false;
    }
    return true;
}

bool ReadFrameWin(HANDLE pipe, uint32_t& opcode, std::string& json) {
    DiscordIpcHeader hdr{};
    DWORD read_bytes = 0;
    if (!ReadFile(pipe, &hdr, sizeof(hdr), &read_bytes, nullptr) || read_bytes != sizeof(hdr)) {
        return false;
    }
    opcode = hdr.opcode;
    json.resize(hdr.length);
    if (!ReadFile(pipe, json.data(), hdr.length, &read_bytes, nullptr) || read_bytes != hdr.length) {
        return false;
    }
    return true;
}

bool PerformHandshakeWin(HANDLE pipe) {
    const std::string& cid_setting = BbSettings::Get().discord_client_id;
    const std::string cid = cid_setting.empty() ? kClientId : cid_setting;
    std::string handshake_json = "{\"v\":1,\"client_id\":\"" + cid + "\"}";
    if (!SendDataWin(pipe, 0, handshake_json)) {
        return false;
    }
    uint32_t op = 0;
    std::string reply;
    if (!ReadFrameWin(pipe, op, reply)) {
        return false;
    }
    return (op == 1);
}
#endif

std::string BuildClearActivityJson(int pid) {
    return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + ",\"activity\":null},\"nonce\":\"1\"}";
}

static std::string GetMapAssetKey(const std::string& map_en) {
    if (map_en.find("Hunter's Dream") != std::string::npos ||
        map_en.find("Abandoned Old Workshop") != std::string::npos) {
        return "hunter_dream";
    }
    if (map_en.find("Central Yharnam") != std::string::npos ||
        map_en.find("Iosefka") != std::string::npos) {
        return "central_yharnam";
    }
    if (map_en.find("Cathedral") != std::string::npos ||
        map_en.find("Healing Church") != std::string::npos ||
        map_en.find("Altar of Despair") != std::string::npos) {
        return "cathedral_ward";
    }
    if (map_en.find("Old Yharnam") != std::string::npos) {
        return "old_yharnam";
    }
    if (map_en.find("Hemwick") != std::string::npos) {
        return "hemwick";
    }
    if (map_en.find("Cainhurst") != std::string::npos) {
        return "cainhurst";
    }
    if (map_en.find("Forbidden Woods") != std::string::npos) {
        return "forbidden_woods";
    }
    if (map_en.find("Yahar'gul") != std::string::npos ||
        map_en.find("Unseen Village") != std::string::npos ||
        map_en.find("Hypogean Gaol") != std::string::npos) {
        return "yahargul";
    }
    if (map_en.find("Byrgenwerth") != std::string::npos ||
        map_en.find("Lecture") != std::string::npos ||
        map_en.find("Moonside") != std::string::npos) {
        return "byrgenwerth";
    }
    if (map_en.find("Nightmare Frontier") != std::string::npos ||
        map_en.find("Nightmare of Mensis") != std::string::npos) {
        return "nightmare";
    }
    if (map_en.find("Hunter's Nightmare") != std::string::npos ||
        map_en.find("Research Hall") != std::string::npos ||
        map_en.find("Fishing Hamlet") != std::string::npos) {
        return "hunters_nightmare";
    }
    if (map_en.find("Chalice") != std::string::npos) {
        return "chalice_dungeon";
    }
    return "app_icon";
}

ActivityDisplay FormatActivity(const BbGameData::PresenceSnapshot& p, const BbSettings::Values& s, int lang) {
    ActivityDisplay ad;
    const std::string& cid = s.discord_client_id;
    const bool is_custom_app = (cid != kShadps4ClientId);

    if (!p.in_game) {
        ad.details = (lang == 1) ? "Menu Principal" : (lang == 2) ? "Главное меню" : "Main Menu";
        ad.state = (lang == 1) ? "No Menu Principal" : (lang == 2) ? "В главном меню" : "In Main Menu";
        if (s.discord_rpc_show_time.load(std::memory_order_relaxed) && p.session_start_unix_s > 0) {
            const auto now_s = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            int64_t elapsed = (now_s > p.session_start_unix_s) ? (now_s - p.session_start_unix_s) : 0;
            int hrs = static_cast<int>(elapsed / 3600);
            int mins = static_cast<int>((elapsed % 3600) / 60);
            int secs = static_cast<int>(elapsed % 60);
            char buf[64];
            if (hrs > 0) {
                std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hrs, mins, secs);
            } else {
                std::snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
            }
            ad.time_text = (lang == 1) ? (std::string(buf) + " decorrido") :
                           (lang == 2) ? (std::string(buf) + " прошло") :
                                         (std::string(buf) + " elapsed");
        }
        ad.large_image = is_custom_app ? "app_icon" : "https://store.playstation.com/store/api/chihiro/00_09_000/titlecontainer/US/en/999/CUSA03173_00/image";
        ad.large_text = "Bloodborne (BBPort)";
        ad.small_image = "app_icon";
        ad.small_text = "BBport Shadowy Edition";
        ad.has_party = false;
        return ad;
    }

    // --- Line 1: Details (Hunter, Level, Deaths) ---
    std::string hunter_part;
    const bool show_name = s.discord_rpc_show_name.load(std::memory_order_relaxed);
    const bool show_lvl = s.discord_rpc_show_level.load(std::memory_order_relaxed);
    if (show_name && !p.hunter_name.empty()) {
        hunter_part = p.hunter_name;
        if (show_lvl && p.level > 0) {
            hunter_part += " (Nv. " + std::to_string(p.level) + ")";
        }
    } else if (show_lvl && p.level > 0) {
        hunter_part = (lang == 1) ? ("Nv. " + std::to_string(p.level)) :
                      (lang == 2) ? ("Ур. " + std::to_string(p.level)) :
                                    ("Lv. " + std::to_string(p.level));
    }

    std::string deaths_part;
    const bool show_deaths = s.discord_rpc_show_deaths.load(std::memory_order_relaxed) &&
                             (s.discord_rpc_deaths_mode.load(std::memory_order_relaxed) != 0);
    if (show_deaths) {
        deaths_part = (lang == 1) ? ("Mortes: " + std::to_string(p.session_deaths)) :
                      (lang == 2) ? ("Смерти: " + std::to_string(p.session_deaths)) :
                                    ("Deaths: " + std::to_string(p.session_deaths));
    }

    if (!hunter_part.empty() && !deaths_part.empty()) {
        ad.details = hunter_part + " • " + deaths_part;
    } else if (!hunter_part.empty()) {
        ad.details = hunter_part;
    } else if (!deaths_part.empty()) {
        ad.details = deaths_part;
    } else if (s.discord_rpc_show_area.load(std::memory_order_relaxed)) {
        ad.details = (lang == 1) ? p.map_name_pt : (lang == 2) ? p.map_name_ru : p.map_name_en;
    } else {
        ad.details = (lang == 1) ? "Caçador" : (lang == 2) ? "Охотник" : "Hunter";
    }

    // --- Line 2: State (Boss, Co-op/Party, or Solo) ---
    const bool show_boss = s.discord_rpc_show_boss.load(std::memory_order_relaxed);
    const bool show_party = s.discord_rpc_show_party.load(std::memory_order_relaxed);

    if (show_boss && !p.boss_name.empty()) {
        ad.state = (lang == 1) ? "Lutando: " + p.boss_name : (lang == 2) ? "Бой: " + p.boss_name : "Fighting: " + p.boss_name;
    } else if (show_party && p.coop_role == 1) {
        if (p.npc_hunters_count > 0 && p.player_hunters_count == 0) {
            char buf[64];
            if (lang == 1) std::snprintf(buf, sizeof(buf), "Co-op: Anfitrião (+%d NPC%s)", p.npc_hunters_count, p.npc_hunters_count > 1 ? "s" : "");
            else if (lang == 2) std::snprintf(buf, sizeof(buf), "Кооп: Хост (+%d NPC)", p.npc_hunters_count);
            else std::snprintf(buf, sizeof(buf), "Co-op: Host (+%d NPC%s)", p.npc_hunters_count, p.npc_hunters_count > 1 ? "s" : "");
            ad.state = buf;
        } else if (p.player_hunters_count > 0 && p.npc_hunters_count > 0) {
            char buf[64];
            if (lang == 1) std::snprintf(buf, sizeof(buf), "Co-op (+%d Jog, +%d NPC)", p.player_hunters_count, p.npc_hunters_count);
            else if (lang == 2) std::snprintf(buf, sizeof(buf), "Кооп (+%d игр, +%d NPC)", p.player_hunters_count, p.npc_hunters_count);
            else std::snprintf(buf, sizeof(buf), "Co-op (+%d Pl, +%d NPC)", p.player_hunters_count, p.npc_hunters_count);
            ad.state = buf;
        } else if (p.hunters_count > 1) {
            char buf[64];
            int extra = p.hunters_count - 1;
            if (lang == 1) std::snprintf(buf, sizeof(buf), "Co-op: Anfitrião (+%d)", extra);
            else if (lang == 2) std::snprintf(buf, sizeof(buf), "Кооп: Хост (+%d)", extra);
            else std::snprintf(buf, sizeof(buf), "Co-op: Host (+%d)", extra);
            ad.state = buf;
        } else {
            ad.state = (lang == 1) ? "Co-op: Anfitrião" : (lang == 2) ? "Кооп: Хост" : "Co-op: Host";
        }
    } else if (show_party && p.coop_role == 2) {
        ad.state = (lang == 1) ? "Co-op: Convidado" : (lang == 2) ? "Кооп: Гость" : "Co-op: Guest";
    } else if (show_party && p.invaders_count > 0) {
        ad.state = (lang == 1) ? "Invadido!" : (lang == 2) ? "Вторжение!" : "Invaded!";
    } else {
        ad.state = (lang == 1) ? "Jogando Solo" : (lang == 2) ? "Одиночная игра" : "Playing Solo";
    }

    // --- Timestamps ---
    if (s.discord_rpc_show_time.load(std::memory_order_relaxed) && p.session_start_unix_s > 0) {
        const auto now_s = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int64_t elapsed = (now_s > p.session_start_unix_s) ? (now_s - p.session_start_unix_s) : 0;
        int hrs = static_cast<int>(elapsed / 3600);
        int mins = static_cast<int>((elapsed % 3600) / 60);
        int secs = static_cast<int>(elapsed % 60);
        char buf[64];
        if (hrs > 0) {
            std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hrs, mins, secs);
        } else {
            std::snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
        }
        ad.time_text = (lang == 1) ? (std::string(buf) + " decorrido") :
                       (lang == 2) ? (std::string(buf) + " прошло") :
                                     (std::string(buf) + " elapsed");
    }

    // --- Assets ---
    const bool show_area = s.discord_rpc_show_area.load(std::memory_order_relaxed);
    if (is_custom_app) {
        ad.large_image = show_area ? GetMapAssetKey(p.map_name_en) : "app_icon";
        std::string area_text;
        if (show_area) {
            area_text = (lang == 1) ? p.map_name_pt : (lang == 2) ? p.map_name_ru : p.map_name_en;
        }
        if (area_text.empty()) {
            area_text = "Bloodborne (BBPort)";
        }
        ad.large_text = area_text;
        ad.small_image = "app_icon";
        ad.small_text = "BBport Shadowy Edition";
    } else {
        ad.large_image = "https://store.playstation.com/store/api/chihiro/00_09_000/titlecontainer/US/en/999/CUSA03173_00/image";
        ad.large_text = show_area ? ((lang == 1) ? p.map_name_pt : (lang == 2) ? p.map_name_ru : p.map_name_en) : "Bloodborne (BBPort)";
        if (ad.large_text.empty()) ad.large_text = "Bloodborne (BBPort)";
        ad.small_image = "hunter";
        ad.small_text = "BBport Shadowy Edition";
    }

    // --- Party ---
    if (show_party) {
        ad.has_party = true;
        ad.party_size = p.hunters_count > 0 ? p.hunters_count : 1;
        ad.party_max = p.max_hunters > 0 ? p.max_hunters : 3;
        ad.party_text = "(" + std::to_string(ad.party_size) + (lang == 1 ? " de " : lang == 2 ? " из " : " of ") + std::to_string(ad.party_max) + ")";
    } else {
        ad.has_party = false;
    }

    if (ad.details.size() > 127) ad.details.resize(127);
    if (ad.state.size() > 127) ad.state.resize(127);

    return ad;
}

std::string BuildActivityJson(const BbGameData::PresenceSnapshot& p, int lang) {
    const auto& s = BbSettings::Get();
    int pid = 0;
#ifdef _WIN32
    pid = static_cast<int>(GetCurrentProcessId());
#else
    pid = static_cast<int>(getpid());
#endif

    ActivityDisplay ad = FormatActivity(p, s, lang);

    std::string json = "{";
    json += "\"cmd\":\"SET_ACTIVITY\",";
    json += "\"args\":{";
    json += "\"pid\":" + std::to_string(pid) + ",";
    json += "\"activity\":{";
    json += "\"details\":\"" + EscapeJsonString(ad.details) + "\",";
    json += "\"state\":\"" + EscapeJsonString(ad.state) + "\",";

    if (s.discord_rpc_show_time.load(std::memory_order_relaxed) && p.session_start_unix_s > 0) {
        json += "\"timestamps\":{\"start\":" + std::to_string(p.session_start_unix_s) + "},";
    }

    json += "\"assets\":{";
    json += "\"large_image\":\"" + EscapeJsonString(ad.large_image) + "\",";
    json += "\"large_text\":\"" + EscapeJsonString(ad.large_text) + "\",";
    json += "\"small_image\":\"" + EscapeJsonString(ad.small_image) + "\",";
    json += "\"small_text\":\"" + EscapeJsonString(ad.small_text) + "\"";
    json += "}";

    if (ad.has_party) {
        json += ",\"party\":{";
        json += "\"id\":\"ae488379-351d-4a4f-ad32-2b9b01c91657\",";
        json += "\"size\":[" + std::to_string(ad.party_size) + "," + std::to_string(ad.party_max) + "]";
        json += "}";
    }

    json += "}},\"nonce\":\"1\"}";
    return json;
}

void WorkerLoop() {
#ifdef _WIN32
    HANDLE pipe = INVALID_HANDLE_VALUE;
#else
    int sock_fd = -1;
#endif

    while (!g_stop_flag.load()) {
        const auto& s = BbSettings::Get();
        if (!s.discord_rpc.load()) {
            if (g_connected.load()) {
#ifdef _WIN32
                if (pipe != INVALID_HANDLE_VALUE) {
                    DWORD pid = GetCurrentProcessId();
                    SendDataWin(pipe, 1, BuildClearActivityJson(pid));
                    CloseHandle(pipe);
                    pipe = INVALID_HANDLE_VALUE;
                }
#else
                if (sock_fd >= 0) {
                    int pid = getpid();
                    SendData(sock_fd, 1, BuildClearActivityJson(pid));
                    close(sock_fd);
                    sock_fd = -1;
                }
#endif
                g_connected = false;
            }

            std::unique_lock lock(g_mutex);
            g_cv.wait_for(lock, std::chrono::seconds(2), [] {
                return g_stop_flag.load() || BbSettings::Get().discord_rpc.load();
            });
            continue;
        }

        if (!g_connected.load()) {
#ifdef _WIN32
            pipe = ConnectDiscordPipe();
            if (pipe != INVALID_HANDLE_VALUE) {
                if (PerformHandshakeWin(pipe)) {
                    g_connected = true;
                } else {
                    CloseHandle(pipe);
                    pipe = INVALID_HANDLE_VALUE;
                }
            }
#else
            sock_fd = ConnectDiscordSocket();
            if (sock_fd >= 0) {
                if (PerformHandshake(sock_fd)) {
                    g_connected = true;
                } else {
                    close(sock_fd);
                    sock_fd = -1;
                }
            }
#endif
            if (!g_connected.load()) {
                std::unique_lock lock(g_mutex);
                g_cv.wait_for(lock, std::chrono::seconds(5), [] {
                    return g_stop_flag.load();
                });
                continue;
            }
        }

        const int lang = s.menu_language.load();
        const auto presence = BbGameData::GetSnapshot();
        const std::string activity_json = BuildActivityJson(presence, lang);

        bool send_ok = false;
#ifdef _WIN32
        send_ok = SendDataWin(pipe, 1, activity_json);
#else
        send_ok = SendData(sock_fd, 1, activity_json);
#endif

        if (!send_ok) {
#ifdef _WIN32
            if (pipe != INVALID_HANDLE_VALUE) {
                CloseHandle(pipe);
                pipe = INVALID_HANDLE_VALUE;
            }
#else
            if (sock_fd >= 0) {
                close(sock_fd);
                sock_fd = -1;
            }
#endif
            g_connected = false;
            std::unique_lock lock(g_mutex);
            g_cv.wait_for(lock, std::chrono::seconds(5), [] {
                return g_stop_flag.load();
            });
            continue;
        }

#ifdef _WIN32
        DWORD bytes_avail = 0;
        if (PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytes_avail, nullptr) && bytes_avail > 0) {
            uint32_t op = 0;
            std::string reply;
            ReadFrameWin(pipe, op, reply);
        }
#else
        struct pollfd pfd{};
        pfd.fd = sock_fd;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
            uint32_t op = 0;
            std::string reply;
            ReadFrame(sock_fd, op, reply);
        }
#endif

        std::unique_lock lock(g_mutex);
        g_cv.wait_for(lock, std::chrono::seconds(5), [] {
            return g_stop_flag.load() || !BbSettings::Get().discord_rpc.load();
        });
    }

#ifdef _WIN32
    if (pipe != INVALID_HANDLE_VALUE) {
        DWORD pid = GetCurrentProcessId();
        SendDataWin(pipe, 1, BuildClearActivityJson(pid));
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;
    }
#else
    if (sock_fd >= 0) {
        int pid = getpid();
        SendData(sock_fd, 1, BuildClearActivityJson(pid));
        close(sock_fd);
        sock_fd = -1;
    }
#endif
    g_connected = false;
}

} // namespace

void Init() {
    if (g_running.exchange(true)) {
        return;
    }
    g_stop_flag = false;
    g_worker_thread = std::thread([] {
        Common::SetCurrentThreadName("bb:discord_rpc");
        BbThreads::MakeBackground();
        WorkerLoop();
    });
}

void Shutdown() {
    if (!g_running.exchange(false)) {
        return;
    }
    g_stop_flag = true;
    g_cv.notify_all();
    if (g_worker_thread.joinable()) {
        g_worker_thread.join();
    }
}

bool IsConnected() {
    return g_connected.load();
}

const char* GetStatusText(int lang) {
    if (!BbSettings::Get().discord_rpc.load()) {
        return (lang == 1) ? "Desativado" : (lang == 2) ? "Отключено" : "Disabled";
    }
    if (g_connected.load()) {
        return (lang == 1) ? "● Conectado ao Discord" : (lang == 2) ? "● Подключено к Discord" : "● Connected to Discord";
    }
    return (lang == 1) ? "○ Procurando Discord..." : (lang == 2) ? "○ Поиск Discord..." : "○ Searching for Discord...";
}

ActivityDisplay GetCurrentActivityDisplay(int lang) {
    const auto& s = BbSettings::Get();
    BbGameData::PresenceSnapshot p = BbGameData::GetSnapshot();
    return FormatActivity(p, s, lang);
}

} // namespace BbDiscordRpc
