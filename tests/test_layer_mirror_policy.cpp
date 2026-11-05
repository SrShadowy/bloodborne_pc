// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// When a mirror is made again over its valid blocks only (gpu/layer/layer_mirror_policy.h).
//
// The default run of 0.7 without any BB_MIRROR_* variable compacted nothing for its whole length:
// 223-225 mirrors of 1,750 MiB, 7.8 MiB each, with 612 MiB of waste between them (2.7 MiB each).
// The fixed 8 MiB a mirror had to waste was more than a whole mirror of that run, so no mirror was
// ever eligible. These tests pin the per-mirror share that replaced it, and the segments a mirror
// comes back as.
#include "layer/layer_mirror_policy.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

namespace {

using VideoCore::LayerPolicy::CompactBudget;
using VideoCore::LayerPolicy::kMaxSegmentsPerMirror;
using VideoCore::LayerPolicy::kRetrySeconds;
using VideoCore::LayerPolicy::kSegmentPays;
using VideoCore::LayerPolicy::MirrorMinHole;
using VideoCore::LayerPolicy::MirrorMinWaste;
using VideoCore::LayerPolicy::MirrorSegments;
using VideoCore::LayerPolicy::Segment;
using VideoCore::LayerPolicy::SegmentsBytes;

constexpr u64 kShift = 16;         // the layer's block: 64 KiB
constexpr u64 kBlocksPerMiB = 16;  // 64 KiB blocks in a MiB

void TestMinWasteFollowsSize() {
    // Never below 1 MiB: a copy costs what it frees otherwise.
    assert(MirrorMinWaste(0) == 1_MB);
    assert(MirrorMinWaste(1_MB) == 1_MB);
    assert(MirrorMinWaste(4_MB) == 1_MB);
    // A quarter of the mirror while that is small enough.
    assert(MirrorMinWaste(8_MB) == 2_MB);
    assert(MirrorMinWaste(16_MB) == 4_MB);
    // 31 MiB / 4 is 7.75 MiB, still under the 8 MiB cap.
    assert(MirrorMinWaste(31_MB) == 31_MB / 4);
    // And at most 8 MiB, the whole per-mirror threshold of 0.7.
    assert(MirrorMinWaste(32_MB) == 8_MB);
    assert(MirrorMinWaste(1_GB) == 8_MB);

    // The run that exposed it: 223 mirrors of 1,750 MiB hold 612 MiB of waste, 7.85 MiB and
    // 2.74 MiB a mirror. Under the fixed 8 MiB none qualified; under the share all of them do.
    const u64 mirror = 7'850'000; // 7.85 MiB
    const u64 waste = 2'870'000;  // 2.74 MiB
    assert(MirrorMinWaste(mirror) < waste);
    assert(waste < 8_MB); // what the old fixed threshold asked for
}

void TestMinHoleFollowsSize() {
    assert(MirrorMinHole(1_MB) == 256_KB);
    assert(MirrorMinHole(8_MB) == 256_KB);
    assert(MirrorMinHole(64_MB) == 2_MB);
    assert(MirrorMinHole(1_GB) == 4_MB);
}

void TestNoRuns() {
    assert(MirrorSegments({}, kShift, 1_MB, kMaxSegmentsPerMirror).empty());
}

void TestOneRunStaysOneSegment() {
    const std::vector<Segment> runs{{0, 100}};
    const auto segments = MirrorSegments(runs, kShift, 1_MB, kMaxSegmentsPerMirror);
    assert(segments.size() == 1);
    assert(segments[0].first == 0 && segments[0].end == 100);
}

void TestHeadAndTailComeOffForFree() {
    // Valid [16, 64) in a mirror of 128 blocks (8 MiB): a head of 1 MiB and a tail of 4 MiB. Both
    // come off without an extra mirror, so all 5 MiB come back whatever the per-hole minimum says.
    const std::vector<Segment> runs{{16, 64}};
    for (const u64 min_hole : {u64{256_KB}, u64{2_MB}, u64{4_MB}}) {
        const auto segments = MirrorSegments(runs, kShift, min_hole, kMaxSegmentsPerMirror);
        assert(segments.size() == 1);
        assert(segments[0].first == 16 && segments[0].end == 64);
        assert((128 << kShift) - SegmentsBytes(segments, kShift) == 5_MB);
    }
}

void TestHoleIsSplitOnlyWhenItPays() {
    // An 8 MiB mirror with two 64-block runs and a hole between them.
    const std::vector<Segment> two{{0, 64}, {72, 128}}; // the hole is 8 blocks = 512 KiB
    // The hole is under the extra mirror's worth (kSegmentPays): kept, so one segment and the
    // caller frees nothing beyond the ends.
    const auto kept = MirrorSegments(two, kShift, 256_KB, kMaxSegmentsPerMirror);
    assert(kept.size() == 1);
    assert(kept[0].first == 0 && kept[0].end == 128);
    assert(SegmentsBytes(kept, kShift) == 128 << kShift);

    // A 2 MiB hole does pay for the mirror it costs: split in two.
    const std::vector<Segment> wide{{0, 64}, {96, 160}}; // the hole is 32 blocks = 2 MiB
    const auto split = MirrorSegments(wide, kShift, 256_KB, kMaxSegmentsPerMirror);
    assert(split.size() == 2);
    assert(split[0].first == 0 && split[0].end == 64);
    assert(split[1].first == 96 && split[1].end == 160);
    assert(SegmentsBytes(split, kShift) == (128 << kShift));
    assert((160 << kShift) - SegmentsBytes(split, kShift) == 2_MB);
}

void TestWasteBetweenValidBlocksFreesNothing() {
    // Many small holes: the mirror comes back whole, so the caller skips it. This is the case the
    // old `continue` decided for every mirror.
    std::vector<Segment> runs;
    u64 at = 0;
    for (u32 i = 0; i < 40; ++i) {
        runs.push_back({at, at + 16});
        at += 16 + 1; // a 64 KiB hole after each 1 MiB run
    }
    const u64 size = runs.back().end << kShift;
    const auto segments = MirrorSegments(runs, kShift, MirrorMinHole(size), kMaxSegmentsPerMirror);
    assert(SegmentsBytes(segments, kShift) == size);
}

void TestSegmentCapKeepsTheSmallestHoles() {
    // Twenty runs of 64 blocks with holes of 1..19 MiB between them.
    std::vector<Segment> runs;
    std::vector<u64> holes;
    u64 at = 0;
    for (u32 i = 1; i <= 20; ++i) {
        runs.push_back({at, at + 64});
        holes.push_back(u64(i) * kBlocksPerMiB);
        at += 64 + u64(i) * kBlocksPerMiB;
    }
    holes.pop_back(); // 19 holes between 20 runs

    const auto segments = MirrorSegments(runs, kShift, 256_KB, kMaxSegmentsPerMirror);
    assert(segments.size() == kMaxSegmentsPerMirror);
    // Nothing is lost: every valid block is inside one of the new mirrors, and they do not
    // overlap.
    std::size_t next = 0;
    for (const auto& segment : segments) {
        assert(segment.first < segment.end);
        while (next < runs.size() && runs[next].first >= segment.first &&
               runs[next].end <= segment.end) {
            ++next;
        }
    }
    assert(next == runs.size());
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        assert(segments[i].end <= segments[i + 1].first);
    }
    assert(segments.front().first == runs.front().first);
    assert(segments.back().end == runs.back().end);

