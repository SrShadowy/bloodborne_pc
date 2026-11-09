// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The server link's handshake and encryption (link version 4, docs/HOSTED_SERVER.md), shared by
// shadNet (server_link.h) and the game (the emulator's net_link.h), so both build it the same
// everywhere. Its one outside piece is Monocypher (coop/monocypher: BSD-2-Clause or CC0, compiled
// once by coop/link_crypto.cpp) for X25519, ChaCha20-Poly1305 and BLAKE2b.
//
// - A hello without a valid cookie gets only a cookie back: HMAC(cookie key, the hello's source
//   address and port, a 30 s bucket), the bits a new account's work needs and the server's public
//   key, smaller than the hello. A spoofed source never sees it, so it makes no link, no account
//   and no key exchange, and the server keeps nothing for it.
// - The hello that echoes it is the first message of Noise NK (Noise_NK_25519_ChaChaPoly_BLAKE2b,
//   noiseprotocol.org): the game's ephemeral key, then the id, build, secret and work nonce
//   encrypted to the server's static key. Its header, version and cookie go in clear, as the
//   handshake's prologue (so they are authenticated too).
// - Its answer (a welcome, a refusal, a work ask) is the second message: the server's ephemeral
//   key, then the whole answer encrypted. Only the static key's holder can make one that opens. An
//   accepted welcome splits the handshake into the link's two keys, one each way.
// - Every datagram after it, both ways, has its body encrypted (ChaCha20-Poly1305, the 9-byte
//   header as associated data) and ends in its counter (8 bytes, the nonce, as WireGuard sends it)
//   and the tag (16). One that does not open, or whose counter was seen already or is older than
//   the window, is dropped without a word.
//
// What it stops: reading the secret, the Rpc stream and the game data on the way; anyone off the
// path (spoofed hellos, reflection, injected datagrams, replays); a server without the static key
// answering. The game knows the key from the launcher, or takes the first cookie's and keeps it
// once a welcome proves it (trust on first use: Client).
#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coop/coop_types.h"

#ifndef MONOCYPHER_CPP_NAMESPACE
#define MONOCYPHER_CPP_NAMESPACE LinkMonocypher
#endif
#include "coop/monocypher/monocypher.h"

namespace LinkAuth {

// A link datagram's header: ffff ffff, "SHLK", the kind (server_link.h, net_link.h).
constexpr size_t HeaderBytes = 9;
constexpr size_t KindAt = 8;
constexpr u8 KindHello = 1;
constexpr u8 KindWelcome = 2;
constexpr u8 KindWork = 10;
constexpr u8 KindRedirect = 11; // a region's (docs/SCALING.md): a hello's answer, or on the session
constexpr size_t KeyBytes = 32;
constexpr size_t CookieBytes = 16;
constexpr size_t CounterBytes = 8;
constexpr size_t TagBytes = 16;
constexpr size_t TrailerBytes = CounterBytes + TagBytes;
// A hello: the header, the version and the cookie in clear; then the game's ephemeral key and the
// rest encrypted, its tag last. A first hello (no cookie yet) is as long as one with an empty rest.
constexpr size_t HelloClearBytes = HeaderBytes + 1 + CookieBytes;
constexpr size_t HelloSealBytes = KeyBytes + TagBytes;
constexpr size_t FirstHelloBytes = HelloClearBytes + HelloSealBytes;
// An answer: the header in clear, the server's ephemeral key, the whole answer encrypted.
constexpr size_t AnswerSealBytes = KeyBytes + TagBytes;
constexpr s64 CookieBucketMs = 30'000; // a cookie holds for 30-60 s
using Cookie = std::array<u8, CookieBytes>;
using Key = std::array<u8, KeyBytes>;
using Digest = std::array<u8, 32>;
using Trailer = std::array<u8, TrailerBytes>;

inline std::span<const u8> Bytes(std::string_view text) {
    return {reinterpret_cast<const u8*>(text.data()), text.size()};
}

class Sha256 {
public:
    void Update(std::span<const u8> data) {
        total += data.size();
        size_t at = 0;
        if (used != 0) {
            const size_t take = std::min(data.size(), 64 - used);
            std::memcpy(buffer + used, data.data(), take);
            used += take;
            at = take;
            if (used < 64) {
                return;
            }
            Block(buffer);
            used = 0;
        }
        for (; data.size() - at >= 64; at += 64) {
            Block(data.data() + at);
        }
        std::memcpy(buffer, data.data() + at, data.size() - at);
        used = data.size() - at;
    }

