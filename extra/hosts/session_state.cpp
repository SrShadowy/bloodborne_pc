// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_state.h"
#include "account.h"
#include "http_client.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>

#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace Extra::Hosts {
namespace {

std::mutex g_state_mu;
std::vector<Peer> g_peers;

std::string g_session_id;
uint64_t g_room_id = 0;
uint16_t g_member_id = 0;
uint16_t g_owner_id = 0;
bool g_is_host = false;

int g_bound_port = 0;
std::string g_mapped_addr;
int g_mapped_port = 0;

uint16_t g_provisional_next = 0xff00;

}  // namespace

void PeersClear() {
    std::lock_guard<std::mutex> lock(g_state_mu);
    g_peers.clear();
    g_provisional_next = 0xff00;
}

void PeersUpsert(const Peer& p) {
    std::lock_guard<std::mutex> lock(g_state_mu);
    for (auto& item : g_peers) {
        if (item.member_id == p.member_id) {
            item = p;
            return;
        }
    }
    g_peers.push_back(p);
}

bool PeersGet(uint16_t member_id, Peer* out) {
    std::lock_guard<std::mutex> lock(g_state_mu);
    for (const auto& item : g_peers) {
        if (item.member_id == member_id) {
            if (out) *out = item;
            return true;
        }
    }
    return false;
}

bool PeersFindOnline(const std::string& online_id, Peer* out) {
    std::lock_guard<std::mutex> lock(g_state_mu);
    for (const auto& item : g_peers) {
        if (item.online_id == online_id) {
            if (out) *out = item;
            return true;
        }
    }
    return false;
}

bool PeersFindConn(unsigned conn_id, Peer* out) {
    std::lock_guard<std::mutex> lock(g_state_mu);
    for (const auto& item : g_peers) {
        if (item.conn_id == conn_id) {
            if (out) *out = item;
            return true;
        }
    }
    return false;
}

void PeersSetConn(uint16_t member_id, unsigned conn_id, int sig_status) {
    std::lock_guard<std::mutex> lock(g_state_mu);
    for (auto& item : g_peers) {
        if (item.member_id == member_id) {
            item.conn_id = conn_id;
            item.sig_status = sig_status;
            return;
        }
    }
}

std::vector<Peer> PeersAll() {
    std::lock_guard<std::mutex> lock(g_state_mu);
    return g_peers;
}

void PeersErase(uint16_t member_id) {
    std::lock_guard<std::mutex> lock(g_state_mu);
    g_peers.erase(std::remove_if(g_peers.begin(), g_peers.end(),
                                 [member_id](const Peer& p) { return p.member_id == member_id; }),
                  g_peers.end());
}

int PeersFromMembers(const nlohmann::json& members) {
    if (!members.is_array()) return 0;
    std::lock_guard<std::mutex> lock(g_state_mu);
    int count = 0;
    for (const auto& m : members) {
        if (!m.is_object()) continue;
        Peer p;
        p.member_id = static_cast<uint16_t>(IntOf(m, "MemberId", 0));
        p.online_id = StrOf(m, "OnlineId");
        p.addr = AddrOf(StrOf(m, "SignalingAddr"));
        p.port = static_cast<uint16_t>(IntOf(m, "SignalingPort", 0));
        p.local_addr = AddrOf(StrOf(m, "LocalAddr"));
        p.local_port = static_cast<uint16_t>(IntOf(m, "LocalPort", p.port));

        auto it = std::find_if(g_peers.begin(), g_peers.end(),
                               [&p](const Peer& item) { return item.member_id == p.member_id; });
        if (it != g_peers.end()) {
            p.conn_id = it->conn_id;
            p.sig_status = it->sig_status;
            *it = p;
        } else {
            g_peers.push_back(p);
        }
        count++;
    }
    return count;
}

