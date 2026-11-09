// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "coop/server_link.h"
#include "coop/net_link.h"
#include "coop/build_fingerprint.h"
#include "common/logging/log.h"
#include "common/singleton.h"
#include "core/libraries/net/net_util.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketType = SOCKET;
constexpr SocketType kInvalidSocket = INVALID_SOCKET;
static void CloseSock(SocketType s) { ::closesocket(s); }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketType = int;
constexpr SocketType kInvalidSocket = -1;
static void CloseSock(SocketType s) { ::close(s); }
#endif

namespace Libraries::Net {

namespace {

s64 NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

class ServerLinkRuntime {
public:
    static ServerLinkRuntime& Instance() {
        static ServerLinkRuntime runtime;
        return runtime;
    }

    ~ServerLinkRuntime() {
        Stop();
    }

    bool IsSwitchOn() {
        if (const char* v = std::getenv("BB_SHADNET_LINK_V4"); v && v[0] == '1') {
            return true;
        }
        if (const char* k = std::getenv("BB_SHADNET_SERVER_KEY"); k && *k) {
            return true;
        }
        std::scoped_lock lock{mutex_};
        return server_endpoint_.addr_nbo != 0 && server_endpoint_.port_nbo != 0;
    }

    void Configure(u32 server_addr_nbo, u16 server_port_nbo, std::string online_id,
                   std::string secret, std::string_view server_key) {
        std::scoped_lock lock{mutex_};
        server_endpoint_ = {server_addr_nbo, server_port_nbo};
        online_id_ = std::move(online_id);
        secret_ = std::move(secret);
        welcomed_ = false;
        last_hello_ms_.reset();
        refusal_.clear();
        warning_.clear();
        auth_ = {};

        const auto key = LinkAuth::KeyFromHex(server_key);
        if (key) {
            auth_.Pin(*key);
            LOG_INFO(Lib_Net, "Server link: pinned server key {}", server_key);
        } else if (!server_key.empty()) {
            LOG_WARNING(Lib_Net, "Server link: '{}' is not a valid 64-hex server key", server_key);
        }

        if (build_.empty()) {
            BloodborneCoop::BuildFingerprint fp;
            fp.Set("version", "1.4.0");
            fp.Set("protocol", std::to_string(BloodborneCoop::CoopFrameProtocol));
            build_ = BloodborneCoop::EncodeBuildFingerprint(fp);
        }

        StartLocked();
    }

    void SetBuild(std::string build) {
        std::scoped_lock lock{mutex_};
        if (Link::SaysHelloAgainForBuild(build_, build)) {
            welcomed_ = false;
            last_hello_ms_.reset();
        }
        build_ = std::move(build);
    }

    void SetObserver(ServerLinkObserver obs) {
        observer_.store(obs);
    }

    bool Welcomed() {
        std::scoped_lock lock{mutex_};
        return welcomed_;
    }

    std::string Refusal() {
        std::scoped_lock lock{mutex_};
        return refusal_;
    }

    std::string Warning() {
        std::scoped_lock lock{mutex_};
        return warning_;
    }

    void CutLink(u32 ms) {
        cut_until_ms_.store(NowMs() + ms);
    }

    bool IsCut() const {
        return NowMs() < cut_until_ms_.load(std::memory_order_relaxed);
    }

    bool CoopSend(const void* body, u32 len) {
        if (!body || len == 0) return false;
        std::scoped_lock lock{mutex_};
        if (server_endpoint_.addr_nbo == 0) return false;
        coop_outbox_.emplace_back(static_cast<const u8*>(body), static_cast<const u8*>(body) + len);
        return true;
    }

    int CoopReceive(void* body, u32 len) {
        std::scoped_lock lock{mutex_};
        if (coop_inbox_.empty()) return -1;
        auto msg = std::move(coop_inbox_.front());
        coop_inbox_.pop_front();
        const u32 copy_len = std::min<u32>(len, static_cast<u32>(msg.size()));
        if (body && copy_len > 0) {
            std::memcpy(body, msg.data(), copy_len);
        }
        return static_cast<int>(copy_len);
    }