    Digest Final() {
        const u64 bits = total * 8;
        static constexpr u8 pad[64] = {0x80};
        Update({pad, (used < 56 ? 56 : 120) - used});
        u8 length[8];
        for (int index = 0; index < 8; ++index) {
            length[index] = static_cast<u8>(bits >> (56 - 8 * index));
        }
        Update(length);
        Digest out;
        for (int index = 0; index < 8; ++index) {
            for (int byte = 0; byte < 4; ++byte) {
                out[index * 4 + byte] = static_cast<u8>(state[index] >> (24 - 8 * byte));
            }
        }
        return out;
    }

    static Digest Of(std::span<const u8> data) {
        Sha256 sha;
        sha.Update(data);
        return sha.Final();
    }

private:
    static u32 Rotr(u32 value, int bits) {
        return (value >> bits) | (value << (32 - bits));
    }

    void Block(const u8* block) {
        static constexpr u32 k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        u32 w[64];
        for (int index = 0; index < 16; ++index) {
            w[index] = u32{block[index * 4]} << 24 | u32{block[index * 4 + 1]} << 16 |
                       u32{block[index * 4 + 2]} << 8 | u32{block[index * 4 + 3]};
        }
        for (int index = 16; index < 64; ++index) {
            const u32 s0 = Rotr(w[index - 15], 7) ^ Rotr(w[index - 15], 18) ^ (w[index - 15] >> 3);
            const u32 s1 = Rotr(w[index - 2], 17) ^ Rotr(w[index - 2], 19) ^ (w[index - 2] >> 10);
            w[index] = w[index - 16] + s0 + w[index - 7] + s1;
        }
        u32 a = state[0], b = state[1], c = state[2], d = state[3];
        u32 e = state[4], f = state[5], g = state[6], h = state[7];
        for (int index = 0; index < 64; ++index) {
            const u32 t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) +
                           k[index] + w[index];
            const u32 t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    u32 state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    u8 buffer[64] = {};
    size_t used = 0;
    u64 total = 0;
};

// HMAC-SHA256 with its key's inner and outer blocks hashed once (a datagram costs its own blocks
// and two more).
class HmacKey {
public:
    HmacKey() : HmacKey(std::span<const u8>{}) {}
    explicit HmacKey(std::span<const u8> key) {
        u8 block[64] = {};
        if (key.size() > 64) {
            const Digest hashed = Sha256::Of(key);
            std::memcpy(block, hashed.data(), hashed.size());
        } else if (!key.empty()) {
            std::memcpy(block, key.data(), key.size());
        }
        u8 pad[64];
        for (int index = 0; index < 64; ++index) {
            pad[index] = block[index] ^ 0x36;
        }
        inner.Update(pad);
        for (int index = 0; index < 64; ++index) {
            pad[index] = block[index] ^ 0x5c;
        }
        outer.Update(pad);
    }

