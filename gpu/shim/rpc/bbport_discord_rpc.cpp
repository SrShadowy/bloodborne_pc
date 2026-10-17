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

constexpr const char* kClientId = "1139939140494971051";

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

std::string BuildActivityJson(const BbGameData::PresenceSnapshot& p, int lang) {
    const auto& s = BbSettings::Get();
    int pid = 0;
#ifdef _WIN32
    pid = static_cast<int>(GetCurrentProcessId());
#else
    pid = static_cast<int>(getpid());
#endif

    const std::string& cid = s.discord_client_id;
    const bool is_custom_app = (!cid.empty() && cid != kClientId);

    if (!p.in_game) {
        const std::string menu_title = (lang == 1) ? "Menu Principal" : (lang == 2) ? "Главное меню" : "Main Menu";
        const std::string menu_state = (lang == 1) ? "No Menu Principal" : (lang == 2) ? "В главном меню" : "In Main Menu";
        std::string json = "{";
        json += "\"cmd\":\"SET_ACTIVITY\",";
        json += "\"args\":{";
        json += "\"pid\":" + std::to_string(pid) + ",";
        json += "\"activity\":{";
        json += "\"details\":\"" + EscapeJsonString(menu_title) + "\",";
        json += "\"state\":\"" + EscapeJsonString(menu_state) + "\",";
        if (s.discord_rpc_show_time && p.session_start_unix_s > 0) {
            json += "\"timestamps\":{\"start\":" + std::to_string(p.session_start_unix_s) + "},";
        }
        json += "\"assets\":{";
        if (is_custom_app) {
            json += "\"large_image\":\"app_icon\",";
        } else {
            json += "\"large_image\":\"https://store.playstation.com/store/api/chihiro/00_09_000/titlecontainer/US/en/999/CUSA03173_00/image\",";
        }
        json += "\"large_text\":\"Bloodborne (BBPort)\",";
        json += "\"small_image\":\"app_icon\",";
        json += "\"small_text\":\"BBport Shadowy Edition\"";
        json += "}";
        json += "}},\"nonce\":\"1\"}";
        return json;
    }

    std::string details;
    if (s.discord_rpc_show_character && !p.hunter_name.empty()) {
        details = p.hunter_name;
        if (p.level > 0) {
            details += " (Nv. " + std::to_string(p.level) + ")";
        }
    } else if (s.discord_rpc_show_area) {
        details = (lang == 1) ? p.map_name_pt : (lang == 2) ? p.map_name_ru : p.map_name_en;
    } else {
        details = (lang == 1) ? "Caçador" : (lang == 2) ? "Охотник" : "Hunter";
    }

    std::string state;
    if (!p.boss_name.empty()) {
        state = (lang == 1) ? "Lutando: " + p.boss_name : (lang == 2) ? "Бой: " + p.boss_name : "Fighting: " + p.boss_name;
    } else if (p.coop_role == 1) {
        state = (lang == 1) ? "Co-op: Anfitrião" : (lang == 2) ? "Кооп: Хост" : "Co-op: Host";
    } else if (p.coop_role == 2) {
        state = (lang == 1) ? "Co-op: Convidado" : (lang == 2) ? "Кооп: Гость" : "Co-op: Guest";
    } else {
        state = (lang == 1) ? "Jogando Solo" : (lang == 2) ? "Одиночная игра" : "Playing Solo";
    }

    std::string json = "{";
    json += "\"cmd\":\"SET_ACTIVITY\",";
    json += "\"args\":{";
    json += "\"pid\":" + std::to_string(pid) + ",";
    json += "\"activity\":{";
    json += "\"details\":\"" + EscapeJsonString(details) + "\",";
    json += "\"state\":\"" + EscapeJsonString(state) + "\",";

    if (s.discord_rpc_show_time && p.session_start_unix_s > 0) {
        json += "\"timestamps\":{\"start\":" + std::to_string(p.session_start_unix_s) + "},";
    }

    json += "\"assets\":{";
    if (is_custom_app) {
        const std::string map_key = GetMapAssetKey(p.map_name_en);
        std::string area_text = (lang == 1) ? p.map_name_pt : (lang == 2) ? p.map_name_ru : p.map_name_en;
        if (area_text.empty()) {
            area_text = "Bloodborne (BBPort)";
        }
        json += "\"large_image\":\"" + EscapeJsonString(map_key) + "\",";
        json += "\"large_text\":\"" + EscapeJsonString(area_text) + "\",";
        json += "\"small_image\":\"app_icon\",";
        json += "\"small_text\":\"BBport Shadowy Edition\"";
    } else {
        json += "\"large_image\":\"https://store.playstation.com/store/api/chihiro/00_09_000/titlecontainer/US/en/999/CUSA03173_00/image\",";
        json += "\"large_text\":\"Bloodborne (BBPort)\",";
        json += "\"small_image\":\"hunter\",";
        json += "\"small_text\":\"BBport Shadowy Edition\"";
    }
    json += "}";

    const int party_size = p.hunters_count > 0 ? p.hunters_count : 1;
    const int party_max = 5;
    json += ",\"party\":{";
    json += "\"id\":\"ae488379-351d-4a4f-ad32-2b9b01c91657\",";
    json += "\"size\":[" + std::to_string(party_size) + "," + std::to_string(party_max) + "]";
    json += "}";

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

} // namespace BbDiscordRpc
