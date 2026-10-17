// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_frame_rate_pacer.h"

#include "bbport_settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sys/time.h>
#include <unistd.h>

namespace BbFrameRate {
namespace {

constexpr std::uint32_t kGuestBase = 0x400000;
constexpr std::uint32_t kWindowObjectSlot = 0x05940e00;
constexpr int kUncappedMaxFps = 240;
constexpr float kThirtieth = 0.0333333351f;
constexpr float kSixtieth = 0.0166666675f;

struct LimiterTuning {
    std::int64_t late_us[128] = {};
    std::uint32_t samples = 0;
    std::int64_t margin_us = 2000;
} g_limiter;

std::int64_t HostClockRealtimeUs() {
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    return static_cast<std::int64_t>(tv.tv_sec) * 1000000LL + static_cast<std::int64_t>(tv.tv_usec);
}

void LimiterNoteSleep(std::int64_t asked_us, std::int64_t took_us) {
    g_limiter.late_us[g_limiter.samples % 128] = std::max<std::int64_t>(0, took_us - asked_us);
    if (++g_limiter.samples < 32 || (g_limiter.samples % 16) != 0) return;
    const std::uint32_t n = std::min<std::uint32_t>(g_limiter.samples, 128);
    std::int64_t first = 0, second = 0;
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::int64_t v = g_limiter.late_us[i];
        if (v > first) { second = first; first = v; }
        else if (v > second) { second = v; }
    }
    g_limiter.margin_us = std::clamp<std::int64_t>(second + 200, 300, 2000);
}

inline float UsToFloat(std::int64_t us) {
    if (us >= 0) return static_cast<float>(us);
    const auto u = static_cast<std::uint64_t>(us);
    const float h = static_cast<float>((u >> 1) | (u & 1));
    return h + h;
}

inline std::int64_t SecondsToUs(float s) {
    const double d = static_cast<double>(s) * 1000000.0;
    return d >= 9223372036854775808.0
               ? static_cast<std::int64_t>(d - 9223372036854775808.0) ^ INT64_MIN
               : static_cast<std::int64_t>(d);
}

bool IsFastLoadingNow(const unsigned char* image) {
    if (!image || image[0x0596286b - kGuestBase] == 0) return false;
    std::uint64_t helper = 0, remo = 0;
    std::memcpy(&helper, image + (0x0593e8b8 - kGuestBase), 8);
    if (helper >= 0x100000000ULL) {
        const auto* h = reinterpret_cast<const std::uint8_t*>(helper);
        if (h[0x50] || h[0x51]) return false;
    }
    std::memcpy(&remo, image + (0x05940058 - kGuestBase), 8);
    if (remo >= 0x100000000ULL) {
        std::uint64_t man = 0, queued = 0;
        std::uint32_t flags = 0;
        std::memcpy(&man, reinterpret_cast<const void*>(remo + 8), 8);
        if (man >= 0x100000000ULL) {
            std::memcpy(&flags, reinterpret_cast<const void*>(man + 0x168), 4);
            std::memcpy(&queued, reinterpret_cast<const void*>(man + 0xf8), 8);
            if ((flags & 1u) != 0 || queued != 0) return false;
        }
    }
    return true;
}

float PaceTarget() {
    const int mode = State().game_fps;
    if (mode == 0) {
        const int limit = BbSettings::Get().fps_limit.load(std::memory_order_relaxed);
        const int cap = (limit > 0) ? std::clamp(limit, 30, kUncappedMaxFps) : kUncappedMaxFps;
        return 1.0f / static_cast<float>(cap);
    }
    if (mode == 30) return kThirtieth;
    if (mode == 60) return kSixtieth;
    if (mode > 60) return 1.0f / static_cast<float>(mode);
    return kThirtieth;
}

bool ConfigureFlipperMode(SprjFlipper* f, float target) {
    const bool converted = target > 0.0f;
    f->secondary_mode_active = f->secondary_mode_requested;
    f->secondary_mode_requested = 0;
    std::uint32_t mode = f->secondary_mode_active ? f->secondary_flip_mode_raw : f->primary_flip_mode_raw;
    if (converted) mode = 2u;
    if (mode > 4) mode = 0;

    static constexpr std::uint32_t kAdv[5] = {2, 2, 1, 1, 1};
    static constexpr std::uint8_t kSkip[5] = {1, 1, 1, 0, 0};
    static constexpr std::uint32_t kFg[5] = {1, 0, 0, 1, 0};
    f->frame_advance_count = kAdv[mode];
    f->allow_frame_skip = kSkip[mode];
    f->target_frame_seconds = (mode == 2) ? kSixtieth : kThirtieth;
    f->foreground_history_count = kFg[mode];
    f->background_history_count = 30;
    if (converted) {
        f->target_frame_seconds = IsFastLoadingNow(State().image) ? 0.0f : target;
        f->foreground_history_count = f->background_history_count = 0;
        f->override_foreground_history_count = f->override_background_history_count = -1;
        f->reset_frame_history = f->force_frame_advance = f->reset_to_30_fps_pending = 0;
        return false;
    }
    bool no_wait = false;
    if (f->reset_to_30_fps_pending) {
        f->frame_advance_count = 1;
        f->allow_frame_skip = f->reset_to_30_fps_pending = 0;
        f->target_frame_seconds = kThirtieth;
        f->foreground_history_count = f->background_history_count = 0;
        no_wait = true;
    }
    if (f->override_foreground_history_count >= 0)
        f->foreground_history_count = static_cast<std::uint32_t>(f->override_foreground_history_count);
    if (f->override_background_history_count >= 0)
        f->background_history_count = static_cast<std::uint32_t>(f->override_background_history_count);
    if (f->reset_frame_history) {
        f->foreground_history_count = f->background_history_count = f->reset_frame_history = 0;
    }
    if (f->force_frame_advance) f->foreground_history_count = f->background_history_count = 0;
    return no_wait;
}

void WaitUntilDeadline(SprjFlipper* f, std::int64_t interval_us, std::int64_t& now) {
    const float target_s = UsToFloat(interval_us) / 1000000.0f;
    const auto prev = static_cast<std::int64_t>(f->previous_frame_microseconds);
    float remaining = target_s - UsToFloat(now - prev) / 1000000.0f;
    const float margin = static_cast<float>(g_limiter.margin_us) * 1e-6f;
    while (!(0.0f > remaining)) {
        const float left = std::min(target_s, remaining);
        if (left <= 0.0f) break;
        const auto us = !(left <= margin) ? static_cast<std::int32_t>((left - margin) * 1000000.0f) : 0;
        if (us > 0) {
            const std::int64_t t0 = now;
            usleep(static_cast<useconds_t>(us));
            now = HostClockRealtimeUs();
            LimiterNoteSleep(us, now - t0);
            f->current_frame_microseconds = static_cast<std::uint64_t>(now);
            remaining = target_s + UsToFloat(now - prev) / -1000000.0f;
            continue;
        }
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#endif
        now = HostClockRealtimeUs();
        f->current_frame_microseconds = static_cast<std::uint64_t>(now);
        remaining = target_s + UsToFloat(now - prev) / -1000000.0f;
    }
}

void SprjFlipperUpdate(SprjFlipper* f, float target) {
    const bool no_wait = ConfigureFlipperMode(f, target);
    f->previous_frame_microseconds = f->current_frame_microseconds;
    std::int64_t now = HostClockRealtimeUs();
    f->current_frame_microseconds = static_cast<std::uint64_t>(now);
    std::int64_t interval_us = SecondsToUs(f->target_frame_seconds);
    if (f->previous_frame_microseconds == 0)
        f->previous_frame_microseconds = static_cast<std::uint64_t>(now - interval_us);

    const std::uint32_t allowance = f->foreground_history_count;
    std::uint32_t walked = 0;
    std::uint64_t behind_us = 0;
    for (int back = 0; walked < allowance; --back) {
        const auto& e = f->frame_history[(f->frame_history_index + static_cast<std::uint32_t>(back)) & 0x1f];
        behind_us += e.elapsed_microseconds;
        ++walked;
        if (e.delayed_or_behind == 0) break;
    }
    if (f->frame_behind) {
        const std::uint64_t budget = static_cast<std::uint64_t>(walked + 1) * static_cast<std::uint64_t>(interval_us);
        if (behind_us + static_cast<std::uint64_t>(interval_us) > budget) {
            const std::uint64_t third = static_cast<std::uint64_t>(interval_us) / 3;
            interval_us = static_cast<std::int64_t>(
                budget <= behind_us ? third : std::max(third, std::min<std::uint64_t>(interval_us, budget - behind_us)));
        }
    }
    if (!(no_wait || f->force_no_sleep)) WaitUntilDeadline(f, interval_us, now);
    if (f->force_no_sleep) f->frame_behind = 1;
    const std::uint32_t idx = (f->frame_history_index + 1) & 0x1f;
    f->frame_history_index = idx;
    const auto frame_us = static_cast<std::int64_t>(f->current_frame_microseconds - f->previous_frame_microseconds);
    f->frame_history[idx].elapsed_microseconds = static_cast<std::uint64_t>(frame_us);
    f->frame_history[idx].delayed_or_behind = f->frame_behind;
    const float frame_s = UsToFloat(frame_us) / 1000000.0f;
    f->measured_frame_seconds = frame_s;

    const bool exhausted = f->frame_behind ? (walked >= allowance) : (allowance == 0);
    const std::int64_t tol_us = SecondsToUs(f->target_frame_seconds * 0.00999999978f);
    const std::int64_t tgt_us = SecondsToUs(f->target_frame_seconds);
    const std::uint64_t budget =
        static_cast<std::uint64_t>(walked + 1) * static_cast<std::uint64_t>(tgt_us) + static_cast<std::uint64_t>(tol_us);
    const bool on_time = !f->force_no_sleep && ((static_cast<std::uint64_t>(frame_us) + behind_us >= budget) ? exhausted : true);
    f->frame_behind = on_time ? 0 : 1;
    if (!on_time) f->frame_advance_count = 0;

    float sum = 0.0f;
    for (int k = 0; k < 15; ++k) {
        f->fps_frame_time_history[k] = f->fps_frame_time_history[k + 1];
        sum += f->fps_frame_time_history[k];
    }
    f->fps_frame_time_history[15] = frame_s;
    sum += frame_s;
    f->calculated_fps = sum <= 0.00100000005f ? 0.0f : 16.0f / sum;
    f->force_no_sleep = 0;

    if (State().image && f->request_window_60_hz) {
        std::uintptr_t window = 0;
        std::memcpy(&window, State().image + (kWindowObjectSlot - kGuestBase), sizeof(window));
        if (window >= 0x100000000ULL) {
            auto* w = reinterpret_cast<std::uint8_t*>(window);
            w[0x4c] = w[0x4d] = 1;
            const std::uint32_t hz60 = 60;
            std::memcpy(w + 0x50, &hz60, 4);
            f->request_window_60_hz = 0;
        }
    }
}

void EachFrame(const SprjFlipper* f) {
    auto& st = State();
    const float step = std::min(std::max(f->measured_frame_seconds, f->target_frame_seconds), kThirtieth);
    if (step > 0.0f && st.step_ptr) *st.step_ptr = step;
    const float floor_s = std::max(f->target_frame_seconds, 1.0f / static_cast<float>(kUncappedMaxFps));
    const float dt = std::min(std::max(f->measured_frame_seconds, floor_s), kThirtieth);
    if (!(dt > 0.0f)) return;
    const double fps = 1.0 / static_cast<double>(dt);
    for (const DtSlot& d : st.dt_slots) {
        for (std::uint8_t k = 0; k < d.lanes; ++k)
            d.dst[k] = RatedValue(d.kind, d.live_src ? d.live_src[k] : d.was, fps);
    }
    if (st.cloth_scalars) {
        st.cloth_dt += (dt - st.cloth_dt) * 0.1f;
        const float cloth = std::min(1.0f, 60.0f * st.cloth_dt);
        for (int k = 0; k < 4; ++k) st.cloth_scalars[k] = cloth * cloth;
    }
}

} // namespace

PacerState& State() { static PacerState s; return s; }

float RatedValue(Kind kind, float was, double fps) {
    switch (kind) {
    case Kind::Step: return static_cast<float>(1.0 / fps);
    case Kind::Amount: return static_cast<float>(static_cast<double>(was) * 30.0 / fps);
    case Kind::Decay: return static_cast<float>(1.0 - std::pow(1.0 - static_cast<double>(was), 30.0 / fps));
    case Kind::Keep: return static_cast<float>(std::pow(static_cast<double>(was), 30.0 / fps));
    case Kind::Rate: return static_cast<float>(static_cast<double>(was) * fps / 30.0);
    }
    return was;
}

extern "C" void SprjFlipperUpdateHook(SprjFlipper* f) {
    if (!f) return;
    SprjFlipperUpdate(f, PaceTarget());
    EachFrame(f);
}

} // namespace BbFrameRate
