// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Integration & unit tests for The Hunter's Dream (bbnet20/thd):
//   1. PollPacing backoff, jitter, WaitMaxMs capping & legacy short-poll fallback.
//   2. PickConnectTimeoutMs 5000 ms floor for TLS/TCP handshakes.
//   3. In-process httplib::Server long polling (/np/events/poll + /np/events/ack),
//      HTTP 401 refusal handling, WaitMaxMs=0 fallback, and fast PollerStop() abort.
//   4. Real loopback 64 KB UDP P2P datagram framing (dgram_send + p2p_reader + EMSGSIZE).
#include "httplib.h"

#include "thd/thd_http_timeout.h"
#include "thd/thd_internal.h"
#include "thd/thd_poll_pacing.h"
#include "include/core/libraries/libs.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace BbEngine {
bool host_read_safe(const void*, void*, std::size_t) { return false; }
} // namespace BbEngine

extern "C" int32_t runtime_thread_spawn(void**, void* PS4_SYSV_ABI (*)(void*), void*, uint64_t,
                                        const char*) {
    return -1;
}

namespace BbThd {
void RegisterMatching2(Core::Loader::SymbolsResolver*) {}
void RegisterNp(Core::Loader::SymbolsResolver*) {}
} // namespace BbThd

namespace {
std::unordered_map<std::string, u64> g_test_syms;
} // namespace

namespace Core::Loader {
void SymbolsResolver::AddSymbol(const char* nid, const char*, const char*, SymbolType,
                                u64 address) {
    if (nid) g_test_syms[nid] = address;
}
} // namespace Core::Loader

namespace {

template <typename Fn>
Fn ResolveNid(const char* nid) {
    auto it = g_test_syms.find(nid);
    assert(it != g_test_syms.end() && it->second != 0);
    return reinterpret_cast<Fn>(it->second);
}

struct SceSockaddrIn {
    std::uint8_t len;
    std::uint8_t family;
    std::uint16_t port;  // nbo
    std::uint32_t addr;  // nbo
    std::uint16_t vport; // nbo
    std::uint8_t zero[6];
};
static_assert(sizeof(SceSockaddrIn) == 16);

std::uint16_t PickFreeUdpPort() {
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd != INVALID_SOCKET);
#else
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0);
#endif
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    assert(::bind(fd, reinterpret_cast<const sockaddr*>(&sa), sizeof(sa)) == 0);
    socklen_t len = sizeof(sa);
    assert(::getsockname(fd, reinterpret_cast<sockaddr*>(&sa), &len) == 0);
    const std::uint16_t port = ntohs(sa.sin_port);
#ifdef _WIN32
    ::closesocket(fd);
#else
    ::close(fd);
#endif
    assert(port != 0);
    return port;
}

void TestPollPacingMath() {
    using namespace BbThd::PollPacing;

    assert(TimeoutFor(0) == kPlainTimeoutMs);
    assert(TimeoutFor(25000) == 25000 + kReadMarginMs);
    assert(ConnectFor(0) == kPlainTimeoutMs);
    assert(ConnectFor(25000) == kConnectMs);

    assert(BackoffAfter(0) == kBackoffFirstMs);
    assert(BackoffAfter(1000) == 2000);
    assert(BackoffAfter(16000) == 30000);
    assert(BackoffAfter(30000) == 30000);

    assert(Jittered(1000, 0.0) == 800);
    assert(Jittered(1000, 0.5) == 1000);
    assert(Jittered(1000, 1.0) >= 1199 && Jittered(1000, 1.0) <= 1200);

    // Long poll server holding 25s -> keeps WaitMs=25000, sleeps 0ms before next poll.
    State s;
    Outcome o;
    o.answered = true;
    o.wait_max_ms = 25000;
    o.elapsed_ms = 25000;
    o.event = false;
    Next n = NextStep(s, o, 0.5);
    assert(n.state.wait_ms == 25000);
    assert(n.state.backoff_ms == 0);
    assert(n.sleep_ms == 0);

    // Legacy server omitting WaitMaxMs -> falls back to wait_ms=0 & 250ms pace.
    o.wait_max_ms = 0;
    o.elapsed_ms = 5;
    n = NextStep(n.state, o, 0.5);
    assert(n.state.wait_ms == 0);
    assert(n.sleep_ms == kPaceMs);

    // Immediate event drain even without WaitMaxMs -> 0ms sleep.
    o.event = true;
    n = NextStep(n.state, o, 0.5);
    assert(n.sleep_ms == 0);

    // HTTP 401 -> 60s refusal sleep.
    Outcome auth_fail;
    auth_fail.answered = false;
    auth_fail.status = 401;
    n = NextStep(State{}, auth_fail, 0.5);
    assert(n.sleep_ms == kRefusedMs);

    // Transient error -> exponential backoff with jitter.
    Outcome transient;
    n = NextStep(State{}, transient, 0.0);
    assert(n.state.backoff_ms == 1000);
    assert(n.sleep_ms == 800);
    n = NextStep(n.state, transient, 1.0);
    assert(n.state.backoff_ms == 2000);
    assert(n.sleep_ms >= 2399 && n.sleep_ms <= 2400);
}

