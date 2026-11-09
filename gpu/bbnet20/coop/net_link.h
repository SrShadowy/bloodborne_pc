// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/types.h"
#include "coop/link_auth.h"

// The server link (docs/SERVER_COOP_PLAN.md phase 2): every game keeps one UDP link to shadNet
// and sends everything it would send to a peer through it; nothing goes game to game. The link
// runs on the P2P transport's own socket, to shadNet's matching UDP port (the STUN ping's), so the
// address shadNet records for a game there is the one the other games already know it by: the
// server forwards a frame to the game at its destination and names the sender by that address, and
// the games, signaling and NP emulation see the same addresses as before.
//
// A datagram: the signaling vport header (0xffff, 0xffff), "SHLK", the kind, its body. Numbers
// are little-endian; addresses and ports as sockaddr_in holds them (network byte order).
// server/tests/contracts/server_link_frames.txt has one of each; shadNet's server_link.h reads
// and writes the same.
//
// Link version 4 (server/coop/link_auth.h): the first hello gets a cookie and the server's key
// back; the hello that echoes it is a Noise NK handshake sealed to that key (the launcher's, or the
// first cookie's once a welcome proved it), and signs in; its answer finishes the handshake; every
// datagram after it, both ways, is encrypted and ends in a counter and a tag (the link's
// TrailerBytes). One that does not open is dropped.
//
// A hosted server asks a first sign-in (an id with no account) for work: the hello's answer is
// Work with the bits, and the game says hello again with a nonce that does them (LinkAuth::
// SolveWork, off the link's thread), as a new handshake. An account never pays twice.
namespace Libraries::Net::Link {

// 2 (phase 11): the hello carries the player id's secret and signs the player in.
// 3: a cookie first, then every datagram after the hello tagged.
// 4: the hello and its answer a Noise NK handshake, every datagram after them encrypted.
constexpr u8 LinkVersion = 4;
enum class Kind : u8 {
    Hello = 1,   // game -> server: link version, cookie, then sealed: online id, build, secret
    Welcome = 2, // server -> game: accepted, or refused with the reason (another build)
    Data = 3,    // a frame for the peer at addr:port (game -> server), from it (server -> game)
    Ping = 4,    // game -> server, once a second
    Pong = 5,    // server -> game: the ping back
    Member = 6,  // server -> game: another member's link went quiet, came back or is gone
    // Both ways: our messages (server/coop/coop.proto) between the game and shadNet itself, one
    // frame of their reliable stream (server/coop/coop_stream.h). Phase 3: the party's world.
    Coop = 7,
    // Both ways (phase 11): the game's calls for shadNet's TCP port (the NP sign-in, friends,
    // scores), one frame of a second reliable stream (server/coop/rpc_tunnel.h): one port only.
    Rpc = 8,
    Cookie = 9, // server -> game: a hello without a valid cookie's answer, with the server's key
    Work = 10,  // server -> game (hosted): no account for the id yet: the work its hello needs
    // server -> game (a region of shards, docs/SCALING.md): continue on another shard, at its
    // address with its key; the hellos there carry the ticket. A hello's answer, or on the link.
    Redirect = 11,
};
struct Hello {
    u8 version = LinkVersion;
    std::string online_id;
    // Which game this is: every member and the server run one build (the game's 0x60 record
    // carries a raw vtable pointer, so another eboot cannot even read the packets).
    std::string build;
    // Phase 11: the player id's secret (the launcher made it once): the hello is the sign-in.
    // Sealed to the server's key, so only in a hello that echoes the server's cookie.
    std::string secret;
    LinkAuth::Cookie cookie{}; // zeros before the server gave one
    std::optional<u64> work;   // hosted: a new account's work nonce, after the secret
    // A region: the ticket of the shard that sent the game here (opaque), after the work's 8
    // bytes (0: none done), its size a u16.
    std::vector<u8> ticket;
};
struct Redirect {
    u32 addr_nbo = 0;
    u16 port_nbo = 0;
    LinkAuth::Key key{};
    std::vector<u8> ticket;
    std::string text;
};
struct Welcome {
    bool accepted = false;
    std::string text;
};
struct Data {
    u32 addr_nbo = 0;
    u16 port_nbo = 0;
    std::span<const u8> frame; // the vport header and the payload, as P2P sent them
};
struct Ping {
    u32 seq = 0;
    u64 sent_ms = 0;
};
enum class MemberState : u8 { Quiet = 0, Back = 1, Gone = 2 };
struct Member {
    MemberState state = MemberState::Quiet;
    u32 gap_ms = 0; // how long nothing came from it
    u32 addr_nbo = 0;
    u16 port_nbo = 0;
    std::string online_id;
};
struct ServerEndpoint {
    u32 addr_nbo = 0;
    u16 port_nbo = 0;
};

constexpr size_t HeaderSize = 4 + 4 + 1; // vports, "SHLK", kind
constexpr size_t DataHeaderSize = HeaderSize + 4 + 2;

namespace Detail {

inline std::vector<u8> Start(Kind kind) {
    return {0xff, 0xff, 0xff, 0xff, 'S', 'H', 'L', 'K', static_cast<u8>(kind)};
}

template <typename T>
void Put(std::vector<u8>& out, T value) {
    for (size_t index = 0; index < sizeof(T); ++index) {
        out.push_back(static_cast<u8>(static_cast<u64>(value) >> (8 * index)));
    }
}

inline void PutRaw(std::vector<u8>& out, const void* data, size_t size) {
    const auto* bytes = static_cast<const u8*>(data);
    out.insert(out.end(), bytes, bytes + size);
}

inline void PutText(std::vector<u8>& out, std::string_view text) {
    const size_t size = std::min<size_t>(text.size(), 0xFF);
    out.push_back(static_cast<u8>(size));
    PutRaw(out, text.data(), size);
}

// Reads a body from its start; every read fails once the bytes run out.
class Reader {
public:
    explicit Reader(std::span<const u8> bytes) : bytes{bytes} {}