    Digest Mac(std::span<const u8> first, std::span<const u8> second = {}) const {
        Sha256 sha = inner;
        sha.Update(first);
        sha.Update(second);
        const Digest inside = sha.Final();
        Sha256 out = outer;
        out.Update(inside);
        return out.Final();
    }

private:
    Sha256 inner;
    Sha256 outer;
};

// Equal without telling, by its time, how much was.
inline bool SameBytes(std::span<const u8> a, std::span<const u8> b) {
    if (a.size() != b.size()) {
        return false;
    }
    u8 differ = 0;
    for (size_t index = 0; index < a.size(); ++index) {
        differ |= a[index] ^ b[index];
    }
    return differ == 0;
}

// The cookie for a source (address and port in host byte order) in a time bucket.
inline Cookie CookieFor(const HmacKey& server_key, u32 ipv4, u16 port, s64 bucket) {
    u8 message[14];
    for (int index = 0; index < 4; ++index) {
        message[index] = static_cast<u8>(ipv4 >> (24 - 8 * index));
    }
    message[4] = static_cast<u8>(port >> 8);
    message[5] = static_cast<u8>(port);
    for (int index = 0; index < 8; ++index) {
        message[6 + index] = static_cast<u8>(static_cast<u64>(bucket) >> (8 * index));
    }
    const Digest mac = server_key.Mac(message);
    Cookie cookie;
    std::memcpy(cookie.data(), mac.data(), cookie.size());
    return cookie;
}

// This bucket's or the last one's.
inline bool CookieValid(const HmacKey& server_key, u32 ipv4, u16 port, s64 now_ms,
                        std::span<const u8> cookie) {
    const s64 bucket = now_ms / CookieBucketMs;
    return SameBytes(cookie, CookieFor(server_key, ipv4, port, bucket)) ||
           SameBytes(cookie, CookieFor(server_key, ipv4, port, bucket - 1));
}

// Hosted: a hello that would make a new account carries a work nonce (docs/HOSTED_SERVER.md): one
// such that SHA-256(cookie, id, nonce as 8 bytes little-endian) starts with the server's number of
// zero bits. The cookie ties it to the hello's source and the next minute; the server checks it
// with one hash, before any password hash or account. An account once made never pays again.
constexpr int MaxWorkBits = 40;

inline Sha256 WorkPrefix(const Cookie& cookie, std::string_view online_id) {
    Sha256 sha;
    sha.Update(cookie);
    sha.Update(Bytes(online_id));
    return sha;
}

inline Digest WorkDigest(Sha256 prefix, u64 nonce) {
    u8 bytes[8];
    for (int index = 0; index < 8; ++index) {
        bytes[index] = static_cast<u8>(nonce >> (8 * index));
    }
    prefix.Update(bytes);
    return prefix.Final();
}

inline int LeadingZeroBits(const Digest& digest) {
    int bits = 0;
    for (const u8 byte : digest) {
        if (byte != 0) {
            for (u8 mask = 0x80; (byte & mask) == 0; mask >>= 1) {
                ++bits;
            }
            return bits;
        }
        bits += 8;
    }
    return bits;
}

inline bool WorkDone(const Cookie& cookie, std::string_view online_id, u64 nonce, int bits) {
    return bits <= 0 || (bits <= MaxWorkBits &&
                         LeadingZeroBits(WorkDigest(WorkPrefix(cookie, online_id), nonce)) >= bits);
}

// The first nonce of [from, from + count) that does the work, if one does (a caller that may have
// to stop asks in slices).
inline std::optional<u64> SolveWork(const Cookie& cookie, std::string_view online_id, int bits,
                                    u64 from, u64 count) {
    if (bits > MaxWorkBits) {
        return std::nullopt;
    }
    const Sha256 prefix = WorkPrefix(cookie, online_id);
    for (u64 nonce = from; nonce - from < count; ++nonce) {
        if (bits <= 0 || LeadingZeroBits(WorkDigest(prefix, nonce)) >= bits) {
            return nonce;
        }
    }
    return std::nullopt;
}

// Noise (noiseprotocol.org, revision 34) on Monocypher's primitives: what NK needs and no more.
namespace Noise {

namespace Mono = MONOCYPHER_CPP_NAMESPACE;
using Hash = std::array<u8, 64>; // BLAKE2b's

inline Key PublicOf(const Key& secret) {
    Key out;
    Mono::crypto_x25519_public_key(out.data(), secret.data());
    return out;
}

// X25519; nothing when the result is all zeros (a small-order point: no secret shared).
inline std::optional<Key> Dh(const Key& secret, const Key& other) {
    Key shared;
    Mono::crypto_x25519(shared.data(), secret.data(), other.data());
    u8 any = 0;
    for (const u8 byte : shared) {
        any |= byte;
    }
    if (any == 0) {
        return std::nullopt;
    }
    return shared;
}

inline Hash Blake2b(std::span<const u8> first, std::span<const u8> second = {},
                    std::span<const u8> third = {}) {
    Mono::crypto_blake2b_ctx ctx;
    Mono::crypto_blake2b_init(&ctx, 64);
    Mono::crypto_blake2b_update(&ctx, first.data(), first.size());
    Mono::crypto_blake2b_update(&ctx, second.data(), second.size());
    Mono::crypto_blake2b_update(&ctx, third.data(), third.size());
    Hash out;
    Mono::crypto_blake2b_final(&ctx, out.data());
    return out;
}

// HMAC over BLAKE2b (128-byte blocks), as Noise's HKDF has it; keys here are at most 64 bytes.
inline Hash Hmac(std::span<const u8> key, std::span<const u8> first,
                 std::span<const u8> second = {}) {
    u8 block[128] = {};
    std::memcpy(block, key.data(), std::min(key.size(), sizeof(block)));
    u8 pad[128];
    for (size_t index = 0; index < sizeof(pad); ++index) {
        pad[index] = block[index] ^ 0x36;
    }
    const Hash inner = Blake2b(pad, first, second);
    for (size_t index = 0; index < sizeof(pad); ++index) {
        pad[index] = block[index] ^ 0x5c;
    }
    return Blake2b(pad, inner);
}

inline std::pair<Hash, Hash> Hkdf(const Hash& chaining, std::span<const u8> input) {
    static constexpr u8 one[1] = {1};
    static constexpr u8 two[1] = {2};
    const Hash temp = Hmac(chaining, input);
    const Hash first = Hmac(temp, one);
    return {first, Hmac(temp, first, two)};
}

inline Key First32(const Hash& hash) {
    Key out;
    std::memcpy(out.data(), hash.data(), out.size());
    return out;
}

// ChaCha20-Poly1305 (RFC 8439), in place; the nonce 32 zero bits, then n little-endian.
inline void Encrypt(const Key& key, u64 n, std::span<const u8> ad, std::span<u8> text, u8* tag) {
    u8 nonce[12] = {};
    for (int index = 0; index < 8; ++index) {
        nonce[4 + index] = static_cast<u8>(n >> (8 * index));
    }
    Mono::crypto_aead_ctx ctx;
    Mono::crypto_aead_init_ietf(&ctx, key.data(), nonce);
    Mono::crypto_aead_write(&ctx, text.data(), tag, ad.data(), ad.size(), text.data(), text.size());
    Mono::crypto_wipe(&ctx, sizeof(ctx));
}

// Whether `text` and `tag` open; `text` is decrypted in place only when they do.
inline bool Decrypt(const Key& key, u64 n, std::span<const u8> ad, std::span<u8> text,
                    const u8* tag) {
    u8 nonce[12] = {};
    for (int index = 0; index < 8; ++index) {
        nonce[4 + index] = static_cast<u8>(n >> (8 * index));
    }
    Mono::crypto_aead_ctx ctx;
    Mono::crypto_aead_init_ietf(&ctx, key.data(), nonce);
    const int mismatch = Mono::crypto_aead_read(&ctx, text.data(), tag, ad.data(), ad.size(),
                                                text.data(), text.size());
    Mono::crypto_wipe(&ctx, sizeof(ctx));
    return mismatch == 0;
}

// Noise's SymmetricState: the chaining key, the handshake's hash, and the key with its nonce.
class Symmetric {
public:
    explicit Symmetric(std::span<const u8> prologue) {
        static constexpr std::string_view name = "Noise_NK_25519_ChaChaPoly_BLAKE2b";
        std::memcpy(hash.data(), name.data(), name.size());
        chaining = hash;
        MixHash(prologue);
    }