    // Each split is a hole, and the holes split are the biggest ones: with 8 pieces at most, 7
    // splits remain and they go to the 7 biggest holes (13..19 MiB).
    std::vector<u64> split_holes;
    for (std::size_t i = 0; i + 1 < segments.size(); ++i) {
        split_holes.push_back(segments[i + 1].first - segments[i].end);
    }
    const std::vector<u64> expected(holes.end() - 7, holes.end());
    assert(split_holes == expected);
}

void TestRunsAreNotSplitForNothing() {
    // The guard against the opposite failure (0.7 made 411 mirrors in 8 minutes for 0.4 GB of data
    // on the 2 GB laptop): holes of a handful of blocks never pay for the mirrors they would add,
    // so the runs stay in one segment.
    const std::vector<Segment> runs{{0, 1}, {2, 3}, {4, 6}};
    for (const u32 limit : {kMaxSegmentsPerMirror, 1u, 0u}) {
        const auto segments = MirrorSegments(runs, kShift, 0, limit);
        assert(segments.size() == 1);
        assert(segments[0].first == 0 && segments[0].end == 6);
        // Nothing freed between the valid blocks: only the (absent) head and tail come off.
        assert(SegmentsBytes(segments, kShift) == (6 << kShift));
    }
}

void TestRetryAndBudget() {
    assert(kRetrySeconds == 30);
    // The per-split minimum the two tests above rely on.
    assert(kSegmentPays == 1_MB);
    // A small waste is taken whole; a large one is capped so the copies spread over the seconds
    // that follow instead of landing in one long frame.
    assert(CompactBudget(1_MB) == 1_MB);
    assert(CompactBudget(32_MB) == 32_MB);
    assert(CompactBudget(612_MB) == 32_MB);
    assert(CompactBudget(0) == 0);
}

} // namespace

int main() {
    TestMinWasteFollowsSize();
    TestMinHoleFollowsSize();
    TestNoRuns();
    TestOneRunStaysOneSegment();
    TestHeadAndTailComeOffForFree();
    TestHoleIsSplitOnlyWhenItPays();
    TestWasteBetweenValidBlocksFreesNothing();
    TestSegmentCapKeepsTheSmallestHoles();
    TestRunsAreNotSplitForNothing();
    TestRetryAndBudget();
    std::printf("layer-mirror-policy-test: all checks passed\n");
    return 0;
}
