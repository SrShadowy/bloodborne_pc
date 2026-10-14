// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "bbport_frame_rate_tables.h"

#include <cstdint>
#include <vector>

namespace BbFrameRate {

struct SprjFlipperFrameHistoryEntry {
    std::uint64_t elapsed_microseconds;
    std::uint8_t delayed_or_behind;
    std::uint8_t pad09[7];
};

struct SprjFlipper {
    void* vftable;
    std::uint32_t primary_flip_mode_raw;
    std::uint32_t secondary_flip_mode_raw;
    std::uint32_t frame_advance_count;
    std::uint8_t allow_frame_skip;
    std::uint8_t pad15[3];
    float target_frame_seconds;
    std::uint8_t pad1c[4];
    std::uint64_t previous_frame_microseconds;
    std::uint64_t current_frame_microseconds;
    std::uint64_t phase_timestamps_microseconds[6];
    SprjFlipperFrameHistoryEntry frame_history[32];
    std::uint32_t frame_history_index;
    float measured_frame_seconds;
    std::uint32_t foreground_history_count;
    std::uint32_t background_history_count;
    std::uint8_t frame_behind;
    std::uint8_t reset_frame_history;
    std::uint8_t force_frame_advance;
    std::uint8_t force_no_sleep;
    std::uint8_t request_window_60_hz;
    std::uint8_t secondary_mode_active;
    std::uint8_t secondary_mode_requested;
    std::uint8_t pad277;
    float fps_frame_time_history[16];
    float calculated_fps;
    std::int32_t override_foreground_history_count;
    std::int32_t override_background_history_count;
    std::uint8_t reset_to_30_fps_pending;
    std::uint8_t use_realtime_clock;
    std::uint8_t pad2c6[2];
};

static_assert(sizeof(SprjFlipper) == 0x2c8, "SprjFlipper layout");

struct DtSlot {
    float* dst;
    const float* live_src;
    float was;
    Kind kind;
    std::uint8_t lanes;
};

struct PacerState {
    unsigned char* image = nullptr;
    int game_fps = 30;
    float* step_ptr = nullptr;
    std::vector<DtSlot> dt_slots;
    float* cloth_scalars = nullptr;
    float cloth_dt = 1.0f / 60.0f;
};

PacerState& State();
float RatedValue(Kind kind, float was, double fps);
extern "C" void SprjFlipperUpdateHook(SprjFlipper* f);

} // namespace BbFrameRate
