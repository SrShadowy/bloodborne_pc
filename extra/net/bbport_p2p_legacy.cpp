#include <cstdio>
#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#else
#include <ws2tcpip.h>
#endif

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

std::string EndpointString(const Endpoint& ep) {
    char addr[64] = "?";
    u16 port = 0;
    const auto* sa = ep.Sockaddr();
    if (sa->sa_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(sa);
        inet_ntop(AF_INET, &in->sin_addr, addr, sizeof(addr));
        port = ntohs(in->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(sa);
        inet_ntop(AF_INET6, &in6->sin6_addr, addr, sizeof(addr));
        port = ntohs(in6->sin6_port);
    }
    return std::string(addr) + ":" + std::to_string(port);
}

bool IsGen20Frame(std::span<const u8> packet) {
    if (packet.size() < 2 || packet[0] != Core::Net::P2P::Wire::Marker) {
        return false;
    }
    const u8 flags = packet[1];
    if ((flags & Core::Net::P2P::Wire::FlagValid) == 0 || (flags & 0x10) != 0) {
        return false;
    }
    const u8 kind = flags & 0x0F;
    if (kind < Core::Net::P2P::Wire::KindDatagram ||
        kind > Core::Net::P2P::Wire::KindStream + 3) {
        return false;
    }
    const bool stream = kind >= Core::Net::P2P::Wire::KindStream;
    const size_t min_size = stream ? 2 : 6;
    return packet.size() >= min_size;
}

Codec::Decoded DecodePlainGen20(std::span<const u8> packet) {
    Codec::Decoded out;
    if (packet.size() < 2) {
        return out;
    }
    const u8 flags = packet[1];
    const u8 kind = flags & 0x0F;
    const bool stream = kind >= Core::Net::P2P::Wire::KindStream;
    size_t offset = 2;
    u16 src_vport = 0;
    u16 dst_vport = 0;
    if (flags & Core::Net::P2P::Wire::FlagBroadcast) {
        if (stream || packet.size() < 4) {
            return out;
        }
        offset = 4;
        dst_vport = static_cast<u16>((packet[2] << 8) | packet[3]);
        src_vport = dst_vport;
    } else if (!stream) {
        offset = 6;
        if (packet.size() < offset) {
            return out;
        }
        src_vport = static_cast<u16>((packet[2] << 8) | packet[3]);
        dst_vport = static_cast<u16>((packet[4] << 8) | packet[5]);
    }
    if (flags & Core::Net::P2P::Wire::FlagDigest) {
        if (packet.size() < offset + 4) {
            return out;
        }
        offset += 4;
    }
    const bool signaling = !stream && dst_vport == SignalingVport;
    out.kind = stream ? Codec::Kind::Stream
                      : signaling ? Codec::Kind::Signaling : Codec::Kind::Datagram;
    out.src_vport = src_vport;
    out.dst_vport = dst_vport;
    out.payload.assign(packet.begin() + offset, packet.end());
    return out;
}

} // namespace

Legacy19Codec::Legacy19Codec(std::unique_ptr<Codec> gen20_delegate, u8 default_gen)
    : gen20_delegate_(std::move(gen20_delegate)), default_gen_(default_gen) {}

u8 Legacy19Codec::PeerGeneration(const Endpoint& peer) const {
    std::scoped_lock lock{mutex_};
    const auto it = peer_gen_.find(peer);
    if (it != peer_gen_.end()) {
        return it->second;
    }
    return default_gen_;
}

Codec::Decoded Legacy19Codec::Decode(std::span<const u8> packet, const Endpoint& from) {
    const bool is_gen20 = IsGen20Frame(packet);
    const bool is_gen19 = !is_gen20 && packet.size() >= HeaderSize;
    if (is_gen20 || is_gen19) {
        const u8 detected = is_gen20 ? 20 : 19;
        bool log_change = false;
        {
            std::scoped_lock lock{mutex_};
            auto [it, inserted] = peer_gen_.emplace(from, detected);
            if (inserted || it->second != detected) {
                it->second = detected;
                log_change = true;
            }
        }
        if (log_change) {
            std::fprintf(stderr,
                         "[Net P2P] Auto-negotiated framing for peer %s: Gen %u (sample packet: %zu bytes)\n",
                         EndpointString(from).c_str(), detected, packet.size());
        }
    }

    if (is_gen20) {
        if (gen20_delegate_) {
            return gen20_delegate_->Decode(packet, from);
        }
        return DecodePlainGen20(packet);
    }

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

std::vector<u8> Legacy19Codec::EncodeStream(std::span<const u8> tcp_segment, const Endpoint& to,
                                            Protection protection) {
    if (PeerGeneration(to) == 20 && gen20_delegate_) {
        return gen20_delegate_->EncodeStream(tcp_segment, to, protection);
    }
    return {};
}

std::vector<u8> Legacy19Codec::EncodeDatagram(u16 src_vport, u16 dst_vport,
                                              std::span<const u8> payload, const Endpoint& to,
                                              Protection protection) {
    if (PeerGeneration(to) == 20) {
        if (gen20_delegate_) {
            return gen20_delegate_->EncodeDatagram(src_vport, dst_vport, payload, to, protection);
        }
        std::vector<u8> packet;
        packet.reserve(6 + payload.size());
        packet.push_back(Core::Net::P2P::Wire::Marker);
        packet.push_back(static_cast<u8>(Core::Net::P2P::Wire::FlagValid |
                                         Core::Net::P2P::Wire::KindDatagram));
        AppendVport(packet, src_vport);
        AppendVport(packet, dst_vport);
        packet.insert(packet.end(), payload.begin(), payload.end());
        return packet;
    }

    std::vector<u8> packet;
    packet.reserve(HeaderSize + payload.size());
    AppendVport(packet, src_vport);
    AppendVport(packet, dst_vport);
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

std::vector<u8> Legacy19Codec::EncodeSignaling(std::span<const u8> data, const Endpoint& to) {
    if (PeerGeneration(to) == 20) {
        if (gen20_delegate_) {
            return gen20_delegate_->EncodeSignaling(data, to);
        }
        std::vector<u8> packet;
        packet.reserve(6 + data.size());
        packet.push_back(Core::Net::P2P::Wire::Marker);
        packet.push_back(static_cast<u8>(Core::Net::P2P::Wire::FlagValid |
                                         Core::Net::P2P::Wire::KindDatagram));
        AppendVport(packet, SignalingVport);
        AppendVport(packet, SignalingVport);
        packet.insert(packet.end(), data.begin(), data.end());
        return packet;
    }

    std::vector<u8> packet;
    packet.reserve(HeaderSize + data.size());
    AppendVport(packet, SignalingVport);
    AppendVport(packet, SignalingVport);
    packet.insert(packet.end(), data.begin(), data.end());
    return packet;
}

PseudoHeader Legacy19Codec::StreamPseudoHeader(const Endpoint& peer) {
    if (gen20_delegate_) {
        return gen20_delegate_->StreamPseudoHeader(peer);
    }
    return {};
}

} // namespace Extra::Net
