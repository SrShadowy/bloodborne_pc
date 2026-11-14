// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace BbThd {

/// Minimum TCP+TLS connection timeout enforced for game sceHttp calls on The Hunter's Dream:
/// PS4 hardware tolerates sub-5s values as lower bounds, whereas internet TLS handshakes fail
/// prematurely without a 5000 ms floor (droogie/bbhost src/hle/http.cpp).
inline constexpr std::uint64_t kMinConnectMs = 5000;

[[nodiscard]] inline std::chrono::milliseconds PickConnectTimeoutMs(std::uint32_t connect_timeout_us,
                                                                    std::uint32_t default_s,
                                                                    bool enforce_floor) noexcept {
    if (connect_timeout_us == 0) {
        const std::uint64_t def_ms = static_cast<std::uint64_t>(default_s) * 1000u;
        return std::chrono::milliseconds(enforce_floor ? std::max(def_ms, kMinConnectMs) : def_ms);
    }
    if (enforce_floor) {
        const std::uint64_t ms = (static_cast<std::uint64_t>(connect_timeout_us) + 999u) / 1000u;
        return std::chrono::milliseconds(std::max(ms, kMinConnectMs));
    }
    const std::uint64_t secs = std::max<std::uint64_t>(
        1u, (static_cast<std::uint64_t>(connect_timeout_us) + 999999u) / 1000000u);
    return std::chrono::milliseconds(secs * 1000u);
}

} // namespace BbThd
