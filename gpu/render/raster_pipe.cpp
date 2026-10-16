// SPDX-License-Identifier: GPL-2.0-or-later
// bbport render: Rasterizer, the two-stage draw pipeline: stage A (GPU command thread) packs each draw
// (dirty registers, user data, constants) and stage B (draw recording thread) records it.

#include "render/raster_internal.h"

namespace Vulkan {

// bbport: two-stage draw pipeline (vk_draw_pipe.h). Packet layout, 8-byte aligned parts:
// DrawPacket, u16 block indices, u32 block words (RegDirty::BlockWords each), then per stage a
// PacketStage with its user data and flattened user data, then (verification) the full file.
namespace {
enum class PacketKind : u32 { Draw, Task, Dispatch, Special, Indirect, IndirectDispatch };
struct TaskPacket {
    PacketKind kind;
    u32 size;
    Rasterizer::OrderedTask task;
};
struct DrawPacket {
    PacketKind kind;
    const Pipeline* pipeline;
    const PreparedDraw* prepared;
    u32 index_offset;
    u8 is_indexed;
    u8 regs_reset;
    u16 num_blocks;
    u32 num_stages;
    u32 verify;
    std::array<Gcn::CbDbExtent, Gcn::NUM_COLOR_BUFFERS> cb_extent;
    Gcn::CbDbExtent db_extent;
    u32 num_vsharps; ///< V#s of the vertex streams, after the indirect arguments
    u32 pad;
    u64 ring_end; ///< constant ring position after this packet's copies
};
struct PacketStage {
    const Shader::Info* info;
    VAddr pgm_base;
    u32 ud_size;
    u32 flat_size;
    u32 num_ring;
    u32 pad;
};
constexpr u32 AlignPacket(u32 size) {
    return (size + 7) & ~7u;
}
u32 VerifyInterval() {
    static const u32 interval = [] {
        const char* env = std::getenv("BB_PIPE_VERIFY");
        return env ? static_cast<u32>(std::strtoul(env, nullptr, 10)) : 0u;
    }();
    return interval;
}
} // namespace

bool Rasterizer::DrawPipeWanted() {
    const char* env = std::getenv("BB_DRAW_PIPE");
    if (env && env[0]) {
        return env[0] == '1';
    }
    // Stage B spins while draws flow. Measured ahead with 16 threads (+19%) and with 4 cores /
    // 8 threads (taskset, Steam Deck-like: +18%).
    return BbThreads::Available() >= 8;
}

bool Rasterizer::UseDrawPipe() const {
    return draw_pipe && !host_markers_enabled && !BbToggle::Disabled(BbToggle::DrawPipeline);
}

bool Rasterizer::OnStageA() const {
    return std::this_thread::get_id() == liverpool->GetGpuCommandProcessorThread();
}

namespace {
/// Stage A: cycles waited per drain site (BB_FRAME_STATS), keyed by function name and line.
std::unordered_map<const char*, std::unordered_map<u32, u64>> drain_sites;
} // namespace

void Rasterizer::DrainDrawPipe(u32 reason, u32 line, const char* function) {
    if (!draw_pipe || !OnStageA()) {
        return;
    }
    static const bool stats = std::getenv("BB_FRAME_STATS") != nullptr;
    if (!stats) {
        draw_pipe->Drain(reason);
        return;
    }
    const u64 before = draw_pipe->drain_cycles;
    draw_pipe->Drain(reason);
    if (const u64 waited = draw_pipe->drain_cycles - before) {
        drain_sites[function][line] += waited;
    }
}

bool Rasterizer::IsGpuSideThread() const {
    return OnStageA() || DrawPipe::OnStageB();
}

bool Rasterizer::IsGpuSideThreadId(u32 tid) const {
    // The userfaultfd thread handles the fault while the faulting thread waits: without locks
    // only if that thread owns the caches. The GPU command thread does not while the draw
    // recording thread may be running, so its faults take the locked path of guest threads.
    if (draw_pipe) {
        return tid == draw_pipe->StageBThreadId();
    }
    return tid == liverpool->GetGpuCommandProcessorThreadId();
}

void Rasterizer::NotePendingGpuWrite(VAddr address, u64 size) {
    if (!draw_pipe || !size) {
        return;
    }
    // Numbered by packet (a position, the packet's start, counted as reached once the packet
    // before it had run, while this one was not recorded yet), in hashed granules: a lookup per
    // constant binding instead of a scan of the writes (a list scan was ~5% of this thread).
    pending_write_table.Note(address, size, draw_pipe->NextPacket());
}

void Rasterizer::NotePendingRead(VAddr address, u64 size) {
    if (draw_pipe && size && OnStageA()) {
        pending_reads.Note(address, size, draw_pipe->NextPacket());
    }
}

void Rasterizer::WaitForPendingReads(VAddr address, u64 size) {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_CONSTRAM_PIPE");
        return !env || env[0] != '0';
    }();
    if (!draw_pipe || !OnStageA()) {
        return;
    }
    if (!enabled || !UseDrawPipe() || size == 0) {
        DrainDrawPipe(DrawPipe::ReasonConstRam);
        return;
    }
    const u64 packet = std::max(untracked_reads_packet, pending_reads.Newest(address, size));
    if (draw_pipe->ReachedPacket(packet)) {
        ++constram_waits_skipped;
        return;
    }
    draw_pipe->WaitForPacket(packet, DrawPipe::ReasonConstRam);
}

