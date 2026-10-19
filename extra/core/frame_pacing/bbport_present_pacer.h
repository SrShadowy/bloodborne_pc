// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace Extra::PresentPacer {

/// Called at the start of Presenter::Present when frame generation is active.
/// Handles unpause reset and calculates smoothed frame interval, bounded to healthy rates.
void OnPresentStart();

/// Called after presenting the interpolated frame to pace it at half the frame interval.
/// Clamped sleep (1.0ms - 16.667ms) preventing positive-feedback throttling loops.
void SleepInterpolated(bool presented);

/// Called immediately after a real frame is presented to mark the baseline timestamp.
void OnRealFramePresented();

} // namespace Extra::PresentPacer