    bool RpcSend(const void* body, u32 len) {
        if (!body || len == 0) return false;
        std::scoped_lock lock{mutex_};
        if (server_endpoint_.addr_nbo == 0) return false;
        rpc_outbox_.emplace_back(static_cast<const u8*>(body), static_cast<const u8*>(body) + len);
        return true;
    }

    int RpcReceive(void* body, u32 len) {
        std::scoped_lock lock{mutex_};
        if (rpc_inbox_.empty()) return -1;
        auto msg = std::move(rpc_inbox_.front());
        rpc_inbox_.pop_front();
        const u32 copy_len = std::min<u32>(len, static_cast<u32>(msg.size()));
        if (body && copy_len > 0) {
            std::memcpy(body, msg.data(), copy_len);
        }
        return static_cast<int>(copy_len);
    }

    bool DataSend(u32 dest_addr_nbo, u16 dest_port_nbo, const void* frame, u32 len) {
        if (!frame && len != 0) return false;
        sockaddr_in srv_addr{};
        srv_addr.sin_family = AF_INET;
        std::optional<std::vector<u8>> sealed;
        SocketType sock = kInvalidSocket;
        {
            std::scoped_lock lock{mutex_};
            if (server_endpoint_.addr_nbo == 0 || server_endpoint_.port_nbo == 0 ||
                socket_ == kInvalidSocket) {
                return false;
            }
            if (!Link::RoutesThroughServer(server_endpoint_, dest_addr_nbo, dest_port_nbo)) {
                return true;
            }
            srv_addr.sin_addr.s_addr = server_endpoint_.addr_nbo;
            srv_addr.sin_port = server_endpoint_.port_nbo;
            sock = socket_;
            auto unsealed = Link::EncodeData(
                dest_addr_nbo, dest_port_nbo,
                std::span<const u8>(static_cast<const u8*>(frame), static_cast<size_t>(len)));
            sealed = Link::Sealed(auth_, std::move(unsealed));
        }
        if (!sealed) {
            return false;
        }
        if (IsCut()) {
            return true;
        }
        const auto rc = ::sendto(sock, reinterpret_cast<const char*>(sealed->data()),
                                 static_cast<int>(sealed->size()), 0,
                                 reinterpret_cast<const sockaddr*>(&srv_addr), sizeof(srv_addr));
        return rc >= 0;
    }

    int DataReceive(void* frame, u32 len, u32* from_addr_nbo, u16* from_port_nbo) {
        std::scoped_lock lock{mutex_};
        if (data_inbox_.empty()) return -1;
        auto msg = std::move(data_inbox_.front());
        data_inbox_.pop_front();
        const u32 copy_len = std::min<u32>(len, static_cast<u32>(msg.frame.size()));
        if (frame && copy_len > 0) {
            std::memcpy(frame, msg.frame.data(), copy_len);
        }
        if (from_addr_nbo) *from_addr_nbo = msg.from_addr_nbo;
        if (from_port_nbo) *from_port_nbo = msg.from_port_nbo;
        return static_cast<int>(copy_len);
    }

private:
    struct DataPacket {
        u32 from_addr_nbo = 0;
        u16 from_port_nbo = 0;
        std::vector<u8> frame;
    };

    ServerLinkRuntime() = default;

    void Stop() {
        running_.store(false);
        if (worker_.joinable()) {
            worker_.join();
        }
        if (socket_ != kInvalidSocket) {
            CloseSock(socket_);
            socket_ = kInvalidSocket;
        }
    }

    void StartLocked() {
        if (running_.load()) {
            return;
        }
        socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ == kInvalidSocket) {
            LOG_ERROR(Lib_Net, "Server link: failed to create UDP socket");
            return;
        }
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = 0;
        if (::bind(socket_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
            LOG_ERROR(Lib_Net, "Server link: failed to bind UDP socket");
            CloseSock(socket_);
            socket_ = kInvalidSocket;
            return;
        }

        running_.store(true);
        worker_ = std::thread([this] { RunWorker(); });
        LOG_INFO(Lib_Net, "Server link: runtime background thread started");
    }

    static LinkAuth::Key FreshKey() {
        std::random_device rd;
        LinkAuth::Key k;
        for (u8& b : k) {
            b = static_cast<u8>(rd());
        }
        return k;
    }

    void TellObserver(std::string_view event, std::string_view detail) {
        if (auto obs = observer_.load()) {
            obs(event, detail);
        }
    }