void Rasterizer::NoteCommandWriteInOrder(VAddr addr, const void* data, u64 size) {
    static const bool enabled = [] {
        const char* env = std::getenv("BB_CONSTRAM_PIPE");
        return !env || env[0] != '0';
    }();
    if (VideoCore::WriteTracking() || !IsMapped(addr, size)) {
        return;
    }
    if (!enabled || !UseDrawPipe() || !OnStageA() || size > 1_MB) {
        NoteCommandWrite(addr, data, size, false);
        return;
    }
    // The caches belong to the recording thread while it has packets: it updates them in order
    // (later packets see the new bytes there; the memory already holds them for this thread).
    struct Header {
        VAddr addr;
        u64 size;
    };
    thread_local std::vector<u8> payload;
    payload.resize(sizeof(Header) + size);
    const Header header{addr, size};
    std::memcpy(payload.data(), &header, sizeof(header));
    std::memcpy(payload.data() + sizeof(header), data, size);
    RunInOrder(
        [](Rasterizer& self, const u8* bytes) {
            Header write;
            std::memcpy(&write, bytes, sizeof(write));
            self.NoteCommandWrite(write.addr, bytes + sizeof(write), write.size, true);
        },
        payload.data(), static_cast<u32>(payload.size()), BbToggle::PipelinedTasks, false);
}

bool Rasterizer::PendingWriteOverlaps(VAddr address, u64 size) {
    if (!draw_pipe || size == 0) {
        return false;
    }
    const u64 packet = pending_write_table.Newest(address, size);
    return packet != 0 && !draw_pipe->ReachedPacket(packet);
}

void Rasterizer::CollectRingBindings(const Shader::Info& stage, const PreparedDraw* prepared,
                           boost::container::static_vector<RingBinding, Shader::NUM_BUFFERS>& out) {
    BB_SECTION(ARing);
    const PreparedStage* prepared_stage = nullptr;
    if (prepared && !BbToggle::Disabled(BbToggle::PreparedResources)) {
        for (u32 i = 0; i < prepared->num_stages; ++i) {
            const auto& candidate = prepared->stages[i];
            if (&candidate.program->info == &stage &&
                candidate.num_buffers == stage.buffers.size()) {
                prepared_stage = &candidate;
                break;
            }
        }
    }
    const u64 alignment = std::max<u64>(instance.StorageMinAlignment(),
                                        instance.UniformMinAlignment());
    const auto copy = [&](u32 index, const void* source, VAddr address, u64 size) {
        const auto offset = constant_ring->Allocate(size, alignment);
        if (!offset) {
            return false;
        }
        BB_SECTION(ARingCopy);
        if (BbSections::Enabled()) {
            BbSections::ring_copies.fetch_add(1, std::memory_order_relaxed);
            BbSections::ring_bytes.fetch_add(size, std::memory_order_relaxed);
        }
        u8* dst = constant_ring->Data(*offset);
        if (source) {
            std::memcpy(dst, source, size);
        } else {
            memory->CopySparseMemory(address, dst, size);
        }
        constant_ring->Flush(*offset, size);
        out.push_back({index, static_cast<u32>(size), *offset, address});
        return true;
    };
    // Compute stages may run as HLE copy shaders, which read their buffers on the recording
    // thread whether or not they were copied here.
    const bool hle_candidate = stage.sw_stage == Shader::SwStage::Compute;
    // BB_CONSTANTS_IN_PLACE: nothing is copied here; the recording thread binds the game's memory
    // (or copies what is not in place, a read noted below).
    const bool constants_in_place = VideoCore::ConstantsInPlace();
    for (u32 index = 0; index < stage.buffers.size(); ++index) {
        const auto& desc = stage.buffers[index];
        if (desc.IsSpecial()) {
            if (desc.buffer_type == Shader::BufferType::Flatbuf &&
                !stage.flattened_ud_buf.empty()) {
                copy(index, stage.flattened_ud_buf.data(), 0,
                     stage.flattened_ud_buf.size() * sizeof(u32));
            }
            continue;
        }
        const auto vsharp = prepared_stage ? prepared_stage->buffer_sharps[index]
                                           : desc.GetSharp(stage);
        const VAddr address = vsharp.base_address;
        if (address == 0 || vsharp.GetSize() == 0) {
            continue;
        }
        const u64 size = memory->ClampRangeSize(address, vsharp.GetSize());
        if (desc.is_written) {
            NotePendingGpuWrite(address, size);
            continue;
        }
        if (BbCeStats::Enabled()) {
            BbCeStats::Check(size <= VideoCore::BufferCache::STREAM_THRESHOLD
                                 ? BbCeStats::RingConstants
                                 : BbCeStats::LargeBuffer,
                             address, size);
        }
        // The stream path of BufferCache::ObtainBuffer, taken here: small, read-only, not
        // written by the GPU (now or by work still queued for the recording thread). What is not
        // copied is read on the recording thread: a CPU write over these bytes on this thread
        // waits for the packet (WaitForPendingReads).
        if (BbSections::Enabled()) {
            BbSections::ring_buffers.fetch_add(1, std::memory_order_relaxed);
        }
        bool copyable;
        {
            BB_SECTION(ARingChecks);
            copyable = size != 0 && size <= VideoCore::BufferCache::STREAM_THRESHOLD &&
                       !constants_in_place && !buffer_cache.IsRegionGpuModified(address, size) &&
                       !PendingWriteOverlaps(address, size) &&
                       !VideoCore::BufferCache::LayerMirrored(address, size);
        }
        if (!copyable || !copy(index, nullptr, address, size) || hle_candidate) {
            NotePendingRead(address, size);
        }
    }
}

