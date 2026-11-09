// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "coop/rpc_bridge.h"

#include <array>
#include <chrono>
#include <mutex>
#include <random>
#include <span>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "common/logging/log.h"
#include "coop/rpc_tunnel.h"
#include "coop/server_link.h"

namespace Libraries::Net {
namespace {

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket NoSocket = INVALID_SOCKET;
void CloseSocket(Socket socket) {
    closesocket(socket);
}
#else
using Socket = int;
constexpr Socket NoSocket = -1;
void CloseSocket(Socket socket) {
    close(socket);
}
#endif

constexpr int TickMs = 5;

s64 NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Whether `socket` has something to read (a connection, bytes, its end) within `ms`.
bool Readable(Socket socket, int ms) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket, &read_set);
    timeval timeout{0, ms * 1000};
    return select(static_cast<int>(socket) + 1, &read_set, nullptr, nullptr, &timeout) > 0;
}

bool SendAll(Socket socket, std::span<const u8> bytes) {
    size_t sent = 0;
    while (sent < bytes.size()) {
        const int rc = send(socket, reinterpret_cast<const char*>(bytes.data() + sent),
                            static_cast<int>(bytes.size() - sent), 0);
        if (rc <= 0) {
            return false;
        }
        sent += static_cast<size_t>(rc);
    }
    return true;
}

class Bridge {
public:
    u16 Start() {
        std::scoped_lock lock{mutex};
        if (port != 0) {
            return port;
        }
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == NoSocket) {
            return 0;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        socklen_t size = sizeof(address);
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener, 4) != 0 ||
            getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) != 0) {
            LOG_ERROR(Lib_Net, "Rpc bridge: no listener on 127.0.0.1");
            CloseSocket(listener);
            listener = NoSocket;
            return 0;
        }
        port = ntohs(address.sin_port);
        std::thread{[this] { Run(); }}.detach();
        LOG_INFO(Lib_Net, "Rpc bridge: shadNet's calls go through the server link (127.0.0.1:{})",
                 port);
        return port;
    }

private:
    void Run() {
        std::array<u8, 16384> buffer{};
        while (true) {
            const s64 now_ms = NowMs();
            if (client == NoSocket) {
                if (Readable(listener, TickMs)) {
                    Accept(now_ms);
                }
            } else if (Readable(client, TickMs)) {
                const int got =
                    recv(client, reinterpret_cast<char*>(buffer.data()), buffer.size(), 0);
                if (got > 0) {
                    tunnel.Write({buffer.data(), static_cast<size_t>(got)}, now_ms);
                } else {
                    LOG_INFO(Lib_Net, "Rpc bridge: the game's connection ended");
                    DropClient();
                    tunnel.Close(now_ms);
                }
            }
            // A new connection while one is open replaces it (the game reconnected).
            if (client != NoSocket && Readable(listener, 0)) {
                DropClient();
                tunnel.Close(now_ms);
                Accept(now_ms);
            }
            Exchange(now_ms, buffer);
        }
    }

    void Accept(s64 now_ms) {
        client = accept(listener, nullptr, nullptr);
        if (client == NoSocket) {
            return;
        }
        tunnel.Open(now_ms);
    }

    void DropClient() {
        if (client != NoSocket) {
            CloseSocket(client);
            client = NoSocket;
        }
    }

    // What came from shadNet to the game's connection, and what the tunnel sends.
    void Exchange(s64 now_ms, std::array<u8, 16384>& buffer) {
        for (int count = 0; count < 256; ++count) {
            const int size = ServerRpcReceive(buffer.data(), static_cast<u32>(buffer.size()));
            if (size < 0) {
                break;
            }
            for (auto& event : tunnel.Receive({buffer.data(), static_cast<size_t>(size)}, now_ms)) {
                using Kind = BloodborneCoop::RpcTunnel::Event::Kind;
                if (event.kind == Kind::Data && client != NoSocket) {
                    if (!SendAll(client, event.bytes)) {
                        DropClient();
                        tunnel.Close(now_ms);
                    }
                } else if (event.kind == Kind::Closed && client != NoSocket) {
                    // shadNet's end is over (or shadNet restarted): the game's client sees its
                    // connection end and connects again.
                    LOG_INFO(Lib_Net, "Rpc bridge: shadNet ended the connection");
                    DropClient();
                }
            }
        }
        for (const auto& body : tunnel.Flush(now_ms)) {
            ServerRpcSend(body.data(), static_cast<u32>(body.size()));
        }
    }

    std::mutex mutex;
    u16 port = 0;
    Socket listener = NoSocket;
    Socket client = NoSocket;
    BloodborneCoop::RpcTunnel tunnel{"game", [] {
                                         std::random_device random;
                                         return static_cast<u32>(random()) | 1u;
                                     }()};
};

Bridge& Instance() {
    static Bridge bridge;
    return bridge;
}

} // namespace

u16 StartRpcBridge() {
    return Instance().Start();
}

} // namespace Libraries::Net