    void MixHash(std::span<const u8> data) {
        hash = Blake2b(hash, data);
    }

    void MixKey(const Key& input) {
        const auto [chain, next] = Hkdf(chaining, input);
        chaining = chain;
        key = First32(next);
        n = 0;
    }

    // `text` encrypted in place, its tag into `tag`.
    void EncryptAndHash(std::span<u8> text, u8* tag) {
        Encrypt(key, n++, hash, text, tag);
        hash = Blake2b(hash, text, {tag, TagBytes});
    }

    bool DecryptAndHash(std::span<u8> text, const u8* tag) {
        const Hash next = Blake2b(hash, text, {tag, TagBytes});
        if (!Decrypt(key, n, hash, text, tag)) {
            return false;
        }
        ++n;
        hash = next;
        return true;
    }

    // The keys of the initiator's messages (the game's: up) and the responder's (down).
    std::pair<Key, Key> Split() const {
        const auto [up, down] = Hkdf(chaining, {});
        return {First32(up), First32(down)};
    }

private:
    Hash chaining{};
    Hash hash{};
    Key key{};
    u64 n = 0;
};

} // namespace Noise

// The counters taken, the last Size of them remembered: a datagram late by more is dropped too.
class ReplayWindow {
public:
    static constexpr u64 Size = 1024;

