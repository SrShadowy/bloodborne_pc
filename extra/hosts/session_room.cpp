// SPDX-License-Identifier: GPL-2.0-or-later
#include "session_room.h"
#include "account.h"
#include "http_client.h"
#include "session_state.h"

namespace Extra::Hosts {
namespace {

bool SendAndParse(const std::string& path, const nlohmann::json& body,
                  nlohmann::json& out, std::string& error, int timeout_ms = 5000) {
    const HttpResult res = PostJson(path, body, GetAccountToken(), timeout_ms);
    if (res.status == 401) {
        SetAccountRefused(true);
    }
    if (!ParseReply(res, out, error)) {
        return false;
    }
    return CheckResKind(out, error);
}

std::string Base64Encode(const uint8_t* data, size_t len) {
    static const char* const kChars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    if (!data || len == 0) return out;
    len = len > 16 ? 16 : len;
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0u;
        const uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0u;
        const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out += kChars[(triple >> 18) & 63];
        out += kChars[(triple >> 12) & 63];
        out += (i + 1 < len) ? kChars[(triple >> 6) & 63] : '=';
        out += (i + 2 < len) ? kChars[triple & 63] : '=';
    }
    return out;
}

}  // namespace

bool ServerContextStart(nlohmann::json& reply, std::string& error) {
    std::string mapped_a;
    int mapped_p = 0;
    GetMappedAddress(mapped_a, mapped_p);
    const nlohmann::json body = {
        {"OnlineId", GetOnlineId()},
        {"SignalingAddr", LocalAddrText()},
        {"SignalingPort", P2pPort()},
        {"LocalAddr", LocalAddrText()},
        {"LocalPort", P2pPort()},
        {"MappedAddr", mapped_a},
        {"MappedPort", mapped_p},
    };
    if (!SendAndParse("/mp/matching2/context_start", body, reply, error, 4000)) {
        return false;
    }
    const std::string s_addr = StrOf(reply, "MappedAddress");
    const int s_port = static_cast<int>(IntOf(reply, "MappedPort", 0));
    if (!s_addr.empty() && s_port > 0) {
        SetMappedAddress(s_addr, s_port);
    }
    return true;
}

bool ServerCreateRoom(int max_members, const nlohmann::json& extra,
                      nlohmann::json& reply, std::string& error) {
    nlohmann::json body = extra;
    body["OnlineId"] = GetOnlineId();
    body["MaxMembers"] = max_members;
    body["SignalingAddr"] = LocalAddrText();
    body["SignalingPort"] = P2pPort();
    std::string mapped_a;
    int mapped_p = 0;
    GetMappedAddress(mapped_a, mapped_p);
    if (!mapped_a.empty() && mapped_p > 0) {
        body["MappedAddr"] = mapped_a;
        body["MappedPort"] = mapped_p;
    }

    if (!SendAndParse("/mp/matching2/create_room", body, reply, error, 5000)) {
        return false;
    }

    const std::string sess = StrOf(reply, "SessionId");
    const uint64_t rid = static_cast<uint64_t>(IntOf(reply, "RoomId", 0));
    const uint16_t mid = static_cast<uint16_t>(IntOf(reply, "MemberId", 1));
    const uint16_t oid = static_cast<uint16_t>(IntOf(reply, "OwnerId", mid));
    RoomSet(sess, rid, mid, oid, true);
    return true;
}

bool ServerJoinRoom(uint64_t room_id, nlohmann::json& reply, std::string& error) {
    nlohmann::json body = {
        {"OnlineId", GetOnlineId()},
        {"RoomId", room_id},
        {"SignalingAddr", LocalAddrText()},
        {"SignalingPort", P2pPort()},
    };
    std::string mapped_a;
    int mapped_p = 0;
    GetMappedAddress(mapped_a, mapped_p);
    if (!mapped_a.empty() && mapped_p > 0) {
        body["MappedAddr"] = mapped_a;
        body["MappedPort"] = mapped_p;
    }

    if (!SendAndParse("/mp/matching2/join_room", body, reply, error, 5000)) {
        return false;
    }

    const std::string sess = StrOf(reply, "SessionId");
    const uint64_t rid = static_cast<uint64_t>(IntOf(reply, "RoomId", room_id));
    const uint16_t mid = static_cast<uint16_t>(IntOf(reply, "MemberId", 0));
    const uint16_t oid = static_cast<uint16_t>(IntOf(reply, "OwnerId", 1));
    RoomSet(sess, rid, mid, oid, false);

    if (reply.contains("Members")) {
        PeersFromMembers(reply["Members"]);
    }
    return true;
}

bool ServerLeaveRoom(const std::string& session_id, int member_id,
                     nlohmann::json& reply, std::string& error) {
    const nlohmann::json body = {
        {"OnlineId", GetOnlineId()},
        {"SessionId", session_id},
        {"MemberId", member_id},
    };
    const bool ok = SendAndParse("/mp/matching2/leave_room", body, reply, error, 4000);
    RoomClear();
    return ok;
}

int ServerHeartbeat(const std::string& session_id, int member_id) {
    const nlohmann::json body = {
        {"OnlineId", GetOnlineId()},
        {"SessionId", session_id},
        {"MemberId", member_id},
    };
    nlohmann::json reply;
    std::string error;
    if (!SendAndParse("/mp/matching2/heartbeat", body, reply, error, 3000)) {
        return -1;
    }
    return static_cast<int>(IntOf(reply, "InRoom", 1));
}

bool ServerKickMember(const std::string& session_id, int member_id, int kicker_id,
                      const uint8_t* opt, size_t opt_len, std::string& error) {
    const nlohmann::json body = {
        {"OnlineId", GetOnlineId()},
        {"SessionId", session_id},
        {"TargetMemberId", member_id},
        {"KickerMemberId", kicker_id},
        {"OptData", Base64Encode(opt, opt_len)},
    };
    nlohmann::json reply;
    return SendAndParse("/mp/matching2/kick_member", body, reply, error, 4000);
}

bool ServerSignalingResolve(const std::string& online_id, uint32_t* addr,
                            uint16_t* port, std::string& error) {
    const nlohmann::json body = {{"OnlineId", online_id}};
    nlohmann::json reply;
    if (!SendAndParse("/np/signaling/resolve", body, reply, error, 4000)) {
        return false;
    }
    const uint32_t a = AddrOf(StrOf(reply, "Addr"));
    const int p = static_cast<int>(IntOf(reply, "Port", 0));
    if (a == 0 || p <= 0 || p > 65535) {
        error = "no address returned";
        return false;
    }
    if (addr) *addr = a;
    if (port) *port = static_cast<uint16_t>(p);
    return true;
}

bool ServerSignalingUpdate(std::string& error) {
    std::string mapped_a;
    int mapped_p = 0;
    GetMappedAddress(mapped_a, mapped_p);
    const nlohmann::json body = {
        {"OnlineId", GetOnlineId()},
        {"MappedAddr", mapped_a},
        {"MappedPort", mapped_p},
        {"SessionId", CurrentSessionId()},
        {"MemberId", CurrentMemberId()},
    };
    nlohmann::json reply;
    return SendAndParse("/mp/matching2/signaling_update", body, reply, error, 4000);
}

}  // namespace Extra::Hosts
