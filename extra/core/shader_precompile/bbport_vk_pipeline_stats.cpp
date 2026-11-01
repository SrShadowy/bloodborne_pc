// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: pipeline-creation metrics implementation.
#include "bbport_vk_pipeline_stats.h"

namespace BbVkPipelineStats {

namespace {

std::atomic<std::uint64_t> s_creates{0};
std::atomic<std::uint64_t> s_hits{0};
std::atomic<std::uint64_t> s_feedback_valid{0};
std::atomic<std::uint64_t> s_compile_ns{0};
std::atomic<std::uint64_t> s_longest_ns{0};

} // namespace

void RecordCreate(const vk::PipelineCreationFeedbackCreateInfoEXT& fb, std::uint64_t wall_ns) {
    s_creates.fetch_add(1, std::memory_order_relaxed);
    std::uint64_t longest = s_longest_ns.load(std::memory_order_relaxed);
    while (wall_ns > longest &&
           !s_longest_ns.compare_exchange_weak(longest, wall_ns, std::memory_order_relaxed)) {
    }
    // Promoted-layout headers: the driver's report sits behind a pointer, null when the attach
    // never armed — that (or a driver that filled nothing) is the wall-time fallback below.
    const vk::PipelineCreationFeedback* f = fb.pPipelineCreationFeedback;
    if (f && (f->flags & vk::PipelineCreationFeedbackFlagBitsEXT::eValid)) {
        // The driver's own number: the time it spent, without the guard's surrounding scope.
        s_feedback_valid.fetch_add(1, std::memory_order_relaxed);
        s_compile_ns.fetch_add(f->duration, std::memory_order_relaxed);
        if (f->flags & vk::PipelineCreationFeedbackFlagBitsEXT::eApplicationPipelineCacheHit) {
            s_hits.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        // Unarmed (BB_PIPELINE_STATS off) or the driver filled nothing: the wall time of the
        // guard's scope is the fallback — it brackets only the create call and its tail.
        s_compile_ns.fetch_add(wall_ns, std::memory_order_relaxed);
    }
}

Stats GetStats() {
    Stats st;
    st.creates = s_creates.load(std::memory_order_relaxed);
    st.hits = s_hits.load(std::memory_order_relaxed);
    st.feedback_valid = s_feedback_valid.load(std::memory_order_relaxed);
    st.compile_ns = s_compile_ns.load(std::memory_order_relaxed);
    st.longest_ns = s_longest_ns.load(std::memory_order_relaxed);
    return st;
}

} // namespace BbVkPipelineStats