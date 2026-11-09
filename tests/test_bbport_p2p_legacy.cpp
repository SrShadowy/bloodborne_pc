// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The opt-in Gen 19 (shadp2p 0.6.x) P2P codec: golden vectors for the 4-byte vport header,
// signaling, empty/max payloads, big-endian vports and the BB_NET_GEN selection. No crypto and
// no vendored codec implementation is linked.
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#else
#include <ws2tcpip.h>
#endif

#include "extra/net/bbport_p2p_legacy.h"

namespace {

using Extra::Net::Legacy19Codec;
using Extra::Net::LegacyFramingRequested;
namespace P2P = Core::Net::P2P;

P2P::Endpoint MakeIpv4Endpoint(const char* dotted, u16 port) {
    P2P::Endpoint ep{};
    auto* in = reinterpret_cast<sockaddr_in*>(&ep.addr);
    in->sin_family = AF_INET;
    in->sin_port = htons(port);
    inet_pton(AF_INET, dotted, &in->sin_addr);
    return ep;
}

void SetNetGen(const char* value) {
#ifdef _WIN32
    _putenv_s("BB_NET_GEN", value != nullptr ? value : "");
#else
    if (value != nullptr) {
        setenv("BB_NET_GEN", value, 1);
    } else {
        unsetenv("BB_NET_GEN");
    }
#endif
}

void DatagramRoundTrip() {
    Legacy19Codec codec;
    const std::vector<u8> payload{0xDE, 0xAD, 0xBE, 0xEF};
    const auto wire = codec.EncodeDatagram(0xF001, 0x1234, payload, P2P::Endpoint{}, {});
    const std::vector<u8> golden{0xF0, 0x01, 0x12, 0x34, 0xDE, 0xAD, 0xBE, 0xEF};
    assert(wire == golden);

    const auto decoded = codec.Decode(wire, P2P::Endpoint{});
    assert(decoded.kind == P2P::Codec::Kind::Datagram);
    assert(decoded.src_vport == 0xF001);
    assert(decoded.dst_vport == 0x1234);
    assert(decoded.payload == payload);
}

void SignalingRoundTrip() {
    Legacy19Codec codec;
    const std::vector<u8> data{0x53, 0x48, 0x41, 0x44}; // "SHAD"
    const auto wire = codec.EncodeSignaling(data, P2P::Endpoint{});
    assert((wire == std::vector<u8>{0xFF, 0xFF, 0xFF, 0xFF, 0x53, 0x48, 0x41, 0x44}));

    const auto decoded = codec.Decode(wire, P2P::Endpoint{});
    assert(decoded.kind == P2P::Codec::Kind::Signaling);
    assert(decoded.src_vport == 0xFFFF);
    assert(decoded.dst_vport == 0xFFFF);
    assert(decoded.payload == data);
}

void PayloadEdges() {
    Legacy19Codec codec;
    const auto empty = codec.EncodeDatagram(0x0102, 0x0304, std::span<const u8>{},
                                             P2P::Endpoint{}, {});
    assert((empty == std::vector<u8>{0x01, 0x02, 0x03, 0x04}));
    const auto empty_decoded = codec.Decode(empty, P2P::Endpoint{});
    assert(empty_decoded.kind == P2P::Codec::Kind::Datagram);
    assert(empty_decoded.src_vport == 0x0102);
    assert(empty_decoded.dst_vport == 0x0304);
    assert(empty_decoded.payload.empty());

    const std::vector<u8> max_payload(65503, 0x5A); // 65507-byte UDP payload minus the header
    const auto max_wire =
        codec.EncodeDatagram(0x0001, 0xFFFE, max_payload, P2P::Endpoint{}, {});
    assert(max_wire.size() == 65507u);
    const auto max_decoded = codec.Decode(max_wire, P2P::Endpoint{});
    assert(max_decoded.kind == P2P::Codec::Kind::Datagram);
    assert(max_decoded.payload == max_payload);
}

void ShortFramesAreDropped() {
    Legacy19Codec codec;
    assert(codec.Decode(std::span<const u8>{}, P2P::Endpoint{}).kind ==
           P2P::Codec::Kind::Invalid);
    for (size_t size = 1; size < 4u; ++size) {
        const std::vector<u8> frame(size, 0xAA);
        assert(codec.Decode(frame, P2P::Endpoint{}).kind == P2P::Codec::Kind::Invalid);
    }
}

void VportsAreBigEndian() {
    Legacy19Codec codec;
    const auto wire =
        codec.EncodeDatagram(0xAB01, 0xCDEF, std::span<const u8>{}, P2P::Endpoint{}, {});
    assert((wire == std::vector<u8>{0xAB, 0x01, 0xCD, 0xEF}));

    const std::vector<u8> inbound{0x12, 0x34, 0x56, 0x78};
    const auto decoded = codec.Decode(inbound, P2P::Endpoint{});
    assert(decoded.src_vport == 0x1234);
    assert(decoded.dst_vport == 0x5678);
}

void ProtectionIsIgnoredAndStreamsAreUnsupported() {
    Legacy19Codec codec;
    const std::vector<u8> payload{0x10, 0x20};
    const auto plain = codec.EncodeDatagram(1, 2, payload, P2P::Endpoint{}, {});
    const auto protected_wire =
        codec.EncodeDatagram(1, 2, payload, P2P::Endpoint{}, P2P::Protection{true, true});
    assert(plain == protected_wire); // 0.19 has no digest, crypto or signature

    assert(codec.EncodeStream(std::span<const u8>{}, P2P::Endpoint{}, {}).empty());
    const auto pseudo = codec.StreamPseudoHeader(P2P::Endpoint{});
    assert(pseudo.family == AF_INET);
    assert(pseudo.local == std::array<u8, 16>{});
    assert(pseudo.remote == std::array<u8, 16>{});
}

void FactorySelection() {
    SetNetGen(nullptr);
    assert(LegacyFramingRequested());
    SetNetGen("");
    assert(LegacyFramingRequested());
    SetNetGen("19");
    assert(LegacyFramingRequested());
    SetNetGen("20");
    assert(!LegacyFramingRequested());
    SetNetGen("auto");
    assert(LegacyFramingRequested());
    SetNetGen(nullptr);
}

void AutoNegotiationGen19AndGen20PerPeer() {
    Legacy19Codec codec;
    const auto peer_a = MakeIpv4Endpoint("10.0.0.1", 3658);
    const auto peer_b = MakeIpv4Endpoint("10.0.0.2", 3658);

    assert(codec.PeerGeneration(peer_a) == 19);
    assert(codec.PeerGeneration(peer_b) == 19);

    // Peer A sends Gen 19 datagram
    const std::vector<u8> gen19_wire{0x12, 0x34, 0x56, 0x78, 0xAA, 0xBB};
    const auto dec_a = codec.Decode(gen19_wire, peer_a);
    assert(dec_a.kind == P2P::Codec::Kind::Datagram);
    assert(dec_a.src_vport == 0x1234);
    assert(dec_a.dst_vport == 0x5678);
    assert(codec.PeerGeneration(peer_a) == 19);

    // Peer A sends Gen 19 signaling (0xFFFF 0xFFFF must NOT be mistaken for Gen 20)
    const std::vector<u8> gen19_sig{0xFF, 0xFF, 0xFF, 0xFF, 'S', 'H', 'A', 'D'};
    const auto dec_sig = codec.Decode(gen19_sig, peer_a);
    assert(dec_sig.kind == P2P::Codec::Kind::Signaling);
    assert(dec_sig.src_vport == 0xFFFF);
    assert(dec_sig.dst_vport == 0xFFFF);
    assert(codec.PeerGeneration(peer_a) == 19);

    // Peer B sends Gen 20 datagram
    const std::vector<u8> gen20_wire{0xFF, 0x83, 0x22, 0x22, 0x33, 0x33, 0xDE, 0xAD};
    const auto dec_b = codec.Decode(gen20_wire, peer_b);
    assert(dec_b.kind == P2P::Codec::Kind::Datagram);
    assert(dec_b.src_vport == 0x2222);
    assert(dec_b.dst_vport == 0x3333);
    assert((dec_b.payload == std::vector<u8>{0xDE, 0xAD}));
    assert(codec.PeerGeneration(peer_b) == 20);
    assert(codec.PeerGeneration(peer_a) == 19);

    // Encode to peer_a (Gen 19 format)
    const auto out_a = codec.EncodeDatagram(0x1000, 0x2000, std::vector<u8>{1, 2}, peer_a, {});
    assert((out_a == std::vector<u8>{0x10, 0x00, 0x20, 0x00, 1, 2}));
    const auto sig_a = codec.EncodeSignaling(std::vector<u8>{'A'}, peer_a);
    assert((sig_a == std::vector<u8>{0xFF, 0xFF, 0xFF, 0xFF, 'A'}));

    // Encode to peer_b (Gen 20 format)
    const auto out_b = codec.EncodeDatagram(0x1000, 0x2000, std::vector<u8>{1, 2}, peer_b, {});
    assert((out_b == std::vector<u8>{0xFF, 0x83, 0x10, 0x00, 0x20, 0x00, 1, 2}));
    const auto sig_b = codec.EncodeSignaling(std::vector<u8>{'B'}, peer_b);
    assert((sig_b == std::vector<u8>{0xFF, 0x83, 0xFF, 0xFF, 0xFF, 0xFF, 'B'}));
}

} // namespace

int main() {
    DatagramRoundTrip();
    SignalingRoundTrip();
    PayloadEdges();
    ShortFramesAreDropped();
    VportsAreBigEndian();
    ProtectionIsIgnoredAndStreamsAreUnsupported();
    FactorySelection();
    AutoNegotiationGen19AndGen20PerPeer();
    std::puts("PASS: Gen 19 P2P codec: datagram/signaling round trips, header bytes, payload "
              "edges, big-endian vports, BB_NET_GEN selection, per-peer Gen 19/20 auto-negotiation");
}
