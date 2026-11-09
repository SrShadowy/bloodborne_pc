// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Gen 19 (shadp2p 0.6.x) P2P framing inside libbbnet20. The 0.20 codec is the default and the
// launcher's "Gen 19" checkbox is off by default; BB_NET_GEN=19 selects this codec instead.
//
// 0.19 wire: [src vport 2][dst vport 2][payload], both vports big endian. Signaling is a plain
// datagram with both vports 0xFFFF. There is no P2P TCP, digest or payload crypto in 0.19.

#pragma once

#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <map>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include "core/net/p2p_codec.h"

namespace Extra::Net {

using Core::Net::P2P::Codec;
using Core::Net::P2P::Endpoint;
using Core::Net::P2P::FramingCodec;
using Core::Net::P2P::Keyring;
using Core::Net::P2P::Protection;
using Core::Net::P2P::PseudoHeader;

struct PeerEndpointLess {
    bool operator()(const Endpoint& a, const Endpoint& b) const {
        return std::memcmp(&a.addr, &b.addr, sizeof(a.addr)) < 0;
    }
};

/// The hybrid/auto-negotiating framing codec. Defaults to Gen 19 (or default_gen) for unknown
/// peers, but detects Gen 20 vs Gen 19 dynamically per peer and switches framing accordingly.
class Legacy19Codec final : public Codec {
public:
    explicit Legacy19Codec(std::unique_ptr<Codec> gen20_delegate = nullptr, u8 default_gen = 19);

    Decoded Decode(std::span<const u8> packet, const Endpoint& from) override;
    std::vector<u8> EncodeStream(std::span<const u8> tcp_segment, const Endpoint& to,
                                 Protection protection) override;
    std::vector<u8> EncodeDatagram(u16 src_vport, u16 dst_vport, std::span<const u8> payload,
                                   const Endpoint& to, Protection protection) override;
    std::vector<u8> EncodeSignaling(std::span<const u8> data, const Endpoint& to) override;
    PseudoHeader StreamPseudoHeader(const Endpoint& peer) override;

    u8 PeerGeneration(const Endpoint& peer) const;

private:
    std::unique_ptr<Codec> gen20_delegate_;
    u8 default_gen_ = 19;
    mutable std::mutex mutex_;
    std::map<Endpoint, u8, PeerEndpointLess> peer_gen_;
};

/// Gen 19 (shadp2p 0.6.x) framing is active by default (or with BB_NET_GEN=19) for direct
/// compatibility with shadPS4 emulator players. Only BB_NET_GEN=20 selects 0.20 framing.
inline bool LegacyFramingRequested() {
    const char* gen = std::getenv("BB_NET_GEN");
    if (gen == nullptr || *gen == '\0') {
        return true;
    }
    return std::string_view{gen} != "20";
}

/// The single construction site in net_p2p.cpp uses this: Gen 19 (Legacy19Codec with dynamic
/// peer auto-negotiation) is the default; BB_NET_GEN=20 selects pure FramingCodec.
inline std::unique_ptr<Codec> MakeP2PCodec(std::shared_ptr<Keyring> keyring,
                                          FramingCodec::BroadcastObserver observer = {}) {
    auto gen20 = std::make_unique<FramingCodec>(std::move(keyring), std::move(observer));
    if (LegacyFramingRequested()) {
        return std::make_unique<Legacy19Codec>(std::move(gen20), 19);
    }
    return gen20;
}

} // namespace Extra::Net
