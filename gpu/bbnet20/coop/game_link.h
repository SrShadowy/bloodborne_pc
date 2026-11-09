// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "coop/coop_types.h"

// Our reliability layer (shared with shadNet: its world stream, coop/coop_stream.h) for the game's
// own packets (docs/SERVER_COOP_PLAN.md phase 2b): the game's
// NetSync packets ({u32 channel, u32 size, payload}) leave at its transport send (0x00C8F0A0) and
// travel in our frames (SHAD GamePacketsType) over the server link instead of the game's
// ExtNetwork2 streams; the receiver hands them to the member's queues as the game's own receive
// does.
//
// Per member and class: reliable packets (channels 7 and up) carry a sequence number, are
// resent until acked and delivered in order, once; unreliable ones (0-6) are never resent and
// the newest wins per channel (an older one arriving late is dropped). Each game has an instance
// number per run: a member whose game restarted shows a new one, and both streams with it start
// again (what the old game missed is dropped).
namespace BloodborneCoop {

constexpr u8 GamePacketsType = 0x40;
constexpr u8 GameFrameVersion = 1;

enum class PacketClass : u8 { Unreliable = 0, Reliable = 1 };

struct GameFrameEntry {
    PacketClass packet_class = PacketClass::Unreliable;
    u32 seq = 0;
    std::vector<u8> packet;
    bool operator==(const GameFrameEntry&) const = default;
};

struct GameFrame {
    std::string sender;    // the sender's online id
    u32 instance = 0;      // the sender's run
    u32 peer_instance = 0; // the receiver's run the ack is for, 0 before the sender heard it
    u32 ack = 0;           // every reliable packet before this one arrived
    std::vector<GameFrameEntry> entries;
    bool operator==(const GameFrame&) const = default;
};

namespace GameLinkDetail {

template <typename T>
void Put(std::vector<u8>& out, T value) {
    for (size_t index = 0; index < sizeof(T); ++index) {
        out.push_back(static_cast<u8>(static_cast<u64>(value) >> (8 * index)));
    }
}

template <typename T>
bool Get(std::span<const u8> bytes, size_t& at, T& value) {
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

constexpr size_t FrameHeaderBytes = 1 + 1 + 16 + 4 + 4 + 4 + 2;
constexpr size_t EntryHeaderBytes = 1 + 4 + 4;

} // namespace GameLinkDetail

// The payload after "SHAD" and the type: version, sender (u8 length first), instance,
// peer_instance, ack, entry count (u16), entries {class u8, seq u32, size u32, packet}.
inline std::vector<u8> EncodeGameFrame(const GameFrame& frame) {
    using namespace GameLinkDetail;
    std::vector<u8> out;
    out.push_back(GameFrameVersion);
    const size_t sender = std::min<size_t>(frame.sender.size(), 0xFF);
    out.push_back(static_cast<u8>(sender));
    out.insert(out.end(), frame.sender.begin(), frame.sender.begin() + sender);
    Put(out, frame.instance);
    Put(out, frame.peer_instance);
    Put(out, frame.ack);
    Put(out, static_cast<u16>(frame.entries.size()));
    for (const GameFrameEntry& entry : frame.entries) {
        out.push_back(static_cast<u8>(entry.packet_class));
        Put(out, entry.seq);
        Put(out, static_cast<u32>(entry.packet.size()));
        out.insert(out.end(), entry.packet.begin(), entry.packet.end());
    }
    return out;
}

inline std::optional<GameFrame> DecodeGameFrame(std::span<const u8> bytes) {
    using namespace GameLinkDetail;
    size_t at = 0;
    u8 version = 0;
    u8 sender = 0;
    if (!Get(bytes, at, version) || version != GameFrameVersion || !Get(bytes, at, sender) ||
        bytes.size() - at < sender) {
        return std::nullopt;
    }
    GameFrame frame;
    frame.sender.assign(reinterpret_cast<const char*>(bytes.data() + at), sender);
    at += sender;
    u16 count = 0;
    if (!Get(bytes, at, frame.instance) || !Get(bytes, at, frame.peer_instance) ||
        !Get(bytes, at, frame.ack) || !Get(bytes, at, count)) {
        return std::nullopt;
    }
    for (u16 index = 0; index < count; ++index) {
        u8 packet_class = 0;
        GameFrameEntry entry;
        u32 size = 0;
        if (!Get(bytes, at, packet_class) || packet_class > 1 || !Get(bytes, at, entry.seq) ||
            !Get(bytes, at, size) || bytes.size() - at < size) {
            return std::nullopt;
        }
        entry.packet_class = static_cast<PacketClass>(packet_class);
        entry.packet.assign(bytes.begin() + at, bytes.begin() + at + size);
        at += size;
        frame.entries.push_back(std::move(entry));
    }
    if (at != bytes.size()) {
        return std::nullopt;
    }
    return frame;
}

// One member as this game sees it: what goes to it and what came from it.
class GameLinkPeer {
public:
    struct Settings {
        s64 resend_ms = 150;
        s64 keepalive_ms = 1000;   // an empty frame when nothing else went for this long
        size_t frame_bytes = 1200; // entries share a frame up to this; a larger packet goes alone
        size_t held_out_of_order = 4096;
        // Lisara's playtest 2026-10-16 (a resend storm on a dropped link): a packet's resends back
        // off (doubling from resend_ms to max_resend_ms), at most max_resends go per flush
        // (oldest first), and none while the member has been silent for quiet_ms (heard again,
        // they go at once).
        s64 max_resend_ms = 2000;
        size_t max_resends = 64;
        s64 quiet_ms = 10'000;
        // What a member may make this side hold (shadNet's streams set them; none by default): the
        // bytes of the out-of-order packets held (past it they are dropped, and come again as
        // resends) and the unreliable channels remembered (a packet on another one is dropped).
        size_t held_bytes = SIZE_MAX;
        size_t unreliable_channels = SIZE_MAX;
    };