void TestHttpConnectFloor() {
    assert(BbThd::PickConnectTimeoutMs(0, 30, false).count() == 30000);
    assert(BbThd::PickConnectTimeoutMs(500'000, 30, false).count() == 1000);

    assert(BbThd::PickConnectTimeoutMs(0, 30, true).count() == 30000);
    assert(BbThd::PickConnectTimeoutMs(0, 2, true).count() == 5000);
    assert(BbThd::PickConnectTimeoutMs(1'500'000, 30, true).count() == 5000);
    assert(BbThd::PickConnectTimeoutMs(1, 30, true).count() == 5000);
    assert(BbThd::PickConnectTimeoutMs(8'000'000, 30, true).count() == 8000);
}

void TestLongPollLifecycleAndFastStop() {
    httplib::Server srv;
    std::mutex mu;
    std::condition_variable cv;
    int poll_count = 0;
    long long ack_cursor = -1;
    long long poll2_cursor = -1;
    int poll1_wait_ms = -1;
    int poll3_wait_ms = -1;
    bool in_poll3 = false;
    bool release_poll3 = false;
    std::string seen_token;

    srv.Post("/np/events/poll", [&](const httplib::Request& req, httplib::Response& res) {
        const auto j = BbThd::Json::parse(req.body, nullptr, false);
        assert(!j.is_discarded());
        int n = 0;
        {
            std::lock_guard lk(mu);
            n = ++poll_count;
            if (req.has_header("X-BB-Token")) seen_token = req.get_header_value("X-BB-Token");
        }
        if (n == 1) {
            poll1_wait_ms = j.contains("WaitMs") ? j["WaitMs"].get<int>() : -1;
            res.set_content(
                R"({"HasEvent":1,"EventId":42,"Name":"RoomUpdate","NextCursor":7,"WaitMaxMs":25000})",
                "application/json");
            return;
        }
        if (n == 2) {
            poll2_cursor = j.value("Cursor", -1LL);
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            res.set_content(R"({"HasEvent":0,"NextCursor":7,"WaitMaxMs":12000})",
                            "application/json");
            return;
        }
        if (n == 3) {
            poll3_wait_ms = j.contains("WaitMs") ? j["WaitMs"].get<int>() : -1;
            {
                std::lock_guard lk(mu);
                in_poll3 = true;
            }
            cv.notify_all();
            std::unique_lock lk(mu);
            cv.wait_for(lk, std::chrono::seconds(10), [&] { return release_poll3; });
            res.set_content(R"({"HasEvent":0,"NextCursor":7,"WaitMaxMs":12000})",
                            "application/json");
            return;
        }
        res.set_content(R"({"HasEvent":0,"NextCursor":7,"WaitMaxMs":12000})", "application/json");
    });

    srv.Post("/np/events/ack", [&](const httplib::Request& req, httplib::Response& res) {
        const auto j = BbThd::Json::parse(req.body, nullptr, false);
        {
            std::lock_guard lk(mu);
            ack_cursor = j.value("Cursor", -1LL);
        }
        cv.notify_all();
        res.set_content(R"({"Ok":true})", "application/json");
    });

    const int port = srv.bind_to_any_port("127.0.0.1");
    assert(port > 0);
    std::thread srv_thread([&] { srv.listen_after_bind(); });

    BbThd::SetTestServer("http://127.0.0.1:" + std::to_string(port), "HunterTest", "secret-tok");

    std::atomic<long long> got_event_id{-1};
    BbThd::PollerStart([&](const BbThd::Json& ev) { got_event_id.store(ev.value("EventId", -1LL)); });

    {
        std::unique_lock lk(mu);
        const bool reached =
            cv.wait_for(lk, std::chrono::seconds(5), [&] { return in_poll3 && ack_cursor == 7; });
        assert(reached);
    }

    assert(seen_token == "secret-tok");
    assert(got_event_id.load() == 42);
    assert(poll1_wait_ms == 25000);
    assert(poll2_cursor == 7);
    assert(poll3_wait_ms == 12000);

    // PollerStop() must abort the blocked 25s long-poll request in well under 500ms.
    const auto t0 = std::chrono::steady_clock::now();
    BbThd::PollerStop();
    const auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0)
                             .count();
    assert(stop_ms < 500);

    {
        std::lock_guard lk(mu);
        release_poll3 = true;
    }
    cv.notify_all();
    srv.stop();
    srv_thread.join();
}

void Test401RefusalAndLegacyFallback() {
    // Part A: 401 marks account refused and enters 60s slow backoff without spinning.
    {
        httplib::Server srv;
        std::mutex mu;
        std::condition_variable cv;
        std::atomic<int> polls{0};
        srv.Post("/np/events/poll", [&](const httplib::Request&, httplib::Response& res) {
            polls.fetch_add(1);
            res.status = 401;
            res.set_content(R"({"Message":"token expired"})", "application/json");
            cv.notify_all();
        });
        const int port = srv.bind_to_any_port("127.0.0.1");
        assert(port > 0);
        std::thread srv_thread([&] { srv.listen_after_bind(); });

        BbThd::SetTestServer("http://127.0.0.1:" + std::to_string(port), "Hunter401", "bad-tok");
        BbThd::PollerStart([](const BbThd::Json&) {});

        {
            std::unique_lock lk(mu);
            const bool done =
                cv.wait_for(lk, std::chrono::seconds(5), [&] { return polls.load() >= 1 && BbThd::Refused(); });
            assert(done);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        assert(polls.load() == 1);

        const auto t0 = std::chrono::steady_clock::now();
        BbThd::PollerStop();
        const auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count();
        assert(stop_ms < 500);

        srv.stop();
        srv_thread.join();
    }

    // Part B: server omitting WaitMaxMs falls back cleanly to plain polling without WaitMs.
    {
        httplib::Server srv;
        std::mutex mu;
        std::condition_variable cv;
        int polls = 0;
        bool poll2_had_wait_ms = true;
        srv.Post("/np/events/poll", [&](const httplib::Request& req, httplib::Response& res) {
            const auto j = BbThd::Json::parse(req.body, nullptr, false);
            assert(!j.is_discarded());
            {
                std::lock_guard lk(mu);
                ++polls;
                if (polls == 2) {
                    poll2_had_wait_ms = j.contains("WaitMs");
                }
            }
            cv.notify_all();
            res.set_content(R"({"HasEvent":0,"NextCursor":1})", "application/json");
        });
        const int port = srv.bind_to_any_port("127.0.0.1");
        assert(port > 0);
        std::thread srv_thread([&] { srv.listen_after_bind(); });

        BbThd::SetTestServer("http://127.0.0.1:" + std::to_string(port), "HunterLegacy", "tok");
        BbThd::PollerStart([](const BbThd::Json&) {});

        {
            std::unique_lock lk(mu);
            const bool done = cv.wait_for(lk, std::chrono::seconds(5), [&] { return polls >= 2; });
            assert(done);
        }
        assert(!poll2_had_wait_ms);

        BbThd::PollerStop();
        srv.stop();
        srv_thread.join();
    }
}

void TestUdpLoopback64K() {
    Core::Loader::SymbolsResolver sym;
    BbThd::RegisterNet(&sym);

    auto p_init = ResolveNid<s32(PS4_SYSV_ABI*)()>("Nlev7Lg8k3A");
    auto p_term = ResolveNid<s32(PS4_SYSV_ABI*)()>("cTGkc6-TBlI");
    auto p_socket = ResolveNid<s32(PS4_SYSV_ABI*)(const char*, int, int, int)>("Q4qBuN-c0ZM");
    auto p_close = ResolveNid<s32(PS4_SYSV_ABI*)(int)>("45ggEzakPJQ");
    auto p_bind = ResolveNid<s32(PS4_SYSV_ABI*)(int, const void*, int)>("bErx49PgxyY");
    auto p_getsockname = ResolveNid<s32(PS4_SYSV_ABI*)(int, void*, unsigned*)>("hoOAofhhRvE");
    auto p_sendto = ResolveNid<s32(PS4_SYSV_ABI*)(int, const void*, u64, int, const void*, int)>(
        "gvD1greCu0A");
    auto p_recvfrom =
        ResolveNid<s32(PS4_SYSV_ABI*)(int, void*, u64, int, void*, unsigned*)>("304ooNZxWDY");

    assert(p_init() == 0);

    const int s1 = p_socket("p2p1", 2, 6, 0);
    const int s2 = p_socket("p2p2", 2, 6, 0);
    assert(s1 > 0 && s2 > 0);

    SceSockaddrIn sa1{};
    sa1.len = 16;
    sa1.family = 2;
    sa1.vport = htons(101);
    assert(p_bind(s1, &sa1, sizeof(sa1)) == 0);

    SceSockaddrIn sa2{};
    sa2.len = 16;
    sa2.family = 2;
    sa2.vport = htons(202);
    assert(p_bind(s2, &sa2, sizeof(sa2)) == 0);

    SceSockaddrIn bound{};
    unsigned blen = sizeof(bound);
    assert(p_getsockname(s1, &bound, &blen) == 0);
    assert(bound.port != 0);

    SceSockaddrIn dst{};
    dst.len = 16;
    dst.family = 2;
    dst.addr = htonl(INADDR_LOOPBACK);
    dst.port = bound.port;
    dst.vport = htons(202);

    for (const std::size_t sz :
         {std::size_t{2048}, std::size_t{16384}, std::size_t{60000}, BbThd::kMaxDatagram - 4}) {
        std::vector<std::uint8_t> tx(sz);
        for (std::size_t i = 0; i < sz; ++i) {
            tx[i] = static_cast<std::uint8_t>((i * 131u + sz) & 0xffu);
        }
        const s32 sent = p_sendto(s1, tx.data(), tx.size(), 0, &dst, sizeof(dst));
        assert(sent == static_cast<s32>(sz));

        std::vector<std::uint8_t> rx(BbThd::kMaxDatagram);
        SceSockaddrIn from{};
        unsigned from_len = sizeof(from);
        const s32 recvd = p_recvfrom(s2, rx.data(), rx.size(), 0, &from, &from_len);
        assert(recvd == static_cast<s32>(sz));
        assert(from.vport == htons(101));
        assert(std::memcmp(rx.data(), tx.data(), sz) == 0);
    }

    // Payload == kMaxDatagram leaves no room for the 4-byte P2P vport header -> EMSGSIZE (40).
    std::vector<std::uint8_t> oversize(BbThd::kMaxDatagram, 0xAB);
    const s32 err_ret = p_sendto(s1, oversize.data(), oversize.size(), 0, &dst, sizeof(dst));
    assert(err_ret == static_cast<s32>(0x80410100u | 40u));

    assert(p_close(s1) == 0);
    assert(p_close(s2) == 0);
    assert(p_term() == 0);
}

} // namespace

int main() {
    const std::uint16_t free_udp = PickFreeUdpPort();
    const std::string port_str = std::to_string(free_udp);
#ifdef _WIN32
    _putenv_s("BB_P2P_PORT", port_str.c_str());
    _putenv_s("BB_THD_STUN", "off");
#else
    setenv("BB_P2P_PORT", port_str.c_str(), 1);
    setenv("BB_THD_STUN", "off", 1);
#endif

    TestPollPacingMath();
    TestHttpConnectFloor();
    TestLongPollLifecycleAndFastStop();
    Test401RefusalAndLegacyFallback();
    TestUdpLoopback64K();

    std::printf("PASS test_thd_net\n");
    return 0;
}