    bool Fresh(u64 counter) const {
        if (counter == 0) {
            return false; // counters start at 1
        }
        if (counter > top) {
            return true;
        }
        return top - counter < Size && !(bits[(counter % Size) / 64] >> (counter % 64) & 1);
    }

    void Take(u64 counter) {
        if (counter > top) {
            if (counter - top >= Size) {
                bits.fill(0);
            } else {
                for (u64 skipped = top + 1; skipped < counter; ++skipped) {
                    bits[(skipped % Size) / 64] &= ~(u64{1} << (skipped % 64));
                }
            }
            top = counter;
        }
        bits[(counter % Size) / 64] |= u64{1} << (counter % 64);
    }

private:
    u64 top = 0;
    std::array<u64, Size / 64> bits{};
};

// One side of a link once the handshake is done: encrypts what it sends with one key, opens what
// comes with the other.
class Session {
public:
    enum class Verdict { Ok, Short, BadTag, Replay };

    Session(const Key& send_key, const Key& receive_key) : send{send_key}, receive{receive_key} {}

    // `datagram`'s body (past its header) encrypted in place; the counter and tag go after it.
    Trailer Seal(std::span<u8> datagram) {
        Trailer trailer;
        const u64 counter = next++;
        for (size_t index = 0; index < CounterBytes; ++index) {
            trailer[index] = static_cast<u8>(counter >> (8 * index));
        }
        const size_t header = std::min(datagram.size(), HeaderBytes);
        Noise::Encrypt(send, counter, datagram.first(header), datagram.subspan(header),
                       trailer.data() + CounterBytes);
        return trailer;
    }

