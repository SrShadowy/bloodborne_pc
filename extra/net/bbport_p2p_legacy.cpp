// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "extra/net/bbport_p2p_legacy.h"

namespace Extra::Net {

namespace {

constexpr size_t HeaderSize = 4;
constexpr u16 SignalingVport = 0xFFFF;

u16 ReadVport(std::span<const u8> packet, size_t offset) {
    return static_cast<u16>((packet[offset] << 8) | packet[offset + 1]);
}

void AppendVport(std::vector<u8>& packet, u16 vport) {
    packet.push_back(static_cast<u8>(vport >> 8));
    packet.push_back(static_cast<u8>(vport));
}

} // namespace

Codec::Decoded Legacy19Codec::Decode(std::span<const u8> packet, const Endpoint&) {
    Decoded out;
    if (packet.size() < HeaderSize) {
        return out;
    }
    out.src_vport = ReadVport(packet, 0);
    out.dst_vport = ReadVport(packet, 2);
    out.kind = out.dst_vport == SignalingVport ? Kind::Signaling : Kind::Datagram;
    out.payload.assign(packet.begin() + HeaderSize, packet.end());
    return out;
}

std::vector<u8> Legacy19Codec::EncodeStream(std::span<const u8>, const Endpoint&, Protection) {
    // 0.19 has no P2P TCP. The transport's stream path stays unused on this wire.
    return {};
}

std::vector<u8> Legacy19Codec::EncodeDatagram(u16 src_vport, u16 dst_vport,
                                              std::span<const u8> payload, const Endpoint&,
                                              Protection) {
    std::vector<u8> packet;
    packet.reserve(HeaderSize + payload.size());
    AppendVport(packet, src_vport);
    AppendVport(packet, dst_vport);
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::vector<u8> Legacy19Codec::EncodeSignaling(std::span<const u8> data, const Endpoint&) {
    std::vector<u8> packet;
    packet.reserve(HeaderSize + data.size());
    AppendVport(packet, SignalingVport);
    AppendVport(packet, SignalingVport);
    packet.insert(packet.end(), data.begin(), data.end());
    return packet;
}

PseudoHeader Legacy19Codec::StreamPseudoHeader(const Endpoint&) {
    // No P2P TCP in 0.19: the all-zero pseudo-header is never used for a real segment.
    return {};
}

} // namespace Extra::Net
