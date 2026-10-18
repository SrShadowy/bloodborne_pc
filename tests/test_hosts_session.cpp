// SPDX-License-Identifier: GPL-2.0-or-later
// hosts session test: drives account, session_state, session_room, and
// session_poll against a local in-process httplib server on 127.0.0.1.
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <httplib.h>

#include "hosts/account.h"
#include "hosts/http_client.h"
#include "hosts/session_poll.h"
#include "hosts/session_room.h"
#include "hosts/session_state.h"

namespace {

using namespace Extra::Hosts;

void TestAccount() {
    ClearAccountOverride();
    assert(!IsAccountRefused());

    SetAccountOverride("HunterOne", "secret_tok_456");
    assert(GetOnlineId() == "HunterOne");
    assert(GetAccountToken() == "secret_tok_456");
    assert(IsAccountLoggedIn());
    assert(!IsAccountRefused());

    SetAccountRefused(true);
    assert(IsAccountRefused());
    SetAccountRefused(false);
    assert(!IsAccountRefused());

    ClearAccountOverride();
}

void TestSessionState() {
    PeersClear();
    assert(PeersAll().empty());

    Peer p;
    p.member_id = 1;
    p.online_id = "GoodHunter";
    p.addr = AddrOf("192.168.1.100");
    p.port = 1234;
    p.local_addr = AddrOf("10.0.0.2");
    p.local_port = 1234;
    PeersUpsert(p);

    Peer out;
    assert(PeersGet(1, &out));
    assert(out.online_id == "GoodHunter");
    assert(out.port == 1234);

    assert(PeersFindOnline("GoodHunter", &out));
    assert(out.member_id == 1);

    PeersSetConn(1, 99, 2);
    assert(PeersFindConn(99, &out));
    assert(out.sig_status == 2);

    // Test peers_from_members JSON parsing
    const nlohmann::json members = nlohmann::json::array({
        {
            {"MemberId", 2},
            {"OnlineId", "Cooperator"},
            {"SignalingAddr", "192.168.1.101"},
            {"SignalingPort", 5555},
            {"LocalAddr", "10.0.0.3"},
            {"LocalPort", 5555},
        }
    });
    const int count = PeersFromMembers(members);
    assert(count == 1);
    assert(PeersGet(2, &out));
    assert(out.online_id == "Cooperator");
    assert(out.port == 5555);

    // Test room state
    assert(!InRoom());
    RoomSet("session-xyz", 4242, 1, 1, true);
    assert(InRoom());
    assert(CurrentSessionId() == "session-xyz");
    assert(CurrentRoomId() == 4242);
    assert(CurrentMemberId() == 1);
    assert(CurrentOwnerId() == 1);
    assert(CurrentIsHost());

    RoomClear();
    assert(!InRoom());
    assert(CurrentSessionId().empty());
    assert(CurrentRoomId() == 0);
}

void TestSessionServerFlow() {
    httplib::Server srv;

    std::atomic<int> poll_count{0};
    std::atomic<bool> ack_received{false};

    srv.Post("/mp/matching2/context_start", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json reply = {
            {"ResKind", 0},
            {"MappedAddress", "1.2.3.4"},
            {"MappedPort", 9999},
        };
        res.set_content(reply.dump(), "application/json");
    });

