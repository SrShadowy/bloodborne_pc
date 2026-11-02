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
#include <memory>
#include <span>
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

/// The 0.19 framing. The keyring is not used: 0.19 has no communication ID or payload crypto.
class Legacy19Codec final : public Codec {
public:
    Legacy19Codec() = default;

    Decoded Decode(std::span<const u8> packet, const Endpoint& from) override;
    std::vector<u8> EncodeStream(std::span<const u8> tcp_segment, const Endpoint& to,
                                 Protection protection) override;
    std::vector<u8> EncodeDatagram(u16 src_vport, u16 dst_vport, std::span<const u8> payload,
                                   const Endpoint& to, Protection protection) override;
    std::vector<u8> EncodeSignaling(std::span<const u8> data, const Endpoint& to) override;
    PseudoHeader StreamPseudoHeader(const Endpoint& peer) override;
};

/// BB_NET_GEN=19, exactly, selects Gen 19. Unset or any other value keeps 0.20 byte for byte.
inline bool LegacyFramingRequested() {
    const char* gen = std::getenv("BB_NET_GEN");
    return gen != nullptr && std::string_view{gen} == "19";
}

/// The single construction site in net_p2p.cpp uses this, so the default session still builds a
/// FramingCodec and never touches the legacy codec.
// shortcut: one framing per session, not one per peer; update if mixed Gen 19/20 peers must
// coexist (the transport takes its codec once, at start).
inline std::unique_ptr<Codec> MakeP2PCodec(std::shared_ptr<Keyring> keyring,
                                          FramingCodec::BroadcastObserver observer = {}) {
    if (LegacyFramingRequested()) {
        return std::make_unique<Legacy19Codec>();
    }
    return std::make_unique<FramingCodec>(std::move(keyring), std::move(observer));
}

} // namespace Extra::Net
