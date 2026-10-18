// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace Extra::Hosts {

// Sends /mp/matching2/context_start to register this instance's endpoint.
bool ServerContextStart(nlohmann::json& reply, std::string& error);

// Creates a room on the server (/mp/matching2/create_room).
// Returns true on 2xx and ResKind == 0, updating local room state.
bool ServerCreateRoom(int max_members, const nlohmann::json& extra,
                      nlohmann::json& reply, std::string& error);

// Joins an existing room (/mp/matching2/join_room).
// Returns true on 2xx and ResKind == 0, updating local room & peer state.
bool ServerJoinRoom(uint64_t room_id, nlohmann::json& reply, std::string& error);

// Leaves a room (/mp/matching2/leave_room) and clears local room state.
bool ServerLeaveRoom(const std::string& session_id, int member_id,
                     nlohmann::json& reply, std::string& error);

// Heartbeat to keep session alive (/mp/matching2/heartbeat).
// Returns 1 if still in room, 0 if dropped, -1 on network transport failure.
int ServerHeartbeat(const std::string& session_id, int member_id);

// Kicks a room member (/mp/matching2/kick_member).
bool ServerKickMember(const std::string& session_id, int member_id, int kicker_id,
                      const uint8_t* opt, size_t opt_len, std::string& error);

// Resolves a peer's signaling address (/np/signaling/resolve).
bool ServerSignalingResolve(const std::string& online_id, uint32_t* addr,
                            uint16_t* port, std::string& error);

// Updates signaling keepalive (/mp/matching2/signaling_update).
bool ServerSignalingUpdate(std::string& error);

}  // namespace Extra::Hosts
