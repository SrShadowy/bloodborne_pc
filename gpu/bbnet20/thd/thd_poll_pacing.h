// SPDX-License-Identifier: GPL-3.0-or-later
// Event poller pacing, backoff with jitter, and generation-gated cancellation for The Hunter's
// Dream long polling (/np/events/poll). Adapted from droogie/bbhost src/net/poll_pacing.h.
#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

namespace BbThd::PollPacing {

constexpr int kWaitMs = 25000;
constexpr int kReadMarginMs = 5000;
constexpr int kConnectMs = 5000;
constexpr int kPlainTimeoutMs = 4000;
constexpr int kPaceMs = 250;
constexpr int kBackoffFirstMs = 1000;
constexpr int kBackoffMaxMs = 30000;
constexpr int kRefusedMs = 60000;

struct State {
    int wait_ms = kWaitMs;
    int backoff_ms = 0;
};

struct Outcome {
    bool answered = false;
    long long wait_max_ms = -1;
    bool event = false;
    long long elapsed_ms = 0;
    long status = 0;
    bool refused = false;
};

struct Next {
    State state;
    int sleep_ms = 0;
};

[[nodiscard]] inline int TimeoutFor(int wait_ms) noexcept {
    return wait_ms <= 0 ? kPlainTimeoutMs : wait_ms + kReadMarginMs;
}

[[nodiscard]] inline int ConnectFor(int wait_ms) noexcept {
    return std::min(kConnectMs, TimeoutFor(wait_ms));
}

[[nodiscard]] inline int BackoffAfter(int prev_ms) noexcept {
    if (prev_ms <= 0) {
        return kBackoffFirstMs;
    }
    return std::min(prev_ms * 2, kBackoffMaxMs);
}

[[nodiscard]] inline int Jittered(int back_ms, double unit) noexcept {
    if (unit < 0.0) {
        unit = 0.0;
    }
    if (unit >= 1.0) {
        unit = 0.999999;
    }
    constexpr double kSpread = 0.4;
    return static_cast<int>(back_ms * (1.0 - kSpread / 2.0 + kSpread * unit) + 0.5);
}

[[nodiscard]] inline Next NextStep(const State& s, const Outcome& o, double unit) noexcept {
    Next n;
    n.state = s;
    if (o.answered) {
        n.state.backoff_ms = 0;
        if (o.wait_max_ms > 0) {
            n.state.wait_ms = static_cast<int>(std::min<long long>(kWaitMs, o.wait_max_ms));
        } else {
            n.state.wait_ms = 0;
        }
        if (o.event) {
            n.sleep_ms = 0;
        } else if (n.state.wait_ms > 0) {
            n.sleep_ms = static_cast<int>(
                std::max<long long>(0, kPaceMs - std::max<long long>(0, o.elapsed_ms)));
        } else {
            n.sleep_ms = kPaceMs;
        }
        return n;
    }
    if (o.refused || o.status == 401) {
        n.sleep_ms = kRefusedMs;
        return n;
    }
    n.state.backoff_ms = BackoffAfter(s.backoff_ms);
    n.sleep_ms = Jittered(n.state.backoff_ms, unit);
    return n;
}

class PollGate {
public:
    using AbortFn = std::function<void()>;

    std::uint64_t Start() {
        std::lock_guard<std::mutex> lk(mu_);
        current_ = ++counter_;
        cv_.notify_all();
        return current_;
    }

    void Stop() {
        AbortFn abort;
        {
            std::lock_guard<std::mutex> lk(mu_);
            current_ = 0;
            abort = abort_;
            cv_.notify_all();
        }
        if (abort) {
            abort();
        }
        // Handle the sub-microsecond race before the HTTP client marks its request in flight.
        std::unique_lock<std::mutex> lk(mu_);
        if (busy_ && !cv_.wait_for(lk, std::chrono::milliseconds(50),
                                   [&] { return !busy_ || current_ != 0; })) {
            AbortFn retry_abort = abort_;
            lk.unlock();
            if (retry_abort) {
                retry_abort();
            }
        }
    }

    [[nodiscard]] bool Running() const {
        std::lock_guard<std::mutex> lk(mu_);
        return current_ != 0;
    }

    [[nodiscard]] bool Live(std::uint64_t gen) const {
        std::lock_guard<std::mutex> lk(mu_);
        return gen != 0 && current_ == gen;
    }

    bool Begin(std::uint64_t gen) {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return !busy_ || current_ != gen; });
        if (gen == 0 || current_ != gen) {
            return false;
        }
        busy_ = true;
        return true;
    }

    void End() {
        std::lock_guard<std::mutex> lk(mu_);
        busy_ = false;
        abort_ = nullptr;
        cv_.notify_all();
    }

    bool SetAbort(std::uint64_t gen, AbortFn fn) {
        std::lock_guard<std::mutex> lk(mu_);
        if (gen == 0 || current_ != gen) {
            return false;
        }
        abort_ = std::move(fn);
        return true;
    }

    void ClearAbort() {
        std::lock_guard<std::mutex> lk(mu_);
        abort_ = nullptr;
    }

    bool Sleep(std::uint64_t gen, int ms) {
        std::unique_lock<std::mutex> lk(mu_);
        return !cv_.wait_for(lk, std::chrono::milliseconds(ms < 0 ? 0 : ms),
                              [&] { return current_ != gen; });
    }

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::uint64_t counter_ = 0;
    std::uint64_t current_ = 0;
    bool busy_ = false;
    AbortFn abort_;
};

} // namespace BbThd::PollPacing