    void RunWorker() {
        std::vector<u8> recv_buf(65536);
        s64 last_ping_ms = 0;
        u32 ping_seq = 0;

        while (running_.load()) {
            const s64 now_ms = NowMs();
            sockaddr_in srv_addr{};
            srv_addr.sin_family = AF_INET;
            {
                std::scoped_lock lock{mutex_};
                srv_addr.sin_addr.s_addr = server_endpoint_.addr_nbo;
                srv_addr.sin_port = server_endpoint_.port_nbo;
            }

            if (srv_addr.sin_addr.s_addr != 0 && srv_addr.sin_port != 0 && !IsCut()) {
                // Check if hello is due
                bool do_hello = false;
                std::vector<u8> hello_pkt;
                {
                    std::scoped_lock lock{mutex_};
                    if (Link::HelloDue(welcomed_, last_hello_ms_, !refusal_.empty(), now_ms)) {
                        do_hello = true;
                        last_hello_ms_ = now_ms;
                        hello_pkt = Link::HelloDatagram(auth_, online_id_, build_, secret_, FreshKey());
                    }
                }
                if (do_hello && !hello_pkt.empty()) {
                    ::sendto(socket_, reinterpret_cast<const char*>(hello_pkt.data()),
                             static_cast<int>(hello_pkt.size()), 0,
                             reinterpret_cast<const sockaddr*>(&srv_addr), sizeof(srv_addr));
                }

                // Send ping if welcomed
                bool do_ping = false;
                std::vector<u8> ping_pkt;
                {
                    std::scoped_lock lock{mutex_};
                    if (welcomed_ && now_ms - last_ping_ms >= 1000) {
                        do_ping = true;
                        last_ping_ms = now_ms;
                        Link::Ping p{ping_seq++, static_cast<u64>(now_ms)};
                        auto unsealed = Link::Encode(Link::Kind::Ping, p);
                        auto sealed = Link::Sealed(auth_, std::move(unsealed));
                        if (sealed) {
                            ping_pkt = std::move(*sealed);
                        }
                    }
                }
                if (do_ping && !ping_pkt.empty()) {
                    ::sendto(socket_, reinterpret_cast<const char*>(ping_pkt.data()),
                             static_cast<int>(ping_pkt.size()), 0,
                             reinterpret_cast<const sockaddr*>(&srv_addr), sizeof(srv_addr));
                }

                // Flush RPC outbox
                std::vector<std::vector<u8>> rpc_to_send;
                {
                    std::scoped_lock lock{mutex_};
                    while (!rpc_outbox_.empty()) {
                        auto unsealed = Link::EncodeRpc(rpc_outbox_.front());
                        rpc_outbox_.pop_front();
                        auto sealed = Link::Sealed(auth_, std::move(unsealed));
                        if (sealed) {
                            rpc_to_send.push_back(std::move(*sealed));
                        }
                    }
                }
                for (const auto& pkt : rpc_to_send) {
                    ::sendto(socket_, reinterpret_cast<const char*>(pkt.data()),
                             static_cast<int>(pkt.size()), 0,
                             reinterpret_cast<const sockaddr*>(&srv_addr), sizeof(srv_addr));
                }

                // Flush Coop outbox
                std::vector<std::vector<u8>> coop_to_send;
                {
                    std::scoped_lock lock{mutex_};
                    while (!coop_outbox_.empty()) {
                        auto unsealed = Link::EncodeCoop(coop_outbox_.front());
                        coop_outbox_.pop_front();
                        auto sealed = Link::Sealed(auth_, std::move(unsealed));
                        if (sealed) {
                            coop_to_send.push_back(std::move(*sealed));
                        }
                    }
                }
                for (const auto& pkt : coop_to_send) {
                    ::sendto(socket_, reinterpret_cast<const char*>(pkt.data()),
                             static_cast<int>(pkt.size()), 0,
                             reinterpret_cast<const sockaddr*>(&srv_addr), sizeof(srv_addr));
                }
            }

            // Select on socket for 2ms and drain all ready datagrams
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(socket_, &read_set);
            timeval tv{0, 2000};
            const int sel = ::select(static_cast<int>(socket_) + 1, &read_set, nullptr, nullptr, &tv);
            if (sel > 0 && FD_ISSET(socket_, &read_set)) {
                for (int i = 0; i < 64; ++i) {
                    sockaddr_in from{};
                    socklen_t from_len = sizeof(from);
#ifdef _WIN32
                    const int rc = ::recvfrom(socket_, reinterpret_cast<char*>(recv_buf.data()),
                                              static_cast<int>(recv_buf.size()), 0,
                                              reinterpret_cast<sockaddr*>(&from), &from_len);
#else
                    const int rc = ::recvfrom(socket_, reinterpret_cast<char*>(recv_buf.data()),
                                              static_cast<int>(recv_buf.size()), MSG_DONTWAIT,
                                              reinterpret_cast<sockaddr*>(&from), &from_len);
#endif
                    if (rc <= 0) {
                        break;
                    }
                    if (!IsCut()) {
                        HandleDatagram(std::span<const u8>(recv_buf.data(), static_cast<size_t>(rc)), from);
                    }
#ifdef _WIN32
                    break;
#endif
                }
            }
        }
    }