void Rasterizer::PostDraw(const Pipeline* pipeline, const PreparedDraw* used_prepared,
                          bool is_indexed, u32 index_offset, const Gcn::ComputeProgram* cs,
                          const IndirectDraw* indirect) {
    BB_SECTION(APostDraw);
    auto& dirty = liverpool->pipe_dirty;
    if (!pipe_synced) {
        // Stage B has not run yet: its register copy starts as this one.
        pipe_regs = liverpool->regs;
        dirty.Clear();
        pipe_synced = true;
    }
    std::array<u16, Gcn::RegDirty::NumBlocks> blocks;
    u32 num_blocks = 0;
    dirty.ForEachBlock([&](size_t block) { blocks[num_blocks++] = static_cast<u16>(block); });
    const auto stages =
        pipeline ? pipeline->GetStages() : std::span<const Shader::Info* const>{};
    // Constants: copied here into the ring, the recording thread only binds them.
    thread_local std::array<boost::container::static_vector<RingBinding, Shader::NUM_BUFFERS>,
                            Shader::MaxStageTypes>
        rings;
    u32 num_stages = 0;
    u32 size = sizeof(DrawPacket) + AlignPacket(num_blocks * sizeof(u16)) +
               num_blocks * Gcn::RegDirty::BlockWords * sizeof(u32);
    for (const auto* stage : stages) {
        if (stage) {
            auto& ring = rings[num_stages];
            if (BbCeStats::Enabled()) {
                const auto ud = stage->UserData();
                for (size_t i = 0; i + 1 < ud.size(); ++i) {
                    const u64 value = u64(ud[i]) | u64(ud[i + 1]) << 32;
                    if (value >= 0x100000000ull && value < (1ull << 40)) {
                        BbCeStats::Check(BbCeStats::UserDataPointer, value, 64);
                    }
                }
                BbCeStats::Get().checks[BbCeStats::DmaStage]++;
                BbCeStats::Get().hits[0][BbCeStats::DmaStage] += stage->uses_dma ? 1 : 0;
            }
            // bbport BB_WATCH=addr,size (diagnostics): what the translator reads from that range on
            // the CPU: user data pointing into it, constants copied from it; printed every 2 s.
            static const std::pair<u64, u64> watch = [] {
                const char* env = std::getenv("BB_WATCH");
                if (!env) {
                    return std::pair<u64, u64>{0, 0};
                }
                char* end = nullptr;
                const u64 addr = std::strtoull(env, &end, 0);
                const u64 len = end && *end == ',' ? std::strtoull(end + 1, nullptr, 0) : 0;
                return std::pair<u64, u64>{addr, len};
            }();
            if (watch.second) {
                static u64 ud_hits = 0, ud_checks = 0, ring_hits = 0, vs_hits = 0;
                static u64 code_hits = 0;
                if (stage->ProgramBase() >= watch.first && stage->ProgramBase() < watch.first + watch.second) {
                    ++code_hits;
                }
                static u64 last_hash = 0;
                static auto report = std::chrono::steady_clock::now();
                const auto ud = stage->UserData();
                ++ud_checks;
                for (size_t i = 0; i + 1 < ud.size(); ++i) {
                    const u64 value = (u64(ud[i]) | u64(ud[i + 1]) << 32) & 0xFFFFFFFFFFFFull;
                    if (value >= watch.first && value < watch.first + watch.second) {
                        ++ud_hits;
                        last_hash = stage->pgm_hash;
                    }
                }
                for (u32 index = 0; index < stage->buffers.size(); ++index) {
                    const auto& desc = stage->buffers[index];
                    if (desc.IsSpecial()) {
                        continue;
                    }
                    const auto vsharp = desc.GetSharp(*stage);
                    if (vsharp.base_address < watch.first + watch.second &&
                        watch.first < vsharp.base_address + vsharp.GetSize()) {
                        ++(vsharp.GetSize() <= VideoCore::BufferCache::STREAM_THRESHOLD ? ring_hits
                                                                                      : vs_hits);
                        last_hash = stage->pgm_hash;
                    }
                }
                if (std::chrono::steady_clock::now() - report > std::chrono::seconds(2)) {
                    report = std::chrono::steady_clock::now();
                    std::printf("Watch %#llx+%#llx: user data pointers into it %llu of %llu stages, "
                                "small buffers over it %llu, large %llu, shader code in it %llu (last shader %016llx)\n",
                                (unsigned long long)watch.first, (unsigned long long)watch.second,
                                (unsigned long long)ud_hits, (unsigned long long)ud_checks,
                                (unsigned long long)ring_hits, (unsigned long long)vs_hits,
                                (unsigned long long)code_hits,
                                (unsigned long long)last_hash);
                    ud_hits = ud_checks = ring_hits = vs_hits = code_hits = 0;
                }
            }
            ring.clear();
            if (constant_ring && !BbToggle::Disabled(BbToggle::ConstantRing)) {
                CollectRingBindings(*stage, used_prepared, ring);
            } else {
                untracked_reads_packet = draw_pipe->NextPacket(); // bindings not listed
            }
            ++num_stages;
            size += sizeof(PacketStage) +
                    AlignPacket(u32(stage->user_data.size() + stage->flattened_ud_buf.size()) *
                                sizeof(u32)) +
                    u32(ring.size() * sizeof(RingBinding));
        }
    }
    if (cs) {
        size += AlignPacket(sizeof(Gcn::ComputeProgram));
    }
    if (indirect) {
        size += AlignPacket(sizeof(IndirectDraw));
        // The arguments are read on the recording thread.
        const u64 args_size = u64(std::max(indirect->stride, 4u)) * std::max(indirect->max_count, 1u);
        if (args_size <= 16_MB) {
            NotePendingRead(indirect->args, args_size);
        } else {
            untracked_reads_packet = draw_pipe->NextPacket();
        }
        if (indirect->count) {
            NotePendingRead(indirect->count, sizeof(u32));
        }
    }
    // bbport: the V#s of the vertex streams, read here from the tables the user data points to:
    // those tables are what the game's constant engine dumps (a ring of ~25 pages rewritten
    // ~14000 times a second). Read on the recording thread instead, a later dump had often
    // replaced them by then: depth of field drew its passes with another draw's vertices (a
    // flipped copy of the scene, streaks and blocks in the sky; issue #44). The streams' data and
    // the index buffer are still read there: a DumpConstRam over them waits for this packet.
    thread_local boost::container::static_vector<Gcn::Buffer, Shader::NUM_BUFFERS> vsharps;
    vsharps.clear();
    if (pipeline && !pipeline->IsCompute()) {
        const auto* graphics = static_cast<const GraphicsPipeline*>(pipeline);
        if (const auto& fetch = graphics->GetFetchShader();
            fetch && fetch->attributes.size() <= vsharps.capacity()) {
            const auto& vs = graphics->GetStage(Shader::SwStage::Vertex);
            for (const auto& attrib : fetch->attributes) {
                const auto buffer = attrib.GetSharp(vs);
                vsharps.push_back(buffer);
                if (buffer.base_address != 0 && buffer.GetSize() > 0) {
                    if (BbCeStats::Enabled()) {
                        BbCeStats::Check(BbCeStats::Vertex, buffer.base_address, buffer.GetSize());
                    }
                    NotePendingRead(buffer.base_address, buffer.GetSize());
                }
            }
        }
        if (is_indexed) {
            const auto& regs = liverpool->regs;
            const u32 index_size =
                regs.index_buffer_type.index_type == Gcn::IndexType::Index16 ? 2 : 4;
            const VAddr index_address =
                regs.index_base_address.Address<VAddr>() + u64(index_offset) * index_size;
            const u64 index_bytes = u64(regs.num_indices) * index_size;
            if (BbCeStats::Enabled()) {
                BbCeStats::Check(BbCeStats::Index, index_address, index_bytes);
            }
            NotePendingRead(index_address, index_bytes);
        }
    }
    size += AlignPacket(u32(vsharps.size() * sizeof(Gcn::Buffer)));
    const u32 interval = VerifyInterval();
    const bool verify = interval && (draw_pipe->packets % interval) == 0;
    if (verify) {
        size += sizeof(Gcn::Regs);
    }

    u8* out = draw_pipe->Begin(size);
    u8* const start = out;
    auto& packet = *reinterpret_cast<DrawPacket*>(out);
    packet.kind = cs         ? (indirect ? PacketKind::IndirectDispatch : PacketKind::Dispatch)
                  : indirect ? PacketKind::Indirect
                  : pipeline ? PacketKind::Draw
                             : PacketKind::Special;
    packet.pipeline = pipeline;
    packet.prepared = used_prepared;
    packet.index_offset = index_offset;
    packet.is_indexed = is_indexed;
    packet.regs_reset = dirty.reset;
    packet.num_blocks = static_cast<u16>(num_blocks);
    packet.num_stages = num_stages;
    packet.verify = verify;
    packet.cb_extent = liverpool->last_cb_extent;
    packet.db_extent = liverpool->last_db_extent;
    packet.ring_end = constant_ring ? constant_ring->Position() : 0;
    packet.num_vsharps = static_cast<u32>(vsharps.size());
    out += sizeof(DrawPacket);
    std::memcpy(out, blocks.data(), num_blocks * sizeof(u16));
    out += AlignPacket(num_blocks * sizeof(u16));
    for (u32 i = 0; i < num_blocks; ++i) {
        std::memcpy(out,
                    liverpool->regs.reg_array.data() + blocks[i] * Gcn::RegDirty::BlockWords,
                    Gcn::RegDirty::BlockWords * sizeof(u32));
        out += Gcn::RegDirty::BlockWords * sizeof(u32);
    }
    u32 stage_index = 0;
    for (const auto* stage : stages) {
        if (!stage) {
            continue;
        }
        const auto& ring = rings[stage_index++];
        auto& header = *reinterpret_cast<PacketStage*>(out);
        header = {stage,
                  stage->pgm_base,
                  static_cast<u32>(stage->user_data.size()),
                  static_cast<u32>(stage->flattened_ud_buf.size()),
                  static_cast<u32>(ring.size()),
                  0};
        out += sizeof(PacketStage);
        std::memcpy(out, ring.data(), ring.size() * sizeof(RingBinding));
        out += ring.size() * sizeof(RingBinding);
        std::memcpy(out, stage->user_data.data(), stage->user_data.size_bytes());
        std::memcpy(out + stage->user_data.size_bytes(), stage->flattened_ud_buf.data(),
                    stage->flattened_ud_buf.size() * sizeof(u32));
        out += AlignPacket(u32(stage->user_data.size() + stage->flattened_ud_buf.size()) *
                           sizeof(u32));
    }
    if (cs) {
        std::memcpy(out, cs, sizeof(Gcn::ComputeProgram));
        out += AlignPacket(sizeof(Gcn::ComputeProgram));
    }
    if (indirect) {
        std::memcpy(out, indirect, sizeof(IndirectDraw));
        out += AlignPacket(sizeof(IndirectDraw));
    }
    std::memcpy(out, vsharps.data(), vsharps.size() * sizeof(Gcn::Buffer));
    out += AlignPacket(u32(vsharps.size() * sizeof(Gcn::Buffer)));
    if (verify) {
        std::memcpy(out, &liverpool->regs, sizeof(Gcn::Regs));
        out += sizeof(Gcn::Regs);
    }
    dirty.Clear();
    draw_pipe->Commit(static_cast<u32>(out - start));
    PrintPipeStats();
}