    srv.Post("/mp/matching2/create_room", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json reply = {
            {"ResKind", 0},
            {"SessionId", "sess-room-1"},
            {"RoomId", 777},
            {"MemberId", 1},
            {"OwnerId", 1},
        };
        res.set_content(reply.dump(), "application/json");
    });

    srv.Post("/mp/matching2/join_room", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json reply = {
            {"ResKind", 0},
            {"SessionId", "sess-room-2"},
            {"RoomId", 888},
            {"MemberId", 2},
            {"OwnerId", 1},
            {"Members", nlohmann::json::array({
                {
                    {"MemberId", 1},
                    {"OnlineId", "HostHunter"},
                    {"SignalingAddr", "192.168.1.200"},
                    {"SignalingPort", 4000},
                }
            })},
        };
        res.set_content(reply.dump(), "application/json");
    });

    srv.Post("/mp/matching2/heartbeat", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json reply = {
            {"ResKind", 0},
            {"InRoom", 1},
        };
        res.set_content(reply.dump(), "application/json");
    });

    srv.Post("/mp/matching2/kick_member", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json reply = {{"ResKind", 0}};
        res.set_content(reply.dump(), "application/json");
    });

    srv.Post("/np/signaling/resolve", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json reply = {
            {"ResKind", 0},
            {"Addr", "10.10.10.10"},
            {"Port", 6789},
        };
        res.set_content(reply.dump(), "application/json");
    });

    srv.Post("/mp/matching2/leave_room", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json reply = {{"ResKind", 0}};
        res.set_content(reply.dump(), "application/json");
    });

    srv.Post("/np/events/poll", [&](const httplib::Request&, httplib::Response& res) {
        const int idx = poll_count.fetch_add(1);
        nlohmann::json reply;
        reply["ResKind"] = 0;
        if (idx == 0) {
            reply["HasEvent"] = 1;
            reply["EventCategory"] = "matching2";
            reply["NextCursor"] = 42;
            reply["Name"] = "room_member_joined";
            reply["OnlineId"] = "GuestHunter";
        } else {
            reply["HasEvent"] = 0;
            reply["NextCursor"] = 42;
        }
        res.set_content(reply.dump(), "application/json");
    });

    srv.Post("/np/events/ack", [&](const httplib::Request&, httplib::Response& res) {
        ack_received.store(true);
        nlohmann::json reply = {{"ResKind", 0}};
        res.set_content(reply.dump(), "application/json");
    });

    const int port = srv.bind_to_any_port("127.0.0.1");
    assert(port > 0);
    std::thread serving([&srv] { srv.listen_after_bind(); });
    srv.wait_until_ready();

    setenv("BB_HOST_SCHEME", "http", 1);
    const std::string server = "127.0.0.1:" + std::to_string(port);
    setenv("BB_HOST_SERVER", server.c_str(), 1);

    SetAccountOverride("MyHunter", "test_tok");

    // 1. context_start
    nlohmann::json rep;
    std::string err;
    assert(ServerContextStart(rep, err));
    assert(rep["MappedAddress"] == "1.2.3.4");
    assert(rep["MappedPort"] == 9999);

    // 2. create_room
    assert(ServerCreateRoom(4, {{"Password", "abc"}}, rep, err));
    assert(InRoom());
    assert(CurrentRoomId() == 777);
    assert(CurrentIsHost());

    // 3. heartbeat
    assert(ServerHeartbeat("sess-room-1", 1) == 1);

    // 4. signaling resolve
    uint32_t s_addr = 0;
    uint16_t s_port = 0;
    assert(ServerSignalingResolve("PeerHunter", &s_addr, &s_port, err));
    assert(s_port == 6789);
    assert(s_addr == AddrOf("10.10.10.10"));

    // 5. kick member
    assert(ServerKickMember("sess-room-1", 2, 1, nullptr, 0, err));

    // 6. poller background thread
    std::atomic<bool> got_event{false};
    PollerStart([&](const nlohmann::json& ev) {
        if (ev.value("Name", "") == "room_member_joined") {
            got_event.store(true);
        }
    });
    assert(PollerIsRunning());

    for (int i = 0; i < 20; ++i) {
        if (got_event.load() && ack_received.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    assert(got_event.load());
    assert(ack_received.load());

    PollerStop();
    assert(!PollerIsRunning());

    // 7. leave_room
    assert(ServerLeaveRoom("sess-room-1", 1, rep, err));
    assert(!InRoom());

    // 8. join_room
    assert(ServerJoinRoom(888, rep, err));
    assert(InRoom());
    assert(CurrentRoomId() == 888);
    assert(!CurrentIsHost());
    Peer host_peer;
    assert(PeersGet(1, &host_peer));
    assert(host_peer.online_id == "HostHunter");

    ClearAccountOverride();
    RoomClear();

    srv.stop();
    serving.join();

    unsetenv("BB_HOST_SCHEME");
    unsetenv("BB_HOST_SERVER");
}

}  // namespace

int main() {
    TestAccount();
    TestSessionState();
    TestSessionServerFlow();
    std::puts("test_hosts_session: all tests passed");
    return 0;
}