    // A datagram from the other side (its trailer last). On Ok its first size - TrailerBytes bytes
    // are the plain datagram; otherwise it is as it came. A counter seen already costs no crypto.
    Verdict Open(std::span<u8> datagram) {
        if (datagram.size() < HeaderBytes + TrailerBytes) {
            return Verdict::Short;
        }
        const size_t body = datagram.size() - TrailerBytes;
        u64 counter = 0;
        for (size_t index = 0; index < CounterBytes; ++index) {
            counter |= u64{datagram[body + index]} << (8 * index);
        }
        if (!window.Fresh(counter)) {
            return Verdict::Replay;
        }
        if (!Noise::Decrypt(receive, counter, datagram.first(HeaderBytes),
                            datagram.subspan(HeaderBytes, body - HeaderBytes),
                            datagram.data() + body + CounterBytes)) {
            return Verdict::BadTag;
        }
        window.Take(counter);
        return Verdict::Ok;
    }

private:
    Key send;
    Key receive;
    u64 next = 1;
    ReplayWindow window;
};

// The server's static key pair (made once, LinkServer keeps it in LinkKeyFile).
struct ServerKey {
    Key secret{};
    Key pub{};
};
inline ServerKey ServerKeyOf(const Key& secret) {
    return {secret, Noise::PublicOf(secret)};
}

// The server's side of a hello: opens it, and answers it (as many times as it must: each answer
// starts from the hello's state).
class Responder {
public:
    // A hello opened with the server's key: the handshake to answer it, and in `plain` the hello
    // as the game wrote it (its clear part, then the rest). Nothing when it does not open (another
    // server's key, or changed on the way).
    static std::optional<Responder> Open(const ServerKey& key, std::span<const u8> hello,
                                         std::vector<u8>& plain) {
        if (hello.size() < FirstHelloBytes) {
            return std::nullopt;
        }
        Responder responder{hello.first(HelloClearBytes)};
        responder.state.MixHash(key.pub);
        std::memcpy(responder.game.data(), hello.data() + HelloClearBytes, KeyBytes);
        responder.state.MixHash(responder.game);
        const auto shared = Noise::Dh(key.secret, responder.game);
        if (!shared) {
            return std::nullopt;
        }
        responder.state.MixKey(*shared);
        std::vector<u8> opened(hello.begin(), hello.begin() + HelloClearBytes);
        opened.insert(opened.end(), hello.begin() + HelloClearBytes + KeyBytes,
                      hello.end() - TagBytes);
        if (!responder.state.DecryptAndHash(std::span{opened}.subspan(HelloClearBytes),
                                            hello.data() + hello.size() - TagBytes)) {
            return std::nullopt;
        }
        plain = std::move(opened);
        return responder;
    }

    struct Answered {
        std::vector<u8> datagram;
        Session session; // the link's, once the game takes it (an accepted welcome only)
    };
    // The answer carrying `plain` (a whole datagram: a welcome, a refusal, a work ask), under the
    // fresh ephemeral key `ephemeral`.
    Answered Answer(std::span<const u8> plain, const Key& ephemeral) const {
        Noise::Symmetric answer = state;
        const Key mine = Noise::PublicOf(ephemeral);
        answer.MixHash(mine);
        answer.MixKey(Noise::Dh(ephemeral, game).value_or(Key{})); // Open refused small orders
        std::vector<u8> datagram(plain.begin(),
                                 plain.begin() + std::min(plain.size(), HeaderBytes));
        datagram.insert(datagram.end(), mine.begin(), mine.end());
        datagram.insert(datagram.end(), plain.begin(), plain.end());
        u8 tag[TagBytes];
        answer.EncryptAndHash(std::span{datagram}.subspan(HeaderBytes + KeyBytes), tag);
        datagram.insert(datagram.end(), tag, tag + TagBytes);
        const auto [up, down] = answer.Split();
        return {std::move(datagram), Session{down, up}};
    }

    // The game's ephemeral key: a hello said again has the same.
    const Key& GameKey() const {
        return game;
    }

private:
    explicit Responder(std::span<const u8> prologue) : state{prologue} {}

