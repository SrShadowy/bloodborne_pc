// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// The texture collector's marks (gpu/textures/texture_cache_marks.h).
//
// The log of a GTX 1050 (1962 MiB, preset "apu" -> 2048 MiB) printed "memory pressure, 1351 of
// 1331 MiB (critical 1638)" while its driver reported a budget of 1378-1499 MiB: 1331 and 1638 are
// 65 % and 80 % of 2048 exactly, so the marks were the preset's nominal limit and not the memory
// the card had. VRAM reached 1946 of 1378 MiB, which on Windows means the driver paging it out.
// These tests pin the fix: the limit caps the driver's budget (never raises it), a card of 4 GB or
// less keeps the 75 %/85 % room on that budget, and all of it is recomputed once a second from it
// -- not on every collector call, which is what used to restore the nominal numbers.
#include "textures/texture_cache_marks.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace {

using VideoCore::TextureMarks::ForBudget;
using VideoCore::TextureMarks::ForLimit;
using VideoCore::TextureMarks::Marks;
using VideoCore::TextureMarks::WithRoomForAreaLoad;

/// The marks as the log prints them: whole MiB, truncated. The percentages do not land on MiB
/// boundaries (65 % of 2048 MiB is 1331.2, 80 % is 1638.4), so comparing byte counts to MiB
/// literals would test the rounding instead of the arithmetic.
u64 Mib(u64 bytes) {
    return bytes >> 20;
}

void TestMarksOfANominalLimit() {
    // The numbers the real log printed for a 2048 MiB limit: 65 % and 80 % of it.
    const auto marks = ForBudget(2048_MB);
    assert(Mib(marks.trigger) == 1024);
    assert(Mib(marks.pressure) == 1331);
    assert(Mib(marks.critical) == 1638);
    // Same arithmetic at another size.
    const auto small = ForBudget(1024_MB);
    assert(Mib(small.trigger) == 512);
    assert(Mib(small.pressure) == 665);
    assert(Mib(small.critical) == 819);
    // Ordered at any budget, which the collector's three passes rely on, and never above it.
    for (u64 mib : {u64{64}, u64{256}, u64{1024}, u64{1378}, u64{1962}, u64{2048}, u64{8192}}) {
        const auto m = ForBudget(mib << 20);
        assert(m.trigger <= m.pressure && m.pressure <= m.critical && m.critical <= (mib << 20));
    }
    const auto zero = ForBudget(0);
    assert(zero.trigger == 0 && zero.pressure == 0 && zero.critical == 0);
}

void TestTheLimitCapsTheCardBudget() {
    // No card budget known yet: the limit decides on its own.
    assert(Mib(ForLimit(2048_MB, 0, false).critical) == 1638);
    assert(Mib(ForLimit(2048_MB, 0, true).critical) == 1638);
    // The limit is under what the card has: it holds (the user asked for less).
    assert(Mib(ForLimit(2048_MB, 8_GB, false).critical) == 1638);
    assert(Mib(ForLimit(2048_MB, 8_GB, true).critical) == 1638);
    // The card has less than the limit: the marks follow the card, not the limit.
    assert(Mib(ForLimit(2048_MB, 1378_MB, false).critical) == 1102);
    // A card of 4 GB or less keeps the 75 %/85 % room on that same budget: the aggressive level
    // lands under it, where the log's 1638 was 260 MiB above the whole budget.
    assert(Mib(ForLimit(2048_MB, 1378_MB, true).critical) == 1171);
    assert(Mib(ForLimit(2048_MB, 1499_MB, true).critical) == 1274);
    // No limit at all: the card's budget is what there is.
    assert(Mib(ForLimit(0, 1962_MB, false).critical) == 1569);
    assert(Mib(ForLimit(0, 1962_MB, true).critical) == 1569);
    // Neither known: nothing to place the marks on.
    const auto none = ForLimit(0, 0, true);
    assert(none.trigger == 0 && none.pressure == 0 && none.critical == 0);
}

void TestTheBugTheLogShows() {
    // The log's card: 1962 MiB, "apu" preset, driver reported 1378 MiB at the worst moment. The
    // marks the old code kept were the limit's: critical 1638 MiB, 260 MiB ABOVE the card's whole
    // budget, so the aggressive level was unreachable and VRAM climbed to 1946 MiB.
    const u64 limit = 2048_MB;
    const u64 live = 1378_MB;
    const auto old_marks = ForBudget(limit);
    assert(Mib(old_marks.critical) == 1638);
    assert(Mib(old_marks.critical) - 1378 == 260);
    assert(1946_MB > old_marks.critical);

    // With the fix the aggressive level sits under the budget, 207 MiB below it: the collector
    // makes room before the driver starts paging instead of after.
    const auto fixed = ForLimit(limit, live, true);
    assert(Mib(fixed.critical) == 1171);
    assert(fixed.critical < live);
    assert(Mib(live) - Mib(fixed.critical) == 207);
    // The other reading in the log: 1499 MiB of budget -> critical 1274 MiB.
    assert(Mib(ForLimit(limit, 1499_MB, true).critical) == 1274);
    // Both levels move with it, and keep their order.
    assert(Mib(fixed.pressure) == 1033);
    assert(fixed.trigger <= fixed.pressure && fixed.pressure <= fixed.critical);
}

