// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The reliable stream between a game and shadNet for our messages (coop.proto, phase 3): the
// server link's Coop datagrams carry game link frames (coop/game_link.h) of the reliable class
// only, so messages arrive in order and exactly once while both games run. A message larger than a
// chunk goes in several, each a byte (LastChunk on the last) and its share of the message.
//
// A side whose peer restarted (the game, or shadNet) loses what was in flight: Restarted says so
// once, and the side says hello again.
//
// shadNet's side has limits (Limits): a message past message_bytes, or a chunk larger than any
// Send makes, breaks the stream (Broken: the server drops it, and the game's next datagram meets
// a new one, so the game starts over and says hello again).
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "coop/coop_types.h"
#include "coop/game_link.h"

namespace BloodborneCoop {

class CoopStream {
public:
    static constexpr size_t ChunkBytes = 1000;
    static constexpr u8 LastChunk = 1;

    // What the other side may make this one hold; the defaults hold no limit.
    struct Limits {
        size_t message_bytes = SIZE_MAX; // a message, its chunks together
        size_t chunk_bytes = SIZE_MAX;   // one chunk with its byte (ChunkBytes + 1 from a Send)
        size_t held_bytes = SIZE_MAX;    // chunks held out of order (GameLinkPeer::Settings)
        size_t unreliable_channels = SIZE_MAX;
    };

    CoopStream(std::string self, u32 instance) : CoopStream(std::move(self), instance, Limits{}) {}
    CoopStream(std::string self, u32 instance, Limits limits)
        : peer{std::move(self), instance, Settings(limits)}, limits{limits} {}

    // A message for the other side, sent at the next Flush.
    void Send(std::span<const u8> message, s64 now_ms) {
        size_t at = 0;
        do {
            const size_t size = (std::min)(ChunkBytes, message.size() - at);
            std::vector<u8> chunk;
            chunk.reserve(size + 1);
            chunk.push_back(at + size == message.size() ? LastChunk : 0);
            chunk.insert(chunk.end(), message.begin() + static_cast<std::ptrdiff_t>(at),
                         message.begin() + static_cast<std::ptrdiff_t>(at + size));
            peer.Send(PacketClass::Reliable, chunk, now_ms);
            at += size;
        } while (at < message.size());
    }

    // The frames to send now (each one datagram's body): chunks, resends and acks.
    std::vector<std::vector<u8>> Flush(s64 now_ms) {
        std::vector<std::vector<u8>> bodies;
        for (const GameFrame& frame : peer.Flush(now_ms)) {
            bodies.push_back(EncodeGameFrame(frame));
        }
        return bodies;
    }

    // A datagram's body from the other side: the messages it completed, in order.
    std::vector<std::vector<u8>> Receive(std::span<const u8> body, s64 now_ms) {
        std::vector<std::vector<u8>> messages;
        if (broken) {
            return messages;
        }
        const auto frame = DecodeGameFrame(body);
        if (!frame) {
            return messages;
        }
        if (std::ranges::any_of(frame->entries, [&](const GameFrameEntry& entry) {
                return entry.packet.size() > limits.chunk_bytes;
            })) {
            Break();
            return messages;
        }
        const u64 restarts = peer.Restarts();
        std::vector<std::vector<u8>> chunks = peer.Receive(*frame, now_ms);
        if (peer.Restarts() != restarts) {
            partial.clear();
            restarted = true;
        }
        for (const std::vector<u8>& chunk : chunks) {
            if (chunk.empty()) {
                continue;
            }
            if (chunk.size() - 1 > limits.message_bytes - partial.size()) {
                Break();
                return {};
            }
            partial.insert(partial.end(), chunk.begin() + 1, chunk.end());
            if ((chunk[0] & LastChunk) != 0) {
                messages.push_back(std::exchange(partial, {}));
            }
        }
        return messages;
    }

    // Whether the other side restarted since the last call (what it had not taken is gone).
    bool TakeRestarted() {
        return std::exchange(restarted, false);
    }
    // Whether the other side went past the limits: nothing more is taken from it.
    bool Broken() const {
        return broken;
    }
    // Messages not yet acked by the other side, and their bytes (chunked).
    size_t Unacked() const {
        return peer.Unacked();
    }
    size_t UnackedBytes() const {
        return peer.UnackedBytes();
    }
    // What this side holds of the other's: chunks out of order, a message in part.
    size_t HeldBytes() const {
        return peer.HeldBytes() + partial.size();
    }
    u64 Resends() const {
        return peer.Resends();
    }
    std::optional<s64> SilentFor(s64 now_ms) const {
        return peer.SilentFor(now_ms);
    }

private:
    static GameLinkPeer::Settings Settings(const Limits& limits) {
        GameLinkPeer::Settings settings;
        settings.resend_ms = 200;
        settings.keepalive_ms = 1000;
        settings.held_bytes = limits.held_bytes;
        settings.unreliable_channels = limits.unreliable_channels;
        return settings;
    }

    void Break() {
        broken = true;
        partial = {};
    }

    GameLinkPeer peer;
    Limits limits;
    std::vector<u8> partial;
    bool restarted = false;
    bool broken = false;
};

} // namespace BloodborneCoop