    template <typename T>
    bool Get(T& value) {
        if (bytes.size() - at < sizeof(T)) {
            return false;
        }
        u64 read = 0;
        for (size_t index = 0; index < sizeof(T); ++index) {
            read |= u64{bytes[at + index]} << (8 * index);
        }
        value = static_cast<T>(read);
        at += sizeof(T);
        return true;
    }

    bool GetRaw(void* out, size_t size) {
        if (bytes.size() - at < size) {
            return false;
        }
        std::memcpy(out, bytes.data() + at, size);
        at += size;
        return true;
    }

    bool GetText(std::string& text) {
        u8 size = 0;
        if (!Get(size) || bytes.size() - at < size) {
            return false;
        }
        text.assign(reinterpret_cast<const char*>(bytes.data() + at), size);
        at += size;
        return true;
    }

    std::span<const u8> Rest() const {
        return bytes.subspan(at);
    }

private:
    std::span<const u8> bytes;
    size_t at = 0;
};

// The body of a datagram of `kind`, or nothing.
inline std::optional<Reader> Body(std::span<const u8> datagram, Kind kind) {
    if (datagram.size() < HeaderSize || datagram[8] != static_cast<u8>(kind)) {
        return std::nullopt;
    }
    static constexpr u8 head[8] = {0xff, 0xff, 0xff, 0xff, 'S', 'H', 'L', 'K'};
    if (std::memcmp(datagram.data(), head, sizeof(head)) != 0) {
        return std::nullopt;
    }
    return Reader{datagram.subspan(HeaderSize)};
}

} // namespace Detail

// The kind of a link datagram, or nothing for anything else (signaling, the STUN ping, SHAD).
inline std::optional<Kind> KindOf(std::span<const u8> datagram) {
    if (datagram.size() < HeaderSize) {
        return std::nullopt;
    }
    const u8 kind = datagram[8];
    if (kind < static_cast<u8>(Kind::Hello) || kind > static_cast<u8>(Kind::Redirect)) {
        return std::nullopt;
    }
    if (!Detail::Body(datagram, static_cast<Kind>(kind))) {
        return std::nullopt;
    }
    return static_cast<Kind>(kind);
}

inline std::vector<u8> Encode(const Hello& hello) {
    auto out = Detail::Start(Kind::Hello);
    out.push_back(hello.version);
    Detail::PutRaw(out, hello.cookie.data(), hello.cookie.size());
    Detail::PutText(out, hello.online_id);
    Detail::PutText(out, hello.build);
    Detail::PutText(out, hello.secret);
    if (hello.work || !hello.ticket.empty()) {
        Detail::Put(out, hello.work.value_or(0));
    }
    if (!hello.ticket.empty()) {
        Detail::Put(out, static_cast<u16>(hello.ticket.size()));
        Detail::PutRaw(out, hello.ticket.data(), hello.ticket.size());
    }
    return out;
}

inline std::vector<u8> Encode(const Redirect& redirect) {
    auto out = Detail::Start(Kind::Redirect);
    Detail::PutRaw(out, &redirect.addr_nbo, sizeof(redirect.addr_nbo));
    Detail::PutRaw(out, &redirect.port_nbo, sizeof(redirect.port_nbo));
    Detail::PutRaw(out, redirect.key.data(), redirect.key.size());
    Detail::Put(out, static_cast<u16>(redirect.ticket.size()));
    Detail::PutRaw(out, redirect.ticket.data(), redirect.ticket.size());
    Detail::PutText(out, redirect.text);
    return out;
}

inline std::optional<Redirect> DecodeRedirect(std::span<const u8> datagram) {
    auto body = Detail::Body(datagram, Kind::Redirect);
    Redirect redirect;
    u16 size = 0;
    if (!body || !body->GetRaw(&redirect.addr_nbo, sizeof(redirect.addr_nbo)) ||
        !body->GetRaw(&redirect.port_nbo, sizeof(redirect.port_nbo)) ||
        !body->GetRaw(redirect.key.data(), redirect.key.size()) || !body->Get(size)) {
        return std::nullopt;
    }
    redirect.ticket.resize(size);
    if (!body->GetRaw(redirect.ticket.data(), size) || !body->GetText(redirect.text)) {
        return std::nullopt;
    }
    return redirect;
}

inline std::vector<u8> Encode(const Welcome& welcome) {
    auto out = Detail::Start(Kind::Welcome);
    out.push_back(welcome.accepted ? 1 : 0);
    Detail::PutText(out, welcome.text);
    return out;
}

inline std::vector<u8> EncodeData(u32 addr_nbo, u16 port_nbo, std::span<const u8> frame) {
    auto out = Detail::Start(Kind::Data);
    out.reserve(DataHeaderSize + frame.size());
    Detail::PutRaw(out, &addr_nbo, sizeof(addr_nbo));
    Detail::PutRaw(out, &port_nbo, sizeof(port_nbo));
    out.insert(out.end(), frame.begin(), frame.end());
    return out;
}

inline std::vector<u8> Encode(Kind kind, const Ping& ping) {
    auto out = Detail::Start(kind);
    Detail::Put(out, ping.seq);
    Detail::Put(out, ping.sent_ms);
    return out;
}

inline std::vector<u8> Encode(const Member& member) {
    auto out = Detail::Start(Kind::Member);
    out.push_back(static_cast<u8>(member.state));
    Detail::Put(out, member.gap_ms);
    Detail::PutRaw(out, &member.addr_nbo, sizeof(member.addr_nbo));
    Detail::PutRaw(out, &member.port_nbo, sizeof(member.port_nbo));
    Detail::PutText(out, member.online_id);
    return out;
}

inline std::vector<u8> EncodeCoop(std::span<const u8> body) {
    auto out = Detail::Start(Kind::Coop);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

inline std::optional<std::span<const u8>> DecodeCoop(std::span<const u8> datagram) {
    auto body = Detail::Body(datagram, Kind::Coop);
    if (!body) {
        return std::nullopt;
    }
    return body->Rest();
}

inline std::vector<u8> EncodeRpc(std::span<const u8> body) {
    auto out = Detail::Start(Kind::Rpc);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

inline std::optional<std::span<const u8>> DecodeRpc(std::span<const u8> datagram) {
    auto body = Detail::Body(datagram, Kind::Rpc);
    if (!body) {
        return std::nullopt;
    }
    return body->Rest();
}

inline std::optional<Hello> DecodeHello(std::span<const u8> datagram) {
    auto body = Detail::Body(datagram, Kind::Hello);
    Hello hello;
    if (!body || !body->Get(hello.version) || hello.version != LinkVersion ||
        !body->GetRaw(hello.cookie.data(), hello.cookie.size()) ||
        !body->GetText(hello.online_id) || !body->GetText(hello.build) ||
        !body->GetText(hello.secret)) {
        return std::nullopt;
    }
    if (u64 work = 0; body->Get(work)) {
        hello.work = work;
    }
    if (u16 size = 0; body->Get(size)) {
        hello.ticket.resize(size);
        if (!body->GetRaw(hello.ticket.data(), size)) {
            return std::nullopt;
        }
    }
    return hello;
}

// The cookie, the bits a new account's work needs there (0: none), the server's public key.
inline std::vector<u8> EncodeCookie(const LinkAuth::Cookie& cookie, int work_bits,
                                    const LinkAuth::Key& key) {
    auto out = Detail::Start(Kind::Cookie);
    Detail::PutRaw(out, cookie.data(), cookie.size());
    out.push_back(static_cast<u8>(work_bits));
    Detail::PutRaw(out, key.data(), key.size());
    return out;
}

inline std::optional<LinkAuth::Cookie> DecodeCookie(std::span<const u8> datagram,
                                                    int* work_bits = nullptr,
                                                    LinkAuth::Key* key = nullptr) {
    auto body = Detail::Body(datagram, Kind::Cookie);
    LinkAuth::Cookie cookie;
    u8 bits = 0;
    LinkAuth::Key said;
    if (!body || !body->GetRaw(cookie.data(), cookie.size()) || !body->Get(bits) ||
        !body->GetRaw(said.data(), said.size())) {
        return std::nullopt;
    }
    if (work_bits) {
        *work_bits = bits;
    }
    if (key) {
        *key = said;
    }
    return cookie;
}

inline std::vector<u8> EncodeWork(int bits) {
    auto out = Detail::Start(Kind::Work);
    out.push_back(static_cast<u8>(bits));
    return out;
}

inline std::optional<int> DecodeWork(std::span<const u8> datagram) {
    auto body = Detail::Body(datagram, Kind::Work);
    u8 bits = 0;
    if (!body || !body->Get(bits)) {
        return std::nullopt;
    }
    return bits;
}

inline std::optional<Welcome> DecodeWelcome(std::span<const u8> datagram) {
    auto body = Detail::Body(datagram, Kind::Welcome);
    u8 accepted = 0;
    Welcome welcome;
    if (!body || !body->Get(accepted) || !body->GetText(welcome.text)) {
        return std::nullopt;
    }
    welcome.accepted = accepted != 0;
    return welcome;
}

inline std::optional<Data> DecodeData(std::span<const u8> datagram) {
    auto body = Detail::Body(datagram, Kind::Data);
    Data data;
    if (!body || !body->GetRaw(&data.addr_nbo, sizeof(data.addr_nbo)) ||
        !body->GetRaw(&data.port_nbo, sizeof(data.port_nbo))) {
        return std::nullopt;
    }
    data.frame = body->Rest();
    return data;
}

inline std::optional<Ping> DecodePing(Kind kind, std::span<const u8> datagram) {
    auto body = Detail::Body(datagram, kind);
    Ping ping;
    if (!body || !body->Get(ping.seq) || !body->Get(ping.sent_ms)) {
        return std::nullopt;
    }
    return ping;
}

inline std::optional<Member> DecodeMember(std::span<const u8> datagram) {
    auto body = Detail::Body(datagram, Kind::Member);
    u8 state = 0;
    Member member;
    if (!body || !body->Get(state) || state > static_cast<u8>(MemberState::Gone) ||
        !body->Get(member.gap_ms) || !body->GetRaw(&member.addr_nbo, sizeof(member.addr_nbo)) ||
        !body->GetRaw(&member.port_nbo, sizeof(member.port_nbo)) ||
        !body->GetText(member.online_id)) {
        return std::nullopt;
    }
    member.state = static_cast<MemberState>(state);
    return member;
}

// The game's hello for its link's state, as it goes: without a cookie it asks for one (nothing
// secret in it); with one it is the handshake sealed to the server's key, the same datagram until
// its bytes change (`fresh`: the ephemeral key of a new one). `ticket`: a region's, from the
// Redirect that sent the game to this shard.
inline std::vector<u8> HelloDatagram(LinkAuth::Client& auth, std::string online_id,
                                     std::string build, std::string_view secret,
                                     const LinkAuth::Key& fresh, std::span<const u8> ticket = {}) {
    Hello hello;
    hello.online_id = std::move(online_id);
    hello.build = std::move(build);
    hello.secret = std::string{secret};
    if (auth.HasCookie()) {
        hello.cookie = auth.CookieToEcho();
        hello.work = auth.WorkToSend();
        hello.ticket.assign(ticket.begin(), ticket.end());
    }
    return auth.SealHello(Encode(hello), fresh);
}

// A datagram to the server, encrypted, with its counter and tag; nothing while the link has no
// session yet (the server would drop it). The hello goes by HelloDatagram.
inline std::optional<std::vector<u8>> Sealed(LinkAuth::Client& auth, std::vector<u8> datagram) {
    const auto trailer = auth.Seal(datagram);
    if (!trailer) {
        return std::nullopt;
    }
    datagram.insert(datagram.end(), trailer->begin(), trailer->end());
    return datagram;
}

// A datagram from the server opened: the plain datagram, or nothing when it does not open (not
// from the server, changed on the way, a replay, or an answer to another hello). An accepted
// welcome to the hello under way makes its keys the link's.
inline std::optional<std::vector<u8>> Opened(LinkAuth::Client& auth, std::span<const u8> datagram) {
    return auth.Open(datagram);
}

// Whether a frame for addr:port goes through the server: everything but what is for the server
// itself (the link's own kinds), once the server is known.
inline bool RoutesThroughServer(const ServerEndpoint& server, u32 addr_nbo, u16 port_nbo) {
    if (server.addr_nbo == 0 || server.port_nbo == 0) {
        return false;
    }
    return addr_nbo != server.addr_nbo || port_nbo != server.port_nbo;
}

// No direct P2P (phase 6, every build since phase 9): a game's frame for a peer goes out only
// through the server; before the link is up it is dropped (the game sends its own again).
// Datagrams are taken only from the server.
inline bool SendsToPeer(const ServerEndpoint& server) {
    return server.addr_nbo != 0 && server.port_nbo != 0;
}
inline bool TakesDatagramFrom(const ServerEndpoint& server, u32 addr_nbo, u16 port_nbo) {
    return SendsToPeer(server) && addr_nbo == server.addr_nbo && port_nbo == server.port_nbo;
}

// Phase 7: the peers are the server's room members at their virtual addresses, every frame
// through the server, which knows who is linked; a member is connected at once (no signaling
// handshake), and the server knows this game's address from its hello (no STUN).
inline bool ConnectsWithoutHandshake(const ServerEndpoint& server) {
    return SendsToPeer(server);
}

// A game's virtual address (host byte order), the one the server gives it from its online id
// (server_link.h VirtualEndpointOf): 10.x.y.z, port 3658. What the game's own NAT queries see
// as its address, never its real one.
struct VirtualAddress {
    u32 ipv4 = 0;
    u16 port = 0;
};
constexpr u16 VirtualPort = 3658;
inline VirtualAddress VirtualAddressOf(std::string_view online_id) {
    u32 hash = 2166136261u; // FNV-1a
    for (const char c : online_id) {
        hash = (hash ^ static_cast<u8>(c)) * 16777619u;
    }
    u32 host = hash & 0xFFFFFF;
    if ((host & 0xFF) == 0 || (host & 0xFF) == 0xFF) {
        host ^= 0x01; // no .0 or .255
    }
    return {(10u << 24) | host, VirtualPort};
}

// The link to the server going quiet (no pong for quiet_ms) or answering again.
struct LinkChange {
    bool quiet = false;
    s64 gap_ms = 0; // since the last pong (or the first ping, when none came)
};
// Round trips over the last summary's time.
struct PingSummary {
    u32 pings = 0;
    u32 pongs = 0;
    s64 min_ms = 0;
    s64 avg_ms = 0;
    s64 max_ms = 0;
};
// The game's side of the link's liveness: a ping a second, the round trip of each pong, an
// outage when the server stops answering (each reported once), and a summary every 10 s.
// Whether the link's hello goes now: not once the server took it; once a second, every 10 s
// after a refusal.
constexpr s64 HelloRetryMs = 1000;
constexpr s64 RefusedHelloRetryMs = 10'000;
inline bool HelloDue(bool welcomed, std::optional<s64> last_hello_ms, bool refused, s64 now_ms) {
    if (welcomed) {
        return false;
    }
    return !last_hello_ms ||
           now_ms - *last_hello_ms >= (refused ? RefusedHelloRetryMs : HelloRetryMs);
}

// Phase 11: the link starts with the NP sign-in, before the co-op code says the game's build; a
// game welcomed without one says hello again once it is known (shadNet holds builds against each
// other only once said).
inline bool SaysHelloAgainForBuild(std::string_view before, std::string_view now) {
    return before.empty() && !now.empty();
}

class PingMeter {
public:
    explicit PingMeter(s64 interval_ms = 1000, s64 quiet_ms = 3000, s64 summary_ms = 10'000)
        : interval_ms{interval_ms}, quiet_ms{quiet_ms}, summary_ms{summary_ms} {}

    // The next ping, when one is due.
    std::optional<Ping> Due(s64 now_ms) {
        if (sent_any && now_ms - last_sent_ms < interval_ms) {
            return std::nullopt;
        }
        if (!sent_any) {
            first_sent_ms = now_ms;
            summary_from_ms = now_ms;
        }
        sent_any = true;
        last_sent_ms = now_ms;
        ++pings;
        return Ping{next_seq++, static_cast<u64>(now_ms)};
    }

    // A pong: its round trip counted, and the link back when it was reported quiet.
    std::optional<LinkChange> Pong(const Ping& pong, s64 now_ms) {
        if (!sent_any || pong.seq >= next_seq || static_cast<s64>(pong.sent_ms) > now_ms) {
            return std::nullopt; // not a ping of ours
        }
        const s64 rtt = now_ms - static_cast<s64>(pong.sent_ms);
        min_ms = pongs == 0 ? rtt : std::min(min_ms, rtt);
        max_ms = pongs == 0 ? rtt : std::max(max_ms, rtt);
        total_ms += rtt;
        ++pongs;
        std::optional<LinkChange> change;
        if (reported) {
            reported = false;
            change = LinkChange{false, now_ms - Heard()};
        }
        last_pong_ms = now_ms;
        answered = true;
        return change;
    }

    // The link newly quiet: pings going out and no pong for longer than quiet_ms.
    std::optional<LinkChange> Check(s64 now_ms) {
        if (!sent_any || reported) {
            return std::nullopt;
        }
        const s64 gap = now_ms - Heard();
        if (gap <= quiet_ms) {
            return std::nullopt;
        }
        reported = true;
        return LinkChange{true, gap};
    }

    // The round trips since the last summary, every summary_ms.
    std::optional<PingSummary> TakeSummary(s64 now_ms) {
        if (!sent_any || now_ms - summary_from_ms < summary_ms) {
            return std::nullopt;
        }
        PingSummary summary{pings, pongs, min_ms, pongs == 0 ? 0 : total_ms / pongs, max_ms};
        summary_from_ms = now_ms;
        pings = pongs = 0;
        min_ms = max_ms = total_ms = 0;
        return summary;
    }

    bool Quiet() const {
        return reported;
    }

private:
    s64 Heard() const {
        return answered ? last_pong_ms : first_sent_ms;
    }

    s64 interval_ms;
    s64 quiet_ms;
    s64 summary_ms;
    u32 next_seq = 0;
    bool sent_any = false;
    bool answered = false;
    bool reported = false;
    s64 first_sent_ms = 0;
    s64 last_sent_ms = 0;
    s64 last_pong_ms = 0;
    s64 summary_from_ms = 0;
    u32 pings = 0;
    u32 pongs = 0;
    s64 min_ms = 0;
    s64 max_ms = 0;
    s64 total_ms = 0;
};

} // namespace Libraries::Net::Link
