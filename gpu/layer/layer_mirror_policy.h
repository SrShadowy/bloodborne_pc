// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: when a mirror is worth making again over its valid blocks only (BufferCache::
// LayerCompactMirrors), and from which of its valid runs. Pure: no Vulkan, no guest, no
// environment, so the tests cover it without a device (tests/test_layer_mirror_policy.cpp).
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "common/types.h"

namespace VideoCore::LayerPolicy {

/// A run of blocks: [first, end), both in the layer's blocks.
struct Segment {
    u64 first = 0;
    u64 end = 0;

    bool operator==(const Segment&) const = default;
};

/// The waste (bytes without valid blocks) a mirror of `bytes` must have before it is made again:
/// a quarter of it, between 1 and 8 MiB. A mirror of 4 MiB therefore needs 1 MiB of waste, one of
/// 8 MiB needs 2 MiB, and from 32 MiB up it is 8 MiB.
///
/// The fixed 8 MiB of 0.7 was the size of a whole mirror on the run it was chosen for: 223-225
/// mirrors of 1,750 MiB (7.8 MiB each) held 612 MiB of waste between them (2.7 MiB each), so not
/// one of them ever reached it and nothing was compacted in the whole run (default run without any
/// BB_MIRROR_* variable, 2026-11-05).
inline u64 MirrorMinWaste(u64 bytes) {
    return std::clamp<u64>(bytes / 4, 1_MB, 8_MB);
}

/// A hole (bytes without valid blocks between two valid runs of a mirror) is only split out into
/// a mirror of its own when it is worth one: a thirty-second of the mirror, between 256 KiB and
/// 4 MiB. Below that the copy into the extra mirror costs more than the hole frees -- the run of
/// 2026-10-25 made 411 mirrors in 8 minutes for 0.4 GB of data that way.
inline u64 MirrorMinHole(u64 bytes) {
    return std::clamp<u64>(bytes / 32, 256_KB, 4_MB);
}

/// The most mirrors one compacted mirror becomes. The holes that have to be kept to stay under it
/// are the smallest ones: a mirror split into a hundred pieces costs more to bind than its waste.
constexpr u32 kMaxSegmentsPerMirror = 8;

/// The waste one extra mirror has to free to be worth making: below it the split costs (a copy of
/// the data and one more binding range) more than it gives back.
constexpr u64 kSegmentPays = 1_MB;

/// The segments a mirror is made again from, given its valid runs.
///
/// `runs` must be sorted and must not overlap. The heads and tails (the waste before the first run
/// and after the last) are always dropped: that costs no extra mirror, only the copy it already
/// makes. A hole between two runs is split out when it is at least `min_hole`, while the pieces
/// stay under `max_segments`, and only when the waste freed pays for the extra mirrors
/// (`kSegmentPays` each). So a mirror whose waste is scattered in small holes between its data
/// comes back as one segment and the caller sees no gain -- which is the right answer: it would
/// have been copies for nothing.
///
/// The smallest holes are the ones kept when a limit is reached.
inline std::vector<Segment> MirrorSegments(const std::vector<Segment>& runs, u64 shift,
                                           u64 min_hole, u32 max_segments) {
    std::vector<Segment> segments;
    if (runs.empty()) {
        return segments;
    }
    // The head and the tail are dropped for free: they come off without an extra mirror.
    const std::size_t holes = runs.size() - 1;
    const auto gap = [&](std::size_t i) {
        return (runs[i + 1].first - runs[i].end) << shift;
    };
    const u32 limit = std::max<u32>(max_segments, 1);
    // Holes worth a mirror of their own, smallest first when some have to go back.
    std::vector<std::size_t> candidates;
    for (std::size_t i = 0; i < holes; ++i) {
        if (gap(i) >= min_hole) {
            candidates.push_back(i);
        }
    }
    std::ranges::sort(candidates, {}, gap);
    // The biggest holes first, each as long as the waste it frees pays for the extra mirror it
    // costs (kSegmentPays each). The head and the tail are free: they come off without one.
    std::vector<bool> split(holes, false);
    u64 freed = 0;
    u32 pieces = 1;
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
        const u32 next = pieces + 1;
        if (next > limit || freed + gap(*it) < u64(next - 1) * kSegmentPays) {
            break; // sorted biggest first: nothing after this one pays either
        }
        split[*it] = true;
        freed += gap(*it);
        pieces = next;
    }
    Segment current{runs.front().first, runs.front().end};
    for (std::size_t i = 0; i < holes; ++i) {
        if (split[i]) {
            segments.push_back(current);
            current = Segment{runs[i + 1].first, runs[i + 1].end};
        } else {
            current.end = runs[i + 1].end;
        }
    }
    segments.push_back(current);
    return segments;
}

/// The bytes the segments hold, to compare with the mirror's size.
inline u64 SegmentsBytes(const std::vector<Segment>& segments, u64 shift) {
    u64 bytes = 0;
    for (const auto& segment : segments) {
        bytes += (segment.end - segment.first) << shift;
    }
    return bytes;
}

/// After a compaction that freed nothing, the next attempt waits this long: the mirrors it looked
/// at could not be made again (their waste all lies between valid blocks, or they are too young),
/// and rescanning them every second changes nothing while costing the whole scan.
constexpr u32 kRetrySeconds = 30;

/// The waste one compaction call takes, so the copies it costs are spread over the seconds that
/// follow instead of landing in one long frame. The pressure path passes its own need instead.
inline u64 CompactBudget(u64 waste) {
    return std::min<u64>(waste, 32_MB);
}

} // namespace VideoCore::LayerPolicy
