// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Extra::Hosts {

struct Peer {
    uint16_t member_id = 0;
    std::string online_id;
    uint32_t addr = 0;        // network byte order
    uint16_t port = 0;        // host byte order
    uint32_t local_addr = 0;  // network byte order
    uint16_t local_port = 0;  // host byte order
    unsigned conn_id = 0;     // NpSignaling connection ID (0 = none)
    int sig_status = 0;       // 0 inactive, 1 pending, 2 active
};

// Peer table
void PeersClear();
void PeersUpsert(const Peer& p);
bool PeersGet(uint16_t member_id, Peer* out);
bool PeersFindOnline(const std::string& online_id, Peer* out);
bool PeersFindConn(unsigned conn_id, Peer* out);
void PeersSetConn(uint16_t member_id, unsigned conn_id, int sig_status);
std::vector<Peer> PeersAll();
void PeersErase(uint16_t member_id);
int PeersFromMembers(const nlohmann::json& members);
Peer PeersProvisional(const std::string& online_id, uint32_t addr, uint16_t port);

// Local endpoint / port
std::string LocalAddrText();
int P2pPort();
void SetP2pPortBound(int port);
void SetMappedAddress(const std::string& addr, int port);
void GetMappedAddress(std::string& addr, int& port);
nlohmann::json OurEndpoint();

// Room state
void RoomSet(const std::string& session_id, uint64_t room_id, uint16_t member_id,
             uint16_t owner_id, bool is_host);
void RoomClear();
bool InRoom();
std::string CurrentSessionId();
uint64_t CurrentRoomId();
uint16_t CurrentMemberId();
uint16_t CurrentOwnerId();
bool CurrentIsHost();

// Utility: convert IPv4 string to network-byte-order uint32
uint32_t AddrOf(const std::string& ip_str);

}  // namespace Extra::Hosts
