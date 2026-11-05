// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Discord IPC transport -- the named pipe on Windows, the Unix socket elsewhere, and
// the little header every message is framed with. bbport_discord_rpc_internal.h declares it.
#include "bbport_discord_rpc_internal.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bbport_settings.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace BbDiscordRpc::detail {
namespace {

/// Discord exposes ten IPC endpoints per user (discord-ipc-0 .. discord-ipc-9).
constexpr int kEndpointCount = 10;
/// How long a non-blocking connect waits for the socket to become writable.
constexpr int kConnectTimeoutMs = 200;

/// The handshake's opcode: the client answers with its READY frame.
constexpr std::uint32_t kOpHandshake = 0;
/// The opcode both directions use for a command (SET_ACTIVITY, and its reply).
constexpr std::uint32_t kOpFrame = 1;

#ifdef _WIN32
HANDLE NativeHandle(const IpcConnection& connection) {
    return static_cast<HANDLE>(connection.handle);
}

IpcConnection Wrap(HANDLE pipe) {
    IpcConnection connection;
    connection.handle = pipe;
    return connection;
}
#else
/// Every place Discord's socket has been seen: the XDG runtime dir (native, Flatpak, Canary) and
/// the temp dir, all ending in discord-ipc-<n>.
std::vector<std::string> SocketPaths() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    const char* tmp = std::getenv("TMPDIR");
    if (tmp == nullptr) {
        tmp = "/tmp";
    }

    const std::string run_user = "/run/user/" + std::to_string(getuid());
    std::vector<std::string> bases;
    if (xdg != nullptr && *xdg != '\0') {
        const std::string dir = xdg;
        bases.push_back(dir);
        bases.push_back(dir + "/app/com.discordapp.Discord");
        bases.push_back(dir + "/app/com.discordapp.DiscordCanary");
        bases.push_back(dir + "/.flatpak/com.discordapp.Discord/xdg-run");
    }
    bases.push_back(run_user);
    bases.push_back(run_user + "/app/com.discordapp.Discord");
    bases.push_back(run_user + "/app/com.discordapp.DiscordCanary");
    bases.push_back(run_user + "/.flatpak/com.discordapp.Discord/xdg-run");
    bases.push_back(tmp);

    std::vector<std::string> paths;
    paths.reserve(bases.size() * kEndpointCount);
    for (const std::string& base : bases) {
        for (int i = 0; i < kEndpointCount; ++i) {
            paths.push_back(base + "/discord-ipc-" + std::to_string(i));
        }
    }
    return paths;
}

/// Connects non-blocking so a stale socket cannot hang the worker, then puts the fd back the way
/// it was.
int ConnectToPath(const std::string& path) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0 || !S_ISSOCK(info.st_mode)) {
        return -1;
    }

    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    const int result = connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (result == 0) {
        if (flags >= 0) {
            fcntl(fd, F_SETFL, flags);
        }
        return fd;
    }

    if (errno == EINPROGRESS) {
        struct pollfd poll_fd {};
        poll_fd.fd = fd;
        poll_fd.events = POLLOUT;
        if (poll(&poll_fd, 1, kConnectTimeoutMs) > 0 && (poll_fd.revents & POLLOUT) != 0) {
            int socket_error = 0;
            socklen_t length = sizeof(socket_error);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &length);
            if (socket_error == 0) {
                if (flags >= 0) {
                    fcntl(fd, F_SETFL, flags);
                }
                return fd;
            }
        }
    }

    close(fd);
    return -1;
}

IpcConnection Wrap(int fd) {
    IpcConnection connection;
    connection.fd = fd;
    return connection;
}
#endif

/// Sends the whole buffer, looping over partial writes.
bool SendAll(const IpcConnection& connection, const char* data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(NativeHandle(connection), data + sent, static_cast<DWORD>(size - sent), &written,
                       nullptr) ||
            written == 0) {
            return false;
        }
#else
        const ssize_t written = send(connection.fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (written <= 0) {
            return false;
        }
#endif
        sent += written;
    }
    return true;
}

/// Reads exactly `size` bytes.
bool ReadAll(IpcConnection& connection, char* data, std::size_t size) {
    std::size_t read = 0;
    while (read < size) {
#ifdef _WIN32
        DWORD got = 0;
        if (!ReadFile(NativeHandle(connection), data + read, static_cast<DWORD>(size - read), &got,
                      nullptr) ||
            got == 0) {
            return false;
        }
#else
        const ssize_t got = recv(connection.fd, data + read, size - read, 0);
        if (got <= 0) {
            return false;
        }
#endif
        read += got;
    }
    return true;
}

} // namespace

bool IpcConnection::IsOpen() const {
#ifdef _WIN32
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
#else
    return fd >= 0;
#endif
}

void IpcConnection::Close() {
#ifdef _WIN32
    if (IsOpen()) {
        CloseHandle(NativeHandle(*this));
    }
    handle = nullptr;
#else
    if (fd >= 0) {
        close(fd);
    }
    fd = -1;
#endif
}

IpcConnection Connect() {
#ifdef _WIN32
    for (int i = 0; i < kEndpointCount; ++i) {
        const std::string pipe_name = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
        const HANDLE pipe = CreateFileA(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                        OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            continue;
        }
        // Discord's pipe is message-oriented; frame reads need that mode.
        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
        return Wrap(pipe);
    }
    return Wrap(INVALID_HANDLE_VALUE);
#else
    for (const std::string& path : SocketPaths()) {
        const int fd = ConnectToPath(path);
        if (fd >= 0) {
            return Wrap(fd);
        }
    }
    return Wrap(-1);
#endif
}

bool Send(const IpcConnection& connection, std::uint32_t opcode, const std::string& json) {
    if (!connection.IsOpen()) {
        return false;
    }
    const IpcHeader header{opcode, static_cast<std::uint32_t>(json.size())};
    return SendAll(connection, reinterpret_cast<const char*>(&header), sizeof(header)) &&
           SendAll(connection, json.data(), json.size());
}

bool Read(IpcConnection& connection, std::uint32_t& opcode, std::string& json) {
    if (!connection.IsOpen()) {
        return false;
    }
    IpcHeader header{};
    if (!ReadAll(connection, reinterpret_cast<char*>(&header), sizeof(header))) {
        return false;
    }
    opcode = header.opcode;
    json.resize(header.length);
    return ReadAll(connection, json.data(), header.length);
}

bool HasPendingFrame(const IpcConnection& connection) {
    if (!connection.IsOpen()) {
        return false;
    }
#ifdef _WIN32
    DWORD available = 0;
    return PeekNamedPipe(NativeHandle(connection), nullptr, 0, nullptr, &available, nullptr) != 0 &&
           available > 0;
#else
    struct pollfd poll_fd {};
    poll_fd.fd = connection.fd;
    poll_fd.events = POLLIN;
    return poll(&poll_fd, 1, 0) > 0 && (poll_fd.revents & POLLIN) != 0;
#endif
}

std::string ClientId() {
    const std::string& configured = BbSettings::Get().discord_client_id;
    return configured.empty() ? kBbportClientId : configured;
}

/// The handshake's reply says whether Discord accepted this application ID.
bool PerformHandshake(IpcConnection& connection) {
    const std::string payload = "{\"v\":1,\"client_id\":\"" + ClientId() + "\"}";
    if (!Send(connection, kOpHandshake, payload)) {
        return false;
    }
    std::uint32_t opcode = 0;
    std::string reply;
    if (!Read(connection, opcode, reply)) {
        return false;
    }
    return opcode == kOpFrame;
}

} // namespace BbDiscordRpc::detail