Peer PeersProvisional(const std::string& online_id, uint32_t addr, uint16_t port) {
    std::lock_guard<std::mutex> lock(g_state_mu);
    for (const auto& item : g_peers) {
        if (item.online_id == online_id) return item;
    }
    Peer p;
    p.member_id = g_provisional_next++;
    p.online_id = online_id;
    p.addr = addr;
    p.port = port;
    g_peers.push_back(p);
    return p;
}

std::string LocalAddrText() {
    const char* env_ip = std::getenv("BB_HOST_LOCAL_IP");
    if (env_ip && env_ip[0]) return env_ip;

    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) return "127.0.0.1";

    std::string candidate;
    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
        char buf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf))) {
            const std::string ip(buf);
            if (ip == "127.0.0.1") continue;
            candidate = ip;
            break;
        }
    }
    freeifaddrs(ifaddr);
    return candidate.empty() ? "127.0.0.1" : candidate;
}

int P2pPort() {
    std::lock_guard<std::mutex> lock(g_state_mu);
    if (g_bound_port > 0) return g_bound_port;
    const char* env_p = std::getenv("BB_HOST_P2P_PORT");
    if (env_p && env_p[0]) return std::atoi(env_p);
    return 3658;
}

void SetP2pPortBound(int port) {
    std::lock_guard<std::mutex> lock(g_state_mu);
    g_bound_port = port;
}

void SetMappedAddress(const std::string& addr, int port) {
    std::lock_guard<std::mutex> lock(g_state_mu);
    g_mapped_addr = addr;
    g_mapped_port = port;
}

void GetMappedAddress(std::string& addr, int& port) {
    std::lock_guard<std::mutex> lock(g_state_mu);
    addr = g_mapped_addr;
    port = g_mapped_port;
}

nlohmann::json OurEndpoint() {
    std::lock_guard<std::mutex> lock(g_state_mu);
    nlohmann::json ep;
    ep["OnlineId"] = GetOnlineId();
    ep["SignalingAddr"] = LocalAddrText();
    ep["SignalingPort"] = g_bound_port > 0 ? g_bound_port : 3658;
    if (!g_mapped_addr.empty() && g_mapped_port > 0) {
        ep["MappedAddr"] = g_mapped_addr;
        ep["MappedPort"] = g_mapped_port;
    }
    return ep;
}

void RoomSet(const std::string& session_id, uint64_t room_id, uint16_t member_id,
             uint16_t owner_id, bool is_host) {
    std::lock_guard<std::mutex> lock(g_state_mu);
    g_session_id = session_id;
    g_room_id = room_id;
    g_member_id = member_id;
    g_owner_id = owner_id;
    g_is_host = is_host;
}

void RoomClear() {
    std::lock_guard<std::mutex> lock(g_state_mu);
    g_session_id.clear();
    g_room_id = 0;
    g_member_id = 0;
    g_owner_id = 0;
    g_is_host = false;
    g_peers.clear();
}

bool InRoom() {
    std::lock_guard<std::mutex> lock(g_state_mu);
    return !g_session_id.empty() && g_room_id != 0;
}

std::string CurrentSessionId() {
    std::lock_guard<std::mutex> lock(g_state_mu);
    return g_session_id;
}

uint64_t CurrentRoomId() {
    std::lock_guard<std::mutex> lock(g_state_mu);
    return g_room_id;
}

uint16_t CurrentMemberId() {
    std::lock_guard<std::mutex> lock(g_state_mu);
    return g_member_id;
}

uint16_t CurrentOwnerId() {
    std::lock_guard<std::mutex> lock(g_state_mu);
    return g_owner_id;
}

bool CurrentIsHost() {
    std::lock_guard<std::mutex> lock(g_state_mu);
    return g_is_host;
}

uint32_t AddrOf(const std::string& ip_str) {
    if (ip_str.empty()) return 0;
    struct in_addr in;
    if (inet_aton(ip_str.c_str(), &in) == 0) return 0;
    return in.s_addr;
}

}  // namespace Extra::Hosts