    void HandleDatagram(std::span<const u8> datagram, const sockaddr_in& from) {
        const auto kind = Link::KindOf(datagram);
        if (!kind) return;

        std::scoped_lock lock{mutex_};
        if (server_endpoint_.addr_nbo != 0 &&
            (from.sin_addr.s_addr != server_endpoint_.addr_nbo ||
             from.sin_port != server_endpoint_.port_nbo)) {
            return;
        }

        if (*kind == Link::Kind::Cookie) {
            LinkAuth::Key key{};
            int work_bits = 0;
            if (const auto cookie = Link::DecodeCookie(datagram, &work_bits, &key)) {
                if (auth_.TakeCookie(*cookie, key)) {
                    LOG_INFO(Lib_Net, "Server link: took cookie from server, key {}", LinkAuth::HexOf(key));
                    last_hello_ms_.reset(); // send sealed hello immediately
                }
            }
            return;
        }

        auto opened = Link::Opened(auth_, datagram);
        if (!opened) return;

        const std::span<const u8> plain{*opened};
        switch (*kind) {
        case Link::Kind::Data:
            if (const auto data = Link::DecodeData(plain)) {
                if (data_inbox_.size() >= 4096) {
                    data_inbox_.pop_front();
                }
                data_inbox_.push_back({data->addr_nbo, data->port_nbo,
                                       std::vector<u8>(data->frame.begin(), data->frame.end())});
            }
            break;
        case Link::Kind::Welcome:
            if (const auto welcome = Link::DecodeWelcome(plain)) {
                welcomed_ = welcome->accepted;
                if (welcome->accepted) {
                    refusal_.clear();
                    warning_ = welcome->text;
                    Common::Singleton<NetUtil::NetUtilInternal>::Instance()->SetExternalIp(
                        htonl(Link::VirtualAddressOf(online_id_).ipv4));
                    LOG_INFO(Lib_Net, "Server link: accepted! Welcome text: '{}'", welcome->text);
                    TellObserver("welcome", welcome->text);
                } else {
                    refusal_ = welcome->text;
                    LOG_ERROR(Lib_Net, "Server link: refused by server: '{}'", welcome->text);
                    TellObserver("refused", welcome->text);
                }
            }
            break;
        case Link::Kind::Pong:
            // ping keepalive satisfied
            break;
        case Link::Kind::Rpc:
            if (const auto body = Link::DecodeRpc(plain)) {
                rpc_inbox_.emplace_back(body->begin(), body->end());
            }
            break;
        case Link::Kind::Coop:
            if (const auto body = Link::DecodeCoop(plain)) {
                coop_inbox_.emplace_back(body->begin(), body->end());
            }
            break;
        case Link::Kind::Work:
            if (const auto bits = Link::DecodeWork(plain)) {
                LOG_INFO(Lib_Net, "Server link: work required: {} bits", *bits);
                if (auth_.WorkAsked(*bits)) {
                    const auto cookie = auth_.CookieToEcho();
                    const std::string id = online_id_;
                    const int wb = *bits;
                    std::thread([this, cookie, id, wb] {
                        u64 nonce = 0;
                        std::optional<u64> solved;
                        while (!solved && nonce < 0x100000000ULL) {
                            solved = LinkAuth::SolveWork(cookie, id, wb, nonce, 50000);
                            nonce += 50000;
                        }
                        std::scoped_lock lock{mutex_};
                        auth_.TakeWork(cookie, wb, solved);
                        last_hello_ms_.reset();
                        if (solved) {
                            LOG_INFO(Lib_Net, "Server link: work solved with nonce {:#x}", *solved);
                        } else {
                            LOG_WARNING(Lib_Net, "Server link: work solving failed");
                        }
                    }).detach();
                }
            }
            break;
        case Link::Kind::Redirect:
            if (const auto redirect = Link::DecodeRedirect(plain)) {
                server_endpoint_ = {redirect->addr_nbo, redirect->port_nbo};
                welcomed_ = false;
                last_hello_ms_.reset();
                auth_ = {};
                auth_.Pin(redirect->key);
                LOG_INFO(Lib_Net, "Server link: redirected to {:#x}:{}, text: '{}'",
                         ntohl(redirect->addr_nbo), ntohs(redirect->port_nbo), redirect->text);
                TellObserver("redirect", redirect->text);
            }
            break;
        default:
            break;
        }
    }

