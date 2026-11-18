// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Unit tests for Vulkan::RecorderRest adaptive rest state machine and Scheduler cache-line layout.
#include <cassert>
#include <cstdio>

#include "render/recorder_rest.h"
#include "render/scheduler.h"

namespace {

using namespace Vulkan::RecorderRest;

static_assert(alignof(Vulkan::Scheduler) >= 64, "Scheduler must be cache-line aligned");

void TestLoadingResetsState() {
    State s;
    assert(Step(s, 0.30, 0.40, 0.20, false) == false);
    assert(Step(s, 0.30, 0.40, 0.20, false) == true);
    assert(s.cap);

    // Loading frame must clear all resting state immediately.
    assert(Step(s, 0.50, 0.20, 0.10, true) == false);
    assert(!s.cap);
    assert(!s.gpu);
    assert(s.quiet == 0);
    assert(s.bound == 0);
    assert(s.busy_n == 0);
}

void TestCapSlackHysteresis() {
    State s;

    // Single high-slack window does not rest immediately.
    assert(Step(s, 0.20, 0.50, 0.25, false) == false);
    assert(!s.cap);
    assert(s.quiet == 1);

    // Second consecutive high-slack window enters cap rest.
    assert(Step(s, 0.15, 0.50, 0.25, false) == true);
    assert(s.cap);

    // Hysteresis zone (kWakeSlack=0.08 <= slack < kRestSlack=0.15) stays resting.
    assert(Step(s, 0.10, 0.50, 0.25, false) == true);
    assert(s.cap);

    // Dropping below kWakeSlack wakes the recorder immediately.
    assert(Step(s, 0.07, 0.50, 0.25, false) == false);
    assert(!s.cap);

    // Refuses to enter cap rest when GPU has insufficient room (>= kGpuRoom=0.85) or CP full.
    s = State{};
    assert(Step(s, 0.30, 0.86, 0.20, false) == false);
    assert(Step(s, 0.30, 0.86, 0.20, false) == false);
    assert(!s.cap);

    s = State{};
    assert(Step(s, 0.30, 0.40, 0.93, false) == false);
    assert(Step(s, 0.30, 0.40, 0.93, false) == false);
    assert(!s.cap);
}

void TestCapRestWakesOnFullGpuOrCp() {
    State s;
    assert(Step(s, 0.25, 0.40, 0.20, false) == false);
    assert(Step(s, 0.25, 0.40, 0.20, false) == true);
    assert(s.cap);

    // Sudden GPU saturation (>= kGpuFull=0.92) cancels cap rest immediately.
    assert(Step(s, 0.25, 0.92, 0.20, false) == false);
    assert(!s.cap);

    // Enter again, then saturate CP (>= kCpFull=0.92).
    assert(Step(s, 0.25, -1.0, 0.30, false) == false);
    assert(Step(s, 0.25, -1.0, 0.30, false) == true);
    assert(s.cap);
    assert(Step(s, 0.25, -1.0, 0.92, false) == false);
    assert(!s.cap);
}

void TestGpuBoundEntryAndMedianExit() {
    State s;

    // Does not enter GPU-bound when CP is near room limit (>= kCpRoom=0.85).
    assert(Step(s, 0.0, 0.95, 0.86, false) == false);
    assert(Step(s, 0.0, 0.95, 0.86, false) == false);
    assert(!s.gpu);

    // Two consecutive GPU-bound windows with CP headroom enter gpu rest.
    s = State{};
    assert(Step(s, 0.0, 0.91, 0.40, false) == false);
    assert(Step(s, 0.0, 0.92, 0.40, false) == true);
    assert(s.gpu);

    // Stays in gpu rest until 6 windows are sampled and median drops below kGpuFree (0.80).
    assert(Step(s, 0.0, 0.50, 0.40, false) == true); // n=3
    assert(Step(s, 0.0, 0.50, 0.40, false) == true); // n=4
    assert(Step(s, 0.0, 0.95, 0.40, false) == true); // n=5
    // At n=6: samples are {0.91, 0.92, 0.50, 0.50, 0.95, 0.90} -> sorted {0.50, 0.50, 0.90, 0.91, 0.92, 0.95}
    // median = 0.905 >= 0.80, so still resting!
    assert(Step(s, 0.0, 0.90, 0.40, false) == true);
    assert(s.gpu);

    // Dropping median of last 6 windows below kGpuFree (0.80) exits gpu rest.
    // Replaces busy[0] (0.91 -> 0.40): sorted {0.40, 0.50, 0.50, 0.90, 0.92, 0.95}, median=0.70.
    assert(Step(s, 0.0, 0.40, 0.40, false) == false);
    assert(!s.gpu);
}

void TestGpuBoundImmediateExits() {
    State s;
    assert(Step(s, 0.0, 0.93, 0.30, false) == false);
    assert(Step(s, 0.0, 0.93, 0.30, false) == true);
    assert(s.gpu);

    // Unknown GPU share (< 0) drops gpu rest immediately.
    assert(Step(s, 0.0, -1.0, 0.30, false) == false);
    assert(!s.gpu);

    // Cap slack >= kRestSlack clears gpu rest and begins quiet tracking for cap rest.
    s = State{};
    assert(Step(s, 0.0, 0.93, 0.30, false) == false);
    assert(Step(s, 0.0, 0.93, 0.30, false) == true);
    assert(Step(s, 0.20, 0.50, 0.30, false) == false);
    assert(!s.gpu);
    assert(s.quiet == 1);
}

} // namespace

int main() {
    TestLoadingResetsState();
    TestCapSlackHysteresis();
    TestCapRestWakesOnFullGpuOrCp();
    TestGpuBoundEntryAndMedianExit();
    TestGpuBoundImmediateExits();
    std::printf("PASS test_recorder_rest\n");
    return 0;
}
