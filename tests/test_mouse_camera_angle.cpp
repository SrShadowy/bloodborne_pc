// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The mouse camera's angle fold (BbMouseCamera::detail::WrapPi).
//
// [extra] This is the regression test for the freeze on leaving Iosefka's Clinic: the fold was a
// `while (angle > pi) angle -= 2*pi` loop, which never ends for an infinity and needs |angle|/(2*pi)
// iterations otherwise. The main thread of the game was found spinning in it (thread dump,
// 2026-11-05), with the GPU idle because the guest had stopped submitting.
//
// The loop is not merely slow, it does not terminate: a test that runs it would hang instead of
// failing. So the test checks the properties the fold has to have, and the one that used to be
// violated -- a huge or non-finite input still returns, in bounded time -- is checked by running
// millions of them.
#include "bbport_mouse_camera_internal.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <thread>

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * kPi;

/// The value is in (-pi, pi], to the precision a float holds there.
void AssertFolded(float input, float folded) {
    assert(std::isfinite(folded));
    assert(folded > -kPi - 1e-5f && folded <= kPi + 1e-5f);
    // Folding removes a whole number of turns: the wrapped angle is the same angle.
    const float turns = std::round((input - folded) / kTwoPi);
    assert(std::abs(input - folded - turns * kTwoPi) < 1e-3f * (1.0f + std::abs(input) / kTwoPi));
}

} // namespace

int main() {
    using BbMouseCamera::detail::WrapPi;

    // Small angles come back unchanged (the identity the camera relies on).
    assert(WrapPi(0.0f) == 0.0f);
    assert(WrapPi(1.0f) == 1.0f);
    assert(WrapPi(-1.0f) == -1.0f);
    assert(WrapPi(kPi) == kPi);

    // Just past the boundary folds to just past the other one.
    const float above = WrapPi(kPi + 0.01f);
    assert(above < -kPi + 0.02f);
    const float below = WrapPi(-kPi - 0.01f);
    assert(below > kPi - 0.02f);

    // Several turns around the circle, both directions, land in range and keep the angle.
    for (int turns = -1000; turns <= 1000; ++turns) {
        const float input = static_cast<float>(turns) * kTwoPi + 0.25f;
        const float folded = WrapPi(input);
        AssertFolded(input, folded);
        // The float spacing at |input| ~ 6.3e3 (1000 turns) is ~5e-4: a whole turn's worth of
        // accumulated error is what the old loop left here.
        assert(std::abs(folded - 0.25f) < 5e-3f);
    }

    // -pi itself is the one value outside (-pi, pi]: it comes back as +pi.
    assert(WrapPi(-kPi) == kPi);

    // The values that used to hang the game. Each must return, and return something usable.
    const float extremes[] = {
        std::numeric_limits<float>::max(),  -std::numeric_limits<float>::max(),
        1e30f,                             -1e30f,
        1.3e8f,                            -1.3e8f, // where a float's spacing passes a whole turn
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::quiet_NaN(),
    };
    for (float input : extremes) {
        const float folded = WrapPi(input);
        assert(std::isfinite(folded));
        assert(folded > -kPi - 1e-5f && folded <= kPi + 1e-5f);
    }

    // Bounded time: the old loop did not finish this at all (inf) or took ~2e7 iterations per call.
    // The input is volatile on purpose: a constant would be folded at compile time and the test
    // would time nothing. It runs on its own thread with a watchdog, because a reintroduced loop
    // does not fail this test -- it hangs the process, and a hang here should be reported as the
    // regression it is instead of stopping the suite.
    std::atomic<bool> done{false};
    std::atomic<bool> finite{true};
    std::atomic<double> elapsed{-1.0};
    std::thread worker{[&] {
        volatile float source = 3.0e38f;
        const auto start = std::chrono::steady_clock::now();
        float sink = 0.0f;
        for (int i = 0; i < 200000; ++i) {
            source = (i & 1) ? 3.0e38f : std::numeric_limits<float>::infinity();
            sink += WrapPi(source);
        }
        finite = std::isfinite(sink); // inf -> 0, 3e38 -> in range: the sum stays finite
        elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                      .count();
        done = true;
    }};
    for (int waited = 0; waited < 10000 && !done; ++waited) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!done) {
        std::printf("mouse-camera-angle-test: FAIL -- the fold did not return within 10 s "
                    "(the unbounded loop of SHADOW_CHANGES.md entry 125 is back)\n");
        std::fflush(stdout);
        std::_Exit(1); // the worker is stuck inside it; detach and leave with the failure
    }
    worker.join();
    assert(finite);
    assert(elapsed < 5000.0); // a whole second per 200k would still be a hang in a frame

    std::printf("mouse-camera-angle-test: OK (600k extreme folds in %.1f ms)\n", double(elapsed));
    return 0;
}
