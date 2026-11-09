// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: where the texture collector's marks sit, as a function of the VRAM budget it may use
// (TextureCache::UpdateMemoryBudget, gpu/textures/texture_cache_memory.cpp). Pure: no Vulkan, no
// env, so the tests cover it without a device (tests/test_texture_cache_marks.cpp).
//
// The marks used to be computed from the preset's nominal limit alone (2048 MiB for "apu") while
// the driver reported a smaller budget as other programs took video memory, and a second copy of
// the computation ran on every collector call (hundreds a second), overwriting the once-a-second
// adjustment with the limit's numbers. A log of the GTX 1050 (1962 MiB, preset apu) shows the
// result: "memory pressure, 1351 of 1331 MiB (critical 1638)" and "VRAM 1946 of 1378 MiB in use" --
// 1331 = 65 % and 1638 = 80 % of 2048 exactly, with the critical mark 260 MiB ABOVE the budget the
// card actually had. The level that evicts images used a moment ago (and only then by submissions)
// could therefore only be reached after the driver had already begun paging VRAM out.
#pragma once

#include <algorithm>

#include "common/types.h"

namespace VideoCore::TextureMarks {

struct Marks {
    u64 trigger = 0;  ///< from here on the collector looks at the images at all
    u64 pressure = 0; ///< evicts what was unused for a few seconds
    u64 critical = 0; ///< evicts what was used a moment ago, then by submissions
};

/// Half, 65 % and 80 % of the budget. For 2048 MiB: 1024, 1331 and 1638 -- the numbers a real log
/// prints, which is how the nominal-limit bug was found.
[[nodiscard]] inline Marks ForBudget(u64 budget) {
    return Marks{.trigger = budget / 2, .pressure = budget * 65 / 100,
                 .critical = budget * 80 / 100};
}

/// The marks of a fixed budget on a card of 4 GB or less: pressure and critical at 75 % and 85 %
/// of the budget, not 65 % and 80 %. The rest is the room an area load needs on top (new mirrors
/// and staging go before the old ones do, 300-400 MiB at once), and at 65 % a 2 GB card evicted its
/// textures all the time (#204, GTX 1050).
[[nodiscard]] inline Marks ForSmallCard(u64 budget) {
    return Marks{.trigger = budget / 2, .pressure = budget * 75 / 100,
                 .critical = budget * 85 / 100};
}

/// The marks of a fixed budget (a preset's limit, BB_VRAM_LIMIT, or a test's BB_GC_BUDGET_MB) on a
/// card whose driver reports `live` bytes now; 0 means "not known".
///
/// The fixed budget is a cap, never a raise: the percentages are taken on the smaller of the two, so
/// a driver budget below the preset lowers the marks instead of leaving a level the card cannot
/// reach. For the log's card ("apu", 2048 MiB limit, driver at 1378 MiB) that is 689/895/1102 MiB
/// instead of 1024/1331/1638: the aggressive level sits 276 MiB under the card's budget rather than
/// 260 MiB above it. Without a limit the driver's budget is used as it comes (integrated GPUs).
///
/// `small_card` (4 GB or less) takes the 75 %/85 % room on the budget the card has now instead,
/// each mark the lower of that and the fixed budget's: 689/1033/1171 MiB for the log's card, which
/// is what the once-a-second adjustment produced before the per-call recompute put the limit's 1331
/// and 1638 back. They lift again when the memory comes back.
///
/// The trigger is half of the smaller of the two in every case: it only decides whether the
/// collector looks at the images at all, and half of a medium preset's 5500 MiB would sit above a
/// 2 GB card's whole budget, so the collector would return on every call and never reach a pass.
[[nodiscard]] inline Marks ForLimit(u64 limit, u64 live, bool small_card) {
    if (limit == 0) {
        return live != 0 ? ForBudget(live) : Marks{};
    }
    Marks marks = ForBudget(limit);
    if (live == 0) {
        return marks;
    }
    const u64 effective = std::min(limit, live);
    if (!small_card) {
        return ForBudget(effective);
    }
    const Marks room = ForSmallCard(live);
    marks.trigger = effective / 2;
    marks.pressure = std::min(marks.pressure, room.pressure);
    marks.critical = std::min(marks.critical, room.critical);
    return marks;
}

/// The same room applied to marks that did not come from a fixed budget: the fractions of the
/// threshold on a card of 4 GB or less (their floors, 1.5 and 3 GB, are past such a card's whole
/// budget, and VRAM went to 2 GB of 1.75 on the laptop while the driver paged). Idempotent, so the
/// once-a-second refresh can reapply it; the trigger is left alone, since it only decides whether
/// the collector looks at the images.
[[nodiscard]] inline Marks WithRoomForAreaLoad(Marks marks, u64 live) {
    if (live == 0) {
        return marks;
    }
    marks.pressure = std::min(marks.pressure, live * 75 / 100);
    marks.critical = std::min(marks.critical, live * 85 / 100);
    return marks;
}

} // namespace VideoCore::TextureMarks