void Rasterizer::RetireSubmission() {
    if (!draw_pipe || !OnStageA()) {
        return;
    }
    while (!pipe_keepalive.empty() && draw_pipe->Reached(pipe_keepalive.front().first)) {
        pipe_keepalive.pop_front();
    }
    if (!draw_pipe->Idle()) {
        pipe_keepalive.emplace_back(draw_pipe->Head(), draw_prep->KeepCurrent());
    }
}

bool Rasterizer::RunInOrder(OrderedTask task, const void* data, u32 size, u64 toggle,
                            bool reads_guest_memory) {
    if (!UseDrawPipe() || BbToggle::Disabled(toggle)) {
        DrainDrawPipe(DrawPipe::ReasonRasterizer);
        task(*this, static_cast<const u8*>(data));
        return false;
    }
    if (reads_guest_memory) {
        untracked_reads_packet = draw_pipe->NextPacket();
    }
    const u32 total = sizeof(TaskPacket) + AlignPacket(size);
    u8* out = draw_pipe->Begin(total);
    *reinterpret_cast<TaskPacket*>(out) = {PacketKind::Task, size, task};
    std::memcpy(out + sizeof(TaskPacket), data, size);
    draw_pipe->Commit(total);
    return true;
}

namespace {
// BB_SECTIONS: ordered tasks by function (cycles, count), stage B only.
constexpr std::size_t TaskKinds = 16;
std::array<void*, TaskKinds> task_kinds{};
std::array<u64, TaskKinds> task_cycles{}, task_counts{};
} // namespace