void TestFollowsTheBudgetBothWays() {
    // Another program took video memory: the marks come down.
    const auto low = ForLimit(2048_MB, 1378_MB, true);
    // It left again: they come back up, capped by the preset. They must not stay stuck at the low
    // budget's marks, or the collector would evict far more than needed.
    const auto back = ForLimit(2048_MB, 2000_MB, true);
    assert(Mib(back.critical) == 1638); // the preset's mark, under 85 % of 2000
    assert(back.critical > low.critical && back.pressure > low.pressure);
    // At its full budget, or above it, the preset's marks hold (the cap only lowers).
    assert(Mib(ForLimit(2048_MB, 1962_MB, true).critical) == 1638);
    assert(Mib(ForLimit(2048_MB, 2048_MB, false).critical) == 1638);
    assert(Mib(ForLimit(2048_MB, 4096_MB, false).critical) == 1638);

    // A large card follows the smaller budget without the small-card room: 80 % of 1378.
    const auto large_low = ForLimit(2048_MB, 1378_MB, false);
    assert(Mib(large_low.critical) == 1102);
    assert(large_low.critical < low.critical); // 85 % of the live budget is above 80 %

    // Idempotent: recomputing from the same numbers gives the same marks, so the once-a-second
    // refresh cannot ratchet them down towards zero.
    const auto once = ForLimit(2048_MB, 1378_MB, true);
    const auto twice = ForLimit(2048_MB, 1378_MB, true);
    assert(once.trigger == twice.trigger && once.pressure == twice.pressure &&
           once.critical == twice.critical);
}

void TestTheTriggerNeverPassesThePressureMark() {
    // The same card with the "medium" preset's 5500 MiB: the fixed budget is far above the whole
    // card, so a trigger at half of it (2750 MiB) would make the collector return on every call and
    // never reach a pass. It follows the card down to half of what it has.
    const u64 medium = 5500_MB;
    const u64 live = 1378_MB;
    const auto marks = ForLimit(medium, live, true);
    assert(Mib(marks.trigger) == 689);    // half of the live budget, not half of 5500
    assert(Mib(marks.pressure) == 1033);  // 75 % of the live budget
    assert(Mib(marks.critical) == 1171);  // 85 % of the live budget
    assert(marks.trigger <= marks.pressure && marks.pressure <= marks.critical);
    assert(marks.critical < live);
    // Without the small-card room the trigger is half of the smaller of the two, also ordered.
    const auto large = ForLimit(medium, live, false);
    assert(large.trigger == live / 2);
    assert(large.trigger <= large.pressure && large.pressure <= large.critical);
}

void TestNoBudgetOnEitherSide() {
    // A card with no preset limit and no budget report gets no marks from here (UpdateMemoryBudget's
    // fractions of the threshold decide), which is what the 0 return is.
    const auto none = ForLimit(0, 0, false);
    assert(none.critical == 0);
    // A budget with no preset (a test's BB_GC_BUDGET_MB): the budget is used directly.
    assert(Mib(ForLimit(0, 512_MB, false).critical) == 409);
    assert(Mib(ForLimit(0, 512_MB, true).critical) == 409);
}

void TestAreaHeadroomOnTheThresholdFractions() {
    // A card of 4 GB or less with the "high" preset has no fixed budget, so its marks come from the
    // fractions of the threshold; the floors in them (1.5 and 3 GB) are past such a card's whole
    // budget, which is how the 2 GB laptop sat at "critical 3072".
    const Marks fractions{.trigger = 0, .pressure = 3_GB, .critical = 5_GB};
    const auto marks = WithRoomForAreaLoad(fractions, 1962_MB);
    assert(Mib(marks.pressure) == 1471); // 75 % of 1962
    assert(Mib(marks.critical) == 1667); // 85 % of 1962
    assert(marks.critical < 1962_MB);
    // It only lowers: a card whose marks are already under the ceiling keeps them.
    const Marks low{.trigger = 0, .pressure = 512_MB, .critical = 700_MB};
    const auto kept = WithRoomForAreaLoad(low, 1962_MB);
    assert(kept.pressure == low.pressure && kept.critical == low.critical);
    // Idempotent, so the once-a-second refresh cannot ratchet the marks down towards zero.
    const auto again = WithRoomForAreaLoad(kept, 1962_MB);
    assert(again.pressure == kept.pressure && again.critical == kept.critical);
    // No budget from the driver: keep the marks rather than zero them.
    const auto unknown = WithRoomForAreaLoad(fractions, 0);
    assert(unknown.pressure == fractions.pressure && unknown.critical == fractions.critical);
    // It lifts when more memory is available again.
    const auto roomy = WithRoomForAreaLoad(fractions, 8_GB);
    assert(roomy.pressure == fractions.pressure && roomy.critical == fractions.critical);
}

} // namespace

int main() {
    TestMarksOfANominalLimit();
    TestTheLimitCapsTheCardBudget();
    TestTheBugTheLogShows();
    TestFollowsTheBudgetBothWays();
    TestTheTriggerNeverPassesThePressureMark();
    TestNoBudgetOnEitherSide();
    TestAreaHeadroomOnTheThresholdFractions();
    std::printf("texture-cache-marks-test: all checks passed\n");
    return 0;
}