    GameLinkPeer(std::string self, u32 instance) : GameLinkPeer(std::move(self), instance, {}) {}
    GameLinkPeer(std::string self, u32 instance, Settings settings)
        : self{std::move(self)}, instance{instance}, settings{settings} {}

    // A packet of this game for the member, sent at the next Flush.
    void Send(PacketClass packet_class, std::span<const u8> packet, s64 now_ms) {
        Outgoing out{packet_class, 0, {packet.begin(), packet.end()}, now_ms};
        if (packet_class == PacketClass::Reliable) {
            out.seq = next_reliable++;
            reliable_bytes += packet.size();
        } else {
            out.seq = next_unreliable++;
        }
        fresh.push_back(std::move(out));
    }

    // The frames to send now: new packets, reliable ones due again, and an ack when one is owed.
    std::vector<GameFrame> Flush(s64 now_ms) {
        std::vector<GameFrameEntry> entries;
        for (Outgoing& out : fresh) {
            entries.push_back({out.packet_class, out.seq, out.packet});
            if (out.packet_class == PacketClass::Reliable) {
                out.sent_ms = now_ms;
                out.wait_ms = settings.resend_ms;
                unacked.emplace(out.seq, std::move(out));
            }
        }
        fresh.clear();
        const bool quiet = last_heard_ms && now_ms - *last_heard_ms >= settings.quiet_ms;
        if (heard_since_quiet) {
            // Heard again after a silence: what is owed goes now.
            heard_since_quiet = false;
            for (auto& [seq, out] : unacked) {
                out.wait_ms = settings.resend_ms;
                out.sent_ms = (std::min)(out.sent_ms, now_ms - settings.resend_ms);
            }
        }
        was_quiet = quiet;
        size_t resent = 0;
        for (auto& [seq, out] : unacked) {
            if (quiet || resent >= settings.max_resends) {
                break;
            }
            if (now_ms - out.sent_ms >= out.wait_ms &&
                std::ranges::none_of(entries, [&](const GameFrameEntry& entry) {
                    return entry.packet_class == PacketClass::Reliable && entry.seq == seq;
                })) {
                out.sent_ms = now_ms;
                out.wait_ms = (std::min)(out.wait_ms * 2, settings.max_resend_ms);
                ++resends;
                ++resent;
                entries.push_back({out.packet_class, out.seq, out.packet});
            }
        }
        std::vector<GameFrame> frames;
        size_t bytes = 0;
        for (GameFrameEntry& entry : entries) {
            const size_t size = GameLinkDetail::EntryHeaderBytes + entry.packet.size();
            if (frames.empty() || bytes + size > settings.frame_bytes) {
                frames.push_back(Header());
                bytes = GameLinkDetail::FrameHeaderBytes;
            }
            frames.back().entries.push_back(std::move(entry));
            bytes += size;
        }
        if (frames.empty() && (ack_owed || now_ms - last_frame_ms >= settings.keepalive_ms)) {
            frames.push_back(Header());
        }
        ack_owed = false;
        if (!frames.empty()) {
            last_frame_ms = now_ms;
        }
        return frames;
    }