void Rasterizer::RunDrawPacket(void* context, const u8* data, u32 size) {
    BB_SECTION(Packet);
    auto& self = *static_cast<Rasterizer*>(context);
    if (*reinterpret_cast<const PacketKind*>(data) == PacketKind::Task) {
        BB_SECTION(Task);
        self.buffer_cache.NewPacket();
        const auto& task = *reinterpret_cast<const TaskPacket*>(data);
        if (BbSections::Enabled()) {
            const u64 t0 = __rdtsc();
            task.task(self, data + sizeof(TaskPacket));
            void* fn = reinterpret_cast<void*>(task.task);
            for (std::size_t k = 0; k < TaskKinds; ++k) {
                if (task_kinds[k] == fn || !task_kinds[k]) {
                    task_kinds[k] = fn;
                    task_cycles[k] += __rdtsc() - t0;
                    ++task_counts[k];
                    break;
                }
            }
            return;
        }
        task.task(self, data + sizeof(TaskPacket));
        return;
    }
    const auto& packet = *reinterpret_cast<const DrawPacket*>(data);
    const u8* in = data + sizeof(DrawPacket);
    const auto* blocks = reinterpret_cast<const u16*>(in);
    in += AlignPacket(packet.num_blocks * sizeof(u16));
    auto& regs = self.pipe_regs;
    if (packet.regs_reset) {
        regs.SetDefaults();
    }
    for (u32 i = 0; i < packet.num_blocks; ++i) {
        std::memcpy(regs.reg_array.data() + blocks[i] * Gcn::RegDirty::BlockWords, in,
                    Gcn::RegDirty::BlockWords * sizeof(u32));
        in += Gcn::RegDirty::BlockWords * sizeof(u32);
    }
    ASSERT(packet.num_stages <= Shader::Info::MaxUdSnapshots);
    for (u32 i = 0; i < packet.num_stages; ++i) {
        const auto& stage = *reinterpret_cast<const PacketStage*>(in);
        in += sizeof(PacketStage);
        self.ring_stages[i] = {stage.info, reinterpret_cast<const RingBinding*>(in),
                               stage.num_ring};
        in += stage.num_ring * sizeof(RingBinding);
        const auto* words = reinterpret_cast<const u32*>(in);
        Shader::Info::ud_snapshots[i] = {stage.info, words, stage.ud_size, words + stage.ud_size,
                                         stage.flat_size, stage.pgm_base};
        in += AlignPacket((stage.ud_size + stage.flat_size) * sizeof(u32));
    }
    Shader::Info::num_ud_snapshots = packet.num_stages;
    self.num_ring_stages = packet.num_stages;
    const Gcn::ComputeProgram* cs = nullptr;
    if (packet.kind == PacketKind::Dispatch || packet.kind == PacketKind::IndirectDispatch) {
        cs = reinterpret_cast<const Gcn::ComputeProgram*>(in);
        in += AlignPacket(sizeof(Gcn::ComputeProgram));
    }
    const IndirectDraw* indirect = nullptr;
    if (packet.kind == PacketKind::Indirect || packet.kind == PacketKind::IndirectDispatch) {
        indirect = reinterpret_cast<const IndirectDraw*>(in);
        in += AlignPacket(sizeof(IndirectDraw));
    }
    self.packet_vsharps = {reinterpret_cast<const Gcn::Buffer*>(in), packet.num_vsharps};
    in += AlignPacket(u32(packet.num_vsharps * sizeof(Gcn::Buffer)));
    if (packet.verify) {
        // The resource tables the GPU thread walked must read the same now: else a write
        // still queued here (WriteData, DMA) changed them after that thread read them.
        static u32 flat_reports = 0;
        for (u32 i = 0; i < packet.num_stages && flat_reports < 32; ++i) {
            const auto& snapshot = Shader::Info::ud_snapshots[i];
            const auto& srt = snapshot.info->srt_info;
            // Stages the pipeline selection did not refresh (no user data) keep their tables.
            if (!srt.walker_func || snapshot.flat_size == 0 || snapshot.user_data_size == 0) {
                continue;
            }
            std::vector<u32> flat(snapshot.flat_size);
            std::memcpy(flat.data(), snapshot.user_data,
                        std::min(snapshot.user_data_size, snapshot.flat_size) * sizeof(u32));
            // Pointers in the tables may be stale by now: a fault only skips the check.
            sigjmp_buf recover;
            if (sigsetjmp(recover, 0)) {
                runtime_fault_recover = nullptr;
                continue;
            }
            runtime_fault_recover = &recover;
            srt.walker_func(snapshot.user_data, flat.data());
            runtime_fault_recover = nullptr;
            if (std::memcmp(flat.data(), snapshot.flat, snapshot.flat_size * sizeof(u32)) != 0) {
                u32 first = 0, count = 0;
                for (u32 w = 0; w < snapshot.flat_size; ++w) {
                    if (flat[w] != snapshot.flat[w]) {
                        first = count++ ? first : w;
                    }
                }
                std::printf("Draw pipe: resource tables of shader %016llx changed after the GPU "
                            "thread read them (%u of %u words, first %u: %#x -> %#x, user data "
                            "%u words)\n",
                            static_cast<unsigned long long>(snapshot.info->pgm_hash), count,
                            snapshot.flat_size, first, snapshot.flat[first], flat[first],
                            snapshot.user_data_size);
                ++flat_reports;
            }
        }
        const auto* live = reinterpret_cast<const Gcn::Regs*>(in);
        in += sizeof(Gcn::Regs);
        static u32 reports = 0;
        for (u32 block = 0; block < Gcn::RegDirty::NumBlocks && reports < 32; ++block) {
            const u32 at = block * Gcn::RegDirty::BlockWords;
            if (std::memcmp(regs.reg_array.data() + at, live->reg_array.data() + at,
                            Gcn::RegDirty::BlockWords * sizeof(u32)) != 0) {
                for (u32 w = at; w < at + Gcn::RegDirty::BlockWords; ++w) {
                    if (regs.reg_array[w] != live->reg_array[w]) {
                        std::printf("Draw pipe: register copy differs at word %#x (%#x vs %#x)\n",
                                    w, regs.reg_array[w], live->reg_array[w]);
                        ++reports;
                        break;
                    }
                }
            }
        }
    }
    ASSERT(static_cast<u32>(in - data) == size);
    stage_regs = &regs;
    self.pipe_cb_extent = packet.cb_extent;
    self.pipe_db_extent = packet.db_extent;
    if (packet.kind == PacketKind::Special) {
        // A draw FilterDraw handles (fast clear elimination, resolve, depth copy, skip).
        FrameCapture::Poll();
        self.scheduler.PopPendingOperations();
        self.FilterDraw();
    } else if (cs) {
        stage_cs = cs;
        const auto* pipeline = static_cast<const ComputePipeline*>(packet.pipeline);
        if (indirect) {
            self.DispatchIndirectRecord(pipeline, indirect->args, indirect->stride);
        } else {
            self.DispatchRecord(pipeline);
        }
        stage_cs = nullptr;
    } else if (indirect) {
        self.DrawIndirectRecord(static_cast<const GraphicsPipeline*>(packet.pipeline),
                                packet.is_indexed, *indirect);
    } else {
        self.DrawRecord(static_cast<const GraphicsPipeline*>(packet.pipeline), packet.prepared,
                        packet.is_indexed, packet.index_offset);
    }
    Shader::Info::num_ud_snapshots = 0;
    self.num_ring_stages = 0;
    self.packet_vsharps = {};
    if (self.constant_ring) {
        self.constant_ring->Stamp(packet.ring_end);
    }
}