    Noise::Symmetric state;
    Key game{};
};

// The game's side: the server's key, its cookie, the hello under way (pending: its handshake) and
// the session the server welcomed (current).
class Client {
public:
    // The server's key from the launcher: only cookies that say it are taken. Without one the
    // first cookie's is used, and kept once a welcome proves the server holds it.
    void Pin(const Key& key) {
        server = key;
        pinned = true;
    }
    bool Pinned() const {
        return pinned;
    }
    const std::optional<Key>& ServerKey() const {
        return server;
    }
    // A key taken on first use that a welcome just proved, once (the game logs it, and the
    // launcher pins it for that server).
    std::optional<Key> TakeLearned() {
        return std::exchange(learned, std::nullopt);
    }

    // A new link (configured, or the server may have restarted): the next hello starts a new
    // handshake; the cookie stays, and the current session until a welcome replaces it.
    void Restart() {
        pending.reset();
    }
    // The server's cookie and key: not taken (false) when the key is not the pinned one.
    bool TakeCookie(const Cookie& from_server, const Key& key) {
        if (pinned && !SameBytes(key, *server)) {
            return false;
        }
        cookie = from_server;
        has_cookie = true;
        if (!pinned) {
            server = key;
        }
        return true;
    }
    bool HasCookie() const {
        return has_cookie;
    }
    const Cookie& CookieToEcho() const {
        return cookie;
    }

    // Hosted: the server asked this id's hello for work (no account yet). Whether the work is to
    // be done now: not when it is done or under way for this cookie. The hello with it is a new
    // handshake.
    bool WorkAsked(int bits) {
        if (bits == work_bits && (working || WorkToSend())) {
            return false;
        }
        work_bits = bits;
        work.reset();
        working = true;
        Restart();
        return true;
    }
    bool Working() const {
        return working;
    }
    int WorkBits() const {
        return work_bits;
    }
    // The work done for a cookie (none: given up); kept for the hellos that echo that cookie.
    void TakeWork(const Cookie& done_for, int bits, std::optional<u64> work_nonce) {
        working = false;
        if (work_nonce && bits == work_bits) {
            work = std::pair{done_for, *work_nonce};
        }
    }
    std::optional<u64> WorkToSend() const {
        if (work && has_cookie && SameBytes(work->first, cookie)) {
            return work->second;
        }
        return std::nullopt;
    }

    // The datagram for the hello `plain` (its clear part, then the rest). Without a cookie only
    // the clear part, padded to FirstHelloBytes: nothing secret goes before the cookie. With one,
    // the handshake's first message, made anew (under `fresh`, a new ephemeral key) only when the
    // hello's bytes changed: a hello said again is the same datagram, so any answer to it opens,
    // and no key ever encrypts two different texts.
    std::vector<u8> SealHello(std::span<const u8> plain, const Key& fresh) {
        if (plain.size() < HelloClearBytes) {
            return {plain.begin(), plain.end()};
        }
        std::vector<u8> first(plain.begin(), plain.begin() + HelloClearBytes);
        first.resize(FirstHelloBytes, 0);
        if (!has_cookie || !server) {
            return first;
        }
        if (pending && std::ranges::equal(pending->plain, plain)) {
            return pending->sealed;
        }
        Pending made{{plain.begin(), plain.end()},
                     {},
                     Noise::Symmetric{plain.first(HelloClearBytes)},
                     fresh};
        made.state.MixHash(*server);
        const Key mine = Noise::PublicOf(fresh);
        made.state.MixHash(mine);
        const auto shared = Noise::Dh(fresh, *server);
        if (!shared) {
            return first; // a key no server can have
        }
        made.state.MixKey(*shared);
        made.sealed.assign(plain.begin(), plain.begin() + HelloClearBytes);
        made.sealed.insert(made.sealed.end(), mine.begin(), mine.end());
        made.sealed.insert(made.sealed.end(), plain.begin() + HelloClearBytes, plain.end());
        u8 tag[TagBytes];
        made.state.EncryptAndHash(std::span{made.sealed}.subspan(HelloClearBytes + KeyBytes), tag);
        made.sealed.insert(made.sealed.end(), tag, tag + TagBytes);
        pending.emplace(std::move(made));
        return pending->sealed;
    }