    // A frame from the member: the packets for the game, in order.
    std::vector<std::vector<u8>> Receive(const GameFrame& frame, s64 now_ms) {
        if (was_quiet) {
            heard_since_quiet = true;
        }
        last_heard_ms = now_ms;
        if (!peer_instance || *peer_instance != frame.instance) {
            if (peer_instance) {
                RestartStreams();
            }
            peer_instance = frame.instance;
        }
        if (frame.peer_instance != 0 && frame.peer_instance != instance) {
            // Meant for this member's earlier game (this one restarted): nothing of it is taken,
            // and a frame goes back at once so the sender starts its streams again.
            ack_owed = true;
            return {};
        }
        if (frame.peer_instance == instance) {
            const auto acked = unacked.lower_bound(frame.ack);
            for (auto out = unacked.begin(); out != acked; ++out) {
                reliable_bytes -= out->second.packet.size();
            }
            unacked.erase(unacked.begin(), acked);
        }
        std::vector<std::vector<u8>> delivered;
        for (const GameFrameEntry& entry : frame.entries) {
            if (entry.packet_class == PacketClass::Unreliable) {
                TakeUnreliable(entry, delivered);
            } else {
                TakeReliable(entry, delivered);
            }
        }
        return delivered;
    }

    size_t Unacked() const {
        return unacked.size() + std::ranges::count_if(fresh, [](const Outgoing& out) {
                   return out.packet_class == PacketClass::Reliable;
               });
    }
    // The bytes of the reliable packets not yet acked (new ones included).
    size_t UnackedBytes() const {
        return reliable_bytes;
    }
    // The bytes of the out-of-order packets held for the gap before them.
    size_t HeldBytes() const {
        return held_bytes;
    }
    u64 Resends() const {
        return resends;
    }
    // How often the member's game restarted (each time, what either side had not taken went).
    u64 Restarts() const {
        return restarts;
    }
    u64 DroppedOld() const {
        return dropped_old;
    }
    std::optional<s64> SilentFor(s64 now_ms) const {
        if (!last_heard_ms) {
            return std::nullopt;
        }
        return now_ms - *last_heard_ms;
    }

private:
    struct Outgoing {
        PacketClass packet_class;
        u32 seq;
        std::vector<u8> packet;
        s64 sent_ms;
        s64 wait_ms = 0; // until its next resend (backs off)
    };

    static u32 ChannelOf(const std::vector<u8>& packet) {
        u32 channel = 0;
        for (size_t index = 0; index < 4 && index < packet.size(); ++index) {
            channel |= u32{packet[index]} << (8 * index);
        }
        return channel;
    }

    GameFrame Header() const {
        return GameFrame{.sender = self,
                         .instance = instance,
                         .peer_instance = peer_instance.value_or(0),
                         .ack = expected};
    }

    // The member's game restarted: what it had not taken is dropped, both ways start again.
    void RestartStreams() {
        ++restarts;
        expected = 0;
        held.clear();
        held_bytes = 0;
        newest_unreliable.clear();
        next_reliable = 0;
        next_unreliable = 0;
        unacked.clear();
        reliable_bytes = 0;
        std::erase_if(
            fresh, [](const Outgoing& out) { return out.packet_class == PacketClass::Reliable; });
    }

    void TakeUnreliable(const GameFrameEntry& entry, std::vector<std::vector<u8>>& delivered) {
        const u32 channel = ChannelOf(entry.packet);
        const auto newest = newest_unreliable.find(channel);
        if ((newest != newest_unreliable.end() && entry.seq <= newest->second) ||
            (newest == newest_unreliable.end() &&
             newest_unreliable.size() >= settings.unreliable_channels)) {
            ++dropped_old;
            return;
        }
        newest_unreliable[channel] = entry.seq;
        delivered.push_back(entry.packet);
    }

    void TakeReliable(const GameFrameEntry& entry, std::vector<std::vector<u8>>& delivered) {
        ack_owed = true;
        if (entry.seq < expected) {
            return; // a resend of one already taken
        }
        if (entry.seq > expected) {
            if (held.size() < settings.held_out_of_order &&
                held_bytes + entry.packet.size() <= settings.held_bytes &&
                held.try_emplace(entry.seq, entry.packet).second) {
                held_bytes += entry.packet.size();
            }
            return;
        }
        delivered.push_back(entry.packet);
        ++expected;
        for (auto next = held.find(expected); next != held.end(); next = held.find(expected)) {
            held_bytes -= next->second.size();
            delivered.push_back(std::move(next->second));
            held.erase(next);
            ++expected;
        }
    }

    std::string self;
    u32 instance;
    Settings settings;
    std::optional<u32> peer_instance;
    std::optional<s64> last_heard_ms;

    u32 next_reliable = 0;
    u32 next_unreliable = 0;
    std::deque<Outgoing> fresh;
    std::map<u32, Outgoing> unacked;
    size_t reliable_bytes = 0; // the reliable packets' in fresh and unacked
    u64 resends = 0;
    bool was_quiet = false;         // the last flush found the member silent
    bool heard_since_quiet = false; // and it was heard since

    u32 expected = 0;
    std::map<u32, std::vector<u8>> held;
    size_t held_bytes = 0;
    std::map<u32, u32> newest_unreliable;
    bool ack_owed = false;
    s64 last_frame_ms = 0;
    u64 dropped_old = 0;
    u64 restarts = 0;
};

} // namespace BloodborneCoop