void Rasterizer::PrintPipeStats() {
    static const bool stats = std::getenv("BB_FRAME_STATS") != nullptr;
    if (!stats || (draw_pipe->packets & 1023) != 0) {
        return;
    }
    static auto window = std::chrono::steady_clock::now();
    static u64 last_packets = 0, last_drains = 0, last_drain_cycles = 0, last_busy = 0;
    static u64 last_tsc = __rdtsc();
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - window).count();
    if (seconds < 5.0) {
        return;
    }
    const u64 tsc = __rdtsc();
    const double cycles = double(tsc - last_tsc);
    const u64 busy = draw_pipe->busy_cycles.load(std::memory_order_relaxed);
    std::printf("Draw pipe: %.0f draws/s pipelined, %.0f drains/s that waited, stage A waited "
                "%.1f%%, stage B busy %.1f%%\n",
                (draw_pipe->packets - last_packets) / seconds,
                (draw_pipe->drains - last_drains) / seconds,
                100.0 * (draw_pipe->drain_cycles - last_drain_cycles) / cycles,
                100.0 * (busy - last_busy) / cycles);
    std::printf("  drains by reason/s:");
    for (u32 r = 0; r < DrawPipe::NumReasons; ++r) {
        if (const u64 n = draw_pipe->drains_by_reason[r]) {
            static constexpr const char* names[] = {"ConstRam", "commands", "compute",
                                                    "submission end", "draw", "rasterizer"};
            const double share = 100.0 * draw_pipe->cycles_by_reason[r] / cycles;
            draw_pipe->cycles_by_reason[r] = 0;
            if (r < 256) {
                std::printf(" %s=%.0f (%.1f%%)",
                            BbCp::Diagnostics::OpName(r).c_str(), n / seconds,
                            share);
            } else {
                std::printf(" %s=%.0f (%.1f%%)", names[r - 256], n / seconds, share);
            }
        }
    }
    if (constram_waits_skipped) {
        std::printf(" ConstRam not waited=%.0f", constram_waits_skipped / seconds);
        constram_waits_skipped = 0;
    }
    if (BbSections::Enabled()) {
        static std::array<u64, BbSections::Count> last_sections{};
        const double packets = double(draw_pipe->packets - last_packets);
        const double tsc_hz = cycles / seconds;
        std::printf("\n  stage B us per packet:");
        for (u32 s = 0; s < BbSections::Count; ++s) {
            const u64 now_cycles = BbSections::cycles[s].load(std::memory_order_relaxed);
            if (packets > 0) {
                std::printf(" %s %.2f", BbSections::Names[s],
                            1e6 * double(now_cycles - last_sections[s]) / tsc_hz / packets);
            }
            last_sections[s] = now_cycles;
        }
        static u64 last_ring_buffers = 0, last_ring_copies = 0, last_ring_bytes = 0;
        if (packets > 0) {
            const u64 b = BbSections::ring_buffers.load(), c = BbSections::ring_copies.load(),
                      n = BbSections::ring_bytes.load();
            std::printf("\n  stage A ring per packet: %.2f buffers, %.2f copies, %.0f bytes",
                        double(b - last_ring_buffers) / packets,
                        double(c - last_ring_copies) / packets,
                        double(n - last_ring_bytes) / packets);
            last_ring_buffers = b;
            last_ring_copies = c;
            last_ring_bytes = n;
        }
        // Racy reads of stage B's counters: diagnostics only.
        std::printf("\n  stage B tasks (us per frame-second, count/s):");
        static std::array<u64, TaskKinds> last_task_cycles{}, last_task_counts{};
        for (std::size_t k = 0; k < TaskKinds && task_kinds[k]; ++k) {
            Dl_info info{};
            dladdr(task_kinds[k], &info);
            char name[32];
            std::snprintf(name, sizeof(name), "+0x%llx",
                          (unsigned long long)(reinterpret_cast<u64>(task_kinds[k]) -
                                               reinterpret_cast<u64>(info.dli_fbase)));
            const u64 c = task_cycles[k], n = task_counts[k];
            std::printf(" [%s %.0f us/s %.0f/s]", name,
                        1e6 * double(c - last_task_cycles[k]) / tsc_hz / seconds,
                        double(n - last_task_counts[k]) / seconds);
            last_task_cycles[k] = c;
            last_task_counts[k] = n;
        }
    }
    std::printf("\n  stage A waits over 0.5%%:");
    for (const auto& [function, lines] : drain_sites) {
        for (const auto& [line, waited] : lines) {
            if (waited * 200 > cycles) {
                std::printf(" %s:%u %.1f%%", function, line, 100.0 * waited / cycles);
            }
        }
    }
    std::printf("\n");
    drain_sites.clear();
    draw_pipe->drains_by_reason = {};
    window = now;
    last_tsc = tsc;
    last_packets = draw_pipe->packets;
    last_drains = draw_pipe->drains;
    last_drain_cycles = draw_pipe->drain_cycles;
    last_busy = busy;
}

} // namespace Vulkan