    std::mutex mutex_;
    Link::ServerEndpoint server_endpoint_{};
    std::string online_id_;
    std::string secret_;
    std::string build_;
    LinkAuth::Client auth_;
    bool welcomed_ = false;
    std::optional<s64> last_hello_ms_;
    std::string refusal_;
    std::string warning_;
    std::atomic<ServerLinkObserver> observer_{nullptr};
    std::atomic<s64> cut_until_ms_{0};

    std::deque<std::vector<u8>> rpc_outbox_;
    std::deque<std::vector<u8>> rpc_inbox_;
    std::deque<std::vector<u8>> coop_outbox_;
    std::deque<std::vector<u8>> coop_inbox_;
    std::deque<DataPacket> data_inbox_;

    SocketType socket_ = kInvalidSocket;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

} // namespace

bool ServerLinkSwitchOn() {
    return ServerLinkRuntime::Instance().IsSwitchOn();
}

void ConfigureServerLink(u32 server_addr_nbo, u16 server_port_nbo, std::string online_id,
                         std::string secret, std::string_view server_key) {
    ServerLinkRuntime::Instance().Configure(server_addr_nbo, server_port_nbo, std::move(online_id),
                                            std::move(secret), server_key);
}

void SetServerLinkBuild(std::string build) {
    ServerLinkRuntime::Instance().SetBuild(std::move(build));
}

void SetServerLinkObserver(ServerLinkObserver observer) {
    ServerLinkRuntime::Instance().SetObserver(observer);
}

bool ServerCoopSend(const void* body, u32 len) {
    return ServerLinkRuntime::Instance().CoopSend(body, len);
}

int ServerCoopReceive(void* body, u32 len) {
    return ServerLinkRuntime::Instance().CoopReceive(body, len);
}

bool ServerRpcSend(const void* body, u32 len) {
    return ServerLinkRuntime::Instance().RpcSend(body, len);
}

int ServerRpcReceive(void* body, u32 len) {
    return ServerLinkRuntime::Instance().RpcReceive(body, len);
}

bool ServerDataSend(u32 dest_addr_nbo, u16 dest_port_nbo, const void* frame, u32 len) {
    return ServerLinkRuntime::Instance().DataSend(dest_addr_nbo, dest_port_nbo, frame, len);
}

int ServerDataReceive(void* frame, u32 len, u32* from_addr_nbo, u16* from_port_nbo) {
    return ServerLinkRuntime::Instance().DataReceive(frame, len, from_addr_nbo, from_port_nbo);
}

bool ServerLinkWelcomed() {
    return ServerLinkRuntime::Instance().Welcomed();
}

bool ConnectsPeersWithoutHandshake() {
    return ServerLinkSwitchOn();
}

void CutServerLinkForTest(u32 ms) {
    ServerLinkRuntime::Instance().CutLink(ms);
}

std::string ServerLinkRefusal() {
    return ServerLinkRuntime::Instance().Refusal();
}

std::string ServerLinkWarning() {
    return ServerLinkRuntime::Instance().Warning();
}

} // namespace Libraries::Net