    // The counter and tag for a datagram to the server (its body encrypted in place); nothing
    // before a welcome (the server would drop it).
    std::optional<Trailer> Seal(std::span<u8> datagram) {
        if (!current) {
            return std::nullopt;
        }
        return current->Seal(datagram);
    }

    // A datagram from the server opened: the plain datagram, or nothing. A welcome or a work ask
    // answers the hello under way and opens with its handshake (the same hello may be answered
    // more than once); an accepted welcome makes its keys the link's. A redirect does either: a
    // hello's answer, or on the link. Everything else opens with the link's keys. A cookie is
    // never encrypted: TakeCookie.
    std::optional<std::vector<u8>> Open(std::span<const u8> datagram) {
        if (datagram.size() < HeaderBytes) {
            return std::nullopt;
        }
        if (datagram[KindAt] == KindWelcome || datagram[KindAt] == KindWork) {
            return OpenAnswer(datagram);
        }
        if (datagram[KindAt] == KindRedirect) {
            if (auto answer = OpenAnswer(datagram)) {
                return answer;
            }
        }
        if (!current) {
            return std::nullopt;
        }
        std::vector<u8> plain(datagram.begin(), datagram.end());
        if (current->Open(plain) != Session::Verdict::Ok) {
            return std::nullopt;
        }
        plain.resize(plain.size() - TrailerBytes);
        return plain;
    }

    bool Linked() const {
        return current.has_value();
    }

private:
    std::optional<std::vector<u8>> OpenAnswer(std::span<const u8> datagram) {
        if (!pending || datagram.size() < HeaderBytes + AnswerSealBytes + HeaderBytes) {
            return std::nullopt;
        }
        Noise::Symmetric answer = pending->state;
        Key theirs;
        std::memcpy(theirs.data(), datagram.data() + HeaderBytes, KeyBytes);
        answer.MixHash(theirs);
        const auto shared = Noise::Dh(pending->ephemeral, theirs);
        if (!shared) {
            return std::nullopt;
        }
        answer.MixKey(*shared);
        std::vector<u8> plain(datagram.begin() + HeaderBytes + KeyBytes, datagram.end() - TagBytes);
        if (!answer.DecryptAndHash(plain, datagram.data() + datagram.size() - TagBytes) ||
            plain.size() <= HeaderBytes ||
            !std::equal(plain.begin(), plain.begin() + HeaderBytes, datagram.begin())) {
            return std::nullopt;
        }
        if (datagram[KindAt] == KindWelcome && plain[HeaderBytes] != 0) {
            const auto [up, down] = answer.Split();
            current.emplace(up, down);
            pending.reset();
            if (!pinned) {
                pinned = true;
                learned = server;
            }
        }
        return plain;
    }

    struct Pending {
        std::vector<u8> plain;
        std::vector<u8> sealed;
        Noise::Symmetric state;
        Key ephemeral{};
    };

    std::optional<Key> server;
    bool pinned = false;
    std::optional<Key> learned;
    Cookie cookie{};
    bool has_cookie = false;
    int work_bits = 0;
    bool working = false;
    std::optional<std::pair<Cookie, u64>> work;
    std::optional<Pending> pending;
    std::optional<Session> current;
};

// Keys as the launcher and the logs write them: 64 hex digits.
inline std::optional<Key> KeyFromHex(std::string_view text) {
    if (text.size() != KeyBytes * 2) {
        return std::nullopt;
    }
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    Key key;
    for (size_t index = 0; index < KeyBytes; ++index) {
        const int high = digit(text[2 * index]);
        const int low = digit(text[2 * index + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        key[index] = static_cast<u8>(high << 4 | low);
    }
    return key;
}

inline std::string HexOf(const Key& key) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (const u8 byte : key) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

} // namespace LinkAuth
