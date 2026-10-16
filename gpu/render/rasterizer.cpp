// SPDX-License-Identifier: GPL-2.0-or-later
// bbport render: Rasterizer, the draw and dispatch entry points: pipeline selection, then recording here or on the
// draw recording thread (raster_pipe.cpp), and the per-frame bookkeeping.

#include "render/raster_internal.h"

namespace Vulkan {

Rasterizer::Rasterizer(const Instance& instance_, Scheduler& scheduler_, Runtime& runtime_,
                       BbCp::CommandProcessor* liverpool_)
    : instance{instance_}, scheduler{scheduler_}, runtime{runtime_}, page_watch{this},
      buffer_cache{instance, scheduler, runtime, liverpool_, texture_cache, page_watch},
      texture_cache{instance, scheduler, runtime, liverpool_, buffer_cache, page_watch},
      liverpool{liverpool_}, memory{Core::Memory::Instance()},
      pipeline_cache{instance, scheduler, liverpool, buffer_cache.GetSparsePageShift()},
      host_markers_enabled{EmulatorSettings.IsVkHostMarkersEnabled()},
      guest_markers_enabled{EmulatorSettings.IsVkGuestMarkersEnabled()} {
    // Before the rasterizer is bound: Liverpool enqueues buffers only once it sees it.
    draw_prep = std::make_unique<DrawPreparation>(pipeline_cache);
    // A program (or fetch shader) that does not look like one yet: the game may have copied it
    // into place with the GPU (DMA) in work recorded but not run yet (another game: all-zero
    // programs, the load hung). Called on the GPU command thread, inside a draw: the work
    // handed to the recording thread and recorded before it runs, so a
    // program the GPU copies into place (in place in guest memory) is there. No read back of
    // GPU-modified ranges here: downloading 512 KiB windows at draws overwrote what the CPU had
    // written there with older GPU data (another game: shader code zeroed, characters missing,
    // 5 FPS), and asking this very thread for one waited forever.
    pipeline_cache.SetProgramWriteSync(
        [this](VAddr address, u64 size) { SyncProgramWrites(address, size); });
    pipeline_cache.SetProgramMemorySync([this](VAddr, u64) {
        DrainDrawPipe(); // a copy into place may still be a packet for the recording thread
        Finish();
    });
    scene_targets = std::make_unique<SceneTargets>(instance, scheduler, runtime, texture_cache);
    // Object motion first: it fixes the buffer addresses the motion shader variants embed.
    object_motion = std::make_unique<ObjectMotion>(instance, scheduler);
    camera_motion = std::make_unique<CameraMotion>(instance, scheduler, texture_cache, runtime);
    camera_motion->SetObjectMotion(object_motion.get());
    upscaler = std::make_unique<TemporalUpscaler>(instance, scheduler, texture_cache, runtime,
                                                  *camera_motion, *scene_targets);
    prompts = std::make_unique<ButtonPrompts>(instance, scheduler, buffer_cache);
    if (!EmulatorSettings.IsNullGPU()) {
        liverpool->BindRasterizer(this);
    }
    BbGuestMemory::Install(instance); // bbport: before the runtime maps direct memory
    memory->SetRasterizer(this);

    GpuProfiler::Init(instance, scheduler);
    if (DrawPipeWanted()) {
        constant_ring = std::make_unique<ConstantRing>(instance, scheduler);
        draw_pipe = std::make_unique<DrawPipe>(&RunDrawPacket, this);
    }
    // bbport: this thread joins the texture binding helper before it changes image state.
    runtime.SetImageAccessHook(&JoinBindHelper, this);
    scheduler.SetSubmitCallback([this](Vulkan::SubmitInfo& info) {
        runtime.FlushBarriers();
        buffer_cache.SubmitPendingArenaBinds(info);
    });
}

Rasterizer::~Rasterizer() = default;


namespace {
// bbport: the pipeline's own type (optional<const ...>): the other one made each draw copy it,
// its attribute list included (a malloc per draw).
std::pair<u32, u32> GetDrawOffsets(
    const Gcn::Regs& regs, const Shader::Info& info,
    const std::optional<const Shader::Gcn::FetchShaderData>& fetch_shader) {
    u32 vertex_offset = regs.index_offset;
    u32 instance_offset = 0;
    if (fetch_shader) {
        if (vertex_offset == 0 && fetch_shader->vertex_offset_sgpr != -1) {
            vertex_offset = info.UserData()[fetch_shader->vertex_offset_sgpr];
        }
        if (fetch_shader->instance_offset_sgpr != -1) {
            instance_offset = info.UserData()[fetch_shader->instance_offset_sgpr];
        }
    }
    return {vertex_offset, instance_offset};
}
} // namespace

void Rasterizer::Draw(bool is_indexed, u32 index_offset, const PreparedDraw* prepared) {
    BB_SECTION(ADraw);
    BbStats::draws.fetch_add(1, std::memory_order_relaxed);

    // bbport: with the draw pipeline this thread only selects the pipeline and hands the draw
    // to the recording thread (DrawRecord there); draws FilterDraw handles itself run here.
    const bool pipelined = UseDrawPipe();
    if (pipelined && !FilterDrawPasses() &&
        !BbToggle::Disabled(BbToggle::PipelinedMemoryWrites)) {
        PostDraw(nullptr, nullptr, false, 0);
        return;
    }
    if (!pipelined || !FilterDrawPasses()) {
        DrainDrawPipe(DrawPipe::ReasonDraw);
        FrameCapture::Poll();
        scheduler.PopPendingOperations();
        if (!FilterDraw()) {
            return;
        }
    }
    const GraphicsPipeline* pipeline = pipeline_cache.GetGraphicsPipeline({}, prepared);
    const PreparedDraw* used_prepared = pipeline_cache.UsedPrepared();
    if (prepared) {
        const u32 state = prepared->state.load(std::memory_order_acquire);
        draw_prep->Count(used_prepared != nullptr,
                         state == PreparedDraw::Pending       ? DrawPreparation::NotReady
                         : state == PreparedDraw::Unavailable ? DrawPreparation::Unavailable
                         : prepared->reg_checksum != liverpool->gfx_reg_checksum
                             ? DrawPreparation::OtherRegisters
                             : DrawPreparation::OtherPrograms);
    }
    if (!pipeline) {
        return;
    }
    if (pipelined && FilterDrawPasses()) {
        PostDraw(pipeline, used_prepared, is_indexed, index_offset);
        return;
    }
    DrawRecord(pipeline, used_prepared, is_indexed, index_offset);
}

void Rasterizer::DrawRecord(const GraphicsPipeline* pipeline, const PreparedDraw* used_prepared,
                            bool is_indexed, u32 index_offset) {
    BB_SECTION(DrawRecord);
    buffer_cache.NewPacket();
    if (DrawPipe::OnStageB()) {
        FrameCapture::Poll();
        scheduler.PopPendingOperations();
    }
    // bbport (BB_PREUPLOAD): once a frame, guest GPU memory the CPU wrote and then left alone is
    // uploaded ahead of its use, BB_PREUPLOAD_MB (16) at most per frame.
    if (const u32 frame = BbStats::frame_number.load(std::memory_order_relaxed);
        frame != preupload_frame) {
        preupload_frame = frame;
        BB_SECTION(Preupload);
        static const u64 budget = [] {
            const char* env = std::getenv("BB_PREUPLOAD_MB");
            return (env ? std::strtoull(env, nullptr, 10) : 16ull) << 20;
        }();
        buffer_cache.Preupload(budget);
    }
    const auto& regs = Regs();
    // bbport: the pass copying a finished frame to a display buffer; the previous draw's
    // target is that frame.
    if (camera_motion->Enabled() && regs.color_buffers[0] &&
        FrameCapture::IsDisplayBuffer(regs.color_buffers[0].Address())) {
        scene_started = false;
        upscaler->OnDisplayPassStart(regs.color_buffers[0].Address());
        camera_motion->OnDisplayPass(cb_descs[0].first);
        if (upscaler->OnFrameStart()) {
            object_motion->InvalidateHistory();
            camera_motion->InvalidateHistory();
        }
        camera_motion->SetJitter(upscaler->Jitter());
        object_motion->OnFrameStart();
        NoteFrameStart();
        static const char* scene_debug = std::getenv("BB_SCENE_DEBUG");
        scene_debug_frame = scene_debug && std::remove(scene_debug) == 0;
        scene_targets->debug = scene_debug_frame;
    }
    // BB_SKIP_PS=hash[,hash...] (diagnostics): draws with these pixel shaders are left out.
    static const std::vector<u64> skipped_ps = [] {
        std::vector<u64> hashes;
        if (const char* env = std::getenv("BB_SKIP_PS")) {
            for (const char* at = env; *at;) {
                char* end = nullptr;
                hashes.push_back(std::strtoull(at, &end, 16));
                at = *end ? end + 1 : end;
            }
        }
        return hashes;
    }();
    if (!skipped_ps.empty()) {
        const auto* ps = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
        if (ps && std::ranges::find(skipped_ps, ps->pgm_hash) != skipped_ps.end()) {
            return;
        }
    }
    // bbport: the key guide icons for the device in use (a change: uploaded before this draw).
    prompts->Update();
    bind_prepared = used_prepared;
    motion_draw = pipeline->GetGraphicsKey().motion_vectors;
    motion_geometry = 0;

    PrepareRenderState(pipeline);
    NoteUpscalerDraw(pipeline);
    const PreparedDraw* draw_prepared = bind_prepared;
    // bbport: vertex and index buffers are resolved while the helper binds textures (their
    // commands are recorded after BeginRendering, as before).
    draw_inputs = {pipeline, draw_prepared, index_offset, is_indexed, true, false};
    const bool bound = BindResources(pipeline);
    bind_prepared = nullptr; // indirect draws and dispatches bind without prepared sharps
    const bool inputs_resolved = draw_inputs.resolved;
    draw_inputs.pending = false;
    if (!bound) {
        return;
    }
    const auto state = BeginRendering(pipeline);

    if (!inputs_resolved) {
        ResolveVertexBuffers(pipeline, draw_prepared);
        if (is_indexed) {
            ResolveIndexBuffer(index_offset);
        }
    }
    EmitVertexBuffers();
    if (is_indexed) {
        EmitIndexBuffer();
    }

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    ScaleToTarget(pipeline);
    if (motion_draw && motion_geometry) {
        BB_SECTION(Motion);
        const auto& vs = pipeline->GetStage(Shader::SwStage::Vertex);
        const auto [base_vertex, first_instance] =
            GetDrawOffsets(regs, vs, pipeline->GetFetchShader());
        const u32 index_size =
            regs.index_buffer_type.index_type == Gcn::IndexType::Index16 ? 2u : 4u;
        const VAddr index_address = is_indexed
            ? regs.index_base_address.Address<VAddr>() + u64(index_offset) * index_size : 0;
        // Without a character-size skeleton the draw is gated on its bone palette changing
        // since last frame (model constants follow the camera, the palette does not). The
        // check comes first: most gated draws are static world pieces with long index lists.
        static const bool all_motion = [] {
            const char* value = std::getenv("BB_OBJECT_MOTION_ALL");
            return value && value[0] == '1';
        }();
        bool gated = !all_motion;
        u64 palette = 0;
        for (const auto& resource : vs.buffers) {
            if (resource.IsSpecial()) continue;
            const auto buffer = resource.GetSharp(vs);
            const u32 size = buffer.GetSize();
            if (!buffer.Valid() || buffer.GetStride() != 16) continue;
            const auto role = Motion::ClassifyBuffer(size);
            if (role == Motion::BufferRole::Skeleton) {
                gated = false;
                break;
            }
            const VAddr address = buffer.base_address;
            if (role == Motion::BufferRole::SmallSkeleton &&
                memory->IsValidGpuMapping(address, 0) &&
                memory->ClampRangeSize(address, size) == size) {
                palette = XXH3_64bits_withSeed(reinterpret_cast<const void*>(address), size,
                                               palette);
            }
        }
        push_data.motion_param = 0;
        if (!gated || object_motion->Moving({.shader = vs.pgm_hash,
                                             .geometry = motion_geometry,
                                             .indices = index_address,
                                             .index_count = regs.num_indices,
                                             .instances = regs.num_instances.NumInstances(),
                                             .first_instance = first_instance},
                                            palette)) {
            Motion::VertexRange range{base_vertex, regs.num_indices};
            u64 topology = 0;
            if (is_indexed) {
                range = {};
                const u64 bytes = u64(regs.num_indices) * index_size;
                if (index_address && memory->IsValidGpuMapping(index_address, 0) &&
                    memory->ClampRangeSize(index_address, bytes) == bytes) {
                    const bool restart = regs.enable_primitive_restart != 0;
                    const u32 count = regs.num_indices;
                    const auto scanned = object_motion->IndexRange(
                        {index_address, count, index_size, s32(base_vertex), restart}, [&] {
                            const auto* data = reinterpret_cast<const void*>(index_address);
                            const auto scan_range =
                                index_size == 2
                                    ? Motion::IndexedRange(
                                          std::span(static_cast<const u16*>(data), count),
                                          s32(base_vertex), restart)
                                    : Motion::IndexedRange(
                                          std::span(static_cast<const u32*>(data), count),
                                          s32(base_vertex), restart);
                            return Motion::IndexRangeCache::Result{scan_range,
                                                                   XXH3_64bits(data, bytes)};
                        });
                    range = scanned.range;
                    topology = scanned.topology;
                }
            }
            push_data.motion_param = object_motion->PrepareDraw({
                .shader = vs.pgm_hash,
                .geometry = motion_geometry,
                .indices = index_address,
                .topology = topology,
                .index_count = regs.num_indices,
                .instances = regs.num_instances.NumInstances(),
                .first_instance = first_instance,
                .vertices = range,
            });
        }
    }
    // Geometry drawn with the scene depth is jittered, full-screen passes are not (a shifted
    // full-screen quad leaves an edge column unwritten).
    BeginDraw(pipeline, state, is_indexed,
              regs.num_indices > 6 || regs.num_instances.NumInstances() > 1);

    const auto& vs_info = pipeline->GetStage(Shader::SwStage::Vertex);
    const auto& fetch_shader = pipeline->GetFetchShader();
    const auto [vertex_offset, instance_offset] = GetDrawOffsets(regs, vs_info, fetch_shader);

    const vk::Pipeline handle = pipeline->Handle();
    const u32 num_indices = regs.num_indices;
    const u32 num_instances = regs.num_instances.NumInstances();
    const u32 first_vertex = vertex_offset;
    const u32 first_instance = instance_offset;
    const auto* ps_info = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
    const Breadcrumbs::Crumb crumb{
        .kind = is_indexed ? Breadcrumbs::Kind::DrawIndexed : Breadcrumbs::Kind::Draw,
        .hash = {vs_info.pgm_hash, ps_info ? ps_info->pgm_hash : 0},
        .program = {vs_info.ProgramBase(), ps_info ? ps_info->ProgramBase() : 0},
        .count = {num_indices, num_instances, 0},
    };
    scheduler.RecordCrumb(crumb, [=](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, handle);
        if (is_indexed) {
            cmdbuf.drawIndexed(num_indices, num_instances, 0, s32(first_vertex), first_instance);
        } else {
            cmdbuf.draw(num_indices, num_instances, first_vertex, first_instance);
        }
    });
    if (FrameCapture::Active()) {
        const auto* ps = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
        FrameCapture::Draw(vs_info.pgm_hash, ps ? ps->pgm_hash : 0, num_indices, num_instances);
        if (const auto& bc = regs.blend_control[0]; bc.enable && regs.color_buffers[0]) {
            char note[128];
            std::snprintf(note, sizeof(note), "\n  blend ps %08x idx %u: src %u dst %u func %u z %u%u",
                          u32(ps ? ps->pgm_hash : 0), num_indices, u32(bc.color_src_factor),
                          u32(bc.color_dst_factor), u32(bc.color_func),
                          u32(regs.depth_control.depth_enable),
                          u32(regs.depth_control.depth_write_enable));
            FrameCapture::Note(note);
        }
    }

    BB_SECTION(Kick);
    ResetBindings(false);
    scheduler.KickRecording();
}

void Rasterizer::NoteUpscalerDraw(const GraphicsPipeline* pipeline) {
    // Single-target passes: the upscaler looks for the scene's and the UI's (UiComposition).
    if (upscaler->Enabled() && std::popcount(pipeline->GetGraphicsKey().mrt_mask) == 1) {
        const auto& viewport = Regs().viewports[0];
        upscaler->OnDraw(pipeline->GetStage(Shader::SwStage::Vertex).pgm_hash,
                         cb_descs[0].first, db_desc.first,
                         UiComposition::NativeViewport(viewport.xscale * 2, viewport.yscale * 2));
    }
}

void Rasterizer::ScaleToTarget(const GraphicsPipeline* pipeline) {
    UiEdgeStretch(pipeline);
    // Screen-space (clip disabled) draws into the upscaler's output-size images.
    push_data.xscale *= target_scale[0];
    push_data.xoffset *= target_scale[0];
    push_data.yscale *= target_scale[1];
    push_data.yoffset *= target_scale[1];
}

void Rasterizer::BeginDraw(const GraphicsPipeline* pipeline, const RenderState& state,
                           bool is_indexed, bool jitter_eligible) {
    {
        BB_SECTION(PipelineBind);
        pipeline->BindResources(set_writes, push_data, {image_infos.data(), image_infos.size()},
                                {buffer_infos.data(), buffer_infos.size()});
    }
    // The upscaler's sub-pixel jitter, for geometry drawn with the scene depth.
    draw_jitter = {};
    if (jitter_eligible && upscaler->Enabled() && db_desc.first &&
        db_desc.first == camera_motion->Depth()) {
        draw_jitter = upscaler->Jitter();
    }
    UpdateDynamicState(pipeline, is_indexed);
    MarkPass(pipeline, state);
    if (occlusion) {
        occlusion->SetPassScale(target_scale[0] * target_scale[1]); // samples at the game's size
    }
    scheduler.BeginRendering(state);
}

void Rasterizer::NoteCopyIntoMemory(VAddr address, u64 size) {
    if (size == 0 || !OnStageA()) {
        return;
    }
    copies_into_memory[num_copies_into_memory++ % copies_into_memory.size()] = {address,
                                                                               address + size};
    if (num_copies_into_memory % copies_into_memory.size() == 0) {
        // Once around the ring: the bounds of the copies kept (the ones pushed out no longer).
        copies_begin = ~VAddr{0};
        copies_end = 0;
        for (const auto& copy : copies_into_memory) {
            copies_begin = std::min(copies_begin, copy.begin);
            copies_end = std::max(copies_end, copy.end);
        }
        return;
    }
    copies_begin = std::min(copies_begin, address);
    copies_end = std::max(copies_end, address + size);
}

// Program code the GPU copies into place (DMA in the stream before the draw): this thread decodes
// the draw ahead of the copy and read the code that was there before (another program: a
// character's vertices stretched into streaks in another game; or zeros). Code under a copy noted
// since the GPU last ran everything: everything before it runs first. Rare (shader uploads); the
// last 64 copies are kept (a game copies far more a frame: a wait for each program check after
// the ring filled cost a quarter of this thread).
void Rasterizer::SyncProgramWrites(VAddr address, u64 size) {
    if (address + size <= copies_begin || address >= copies_end || !OnStageA()) {
        return;
    }
    bool overlaps = false;
    const u32 kept = std::min<u32>(num_copies_into_memory, u32(copies_into_memory.size()));
    for (u32 i = 0; !overlaps && i < kept; ++i) {
        const auto& copy = copies_into_memory[i];
        overlaps = copy.begin < address + size && address < copy.end;
    }
    if (!overlaps) {
        return;
    }
    DrainDrawPipe();
    Finish();
    num_copies_into_memory = 0;
    copies_begin = ~VAddr{0};
    copies_end = 0;
}

void Rasterizer::DrawIndirect(bool is_indexed, VAddr arg_address, u32 offset, u32 stride,
                              u32 max_count, VAddr count_address, u16 vertex_sgpr_offset,
                              u16 instance_sgpr_offset) {
    // bbport: like direct draws, handed to the recording thread after the pipeline selection
    // (else the GPU thread waited here for a whole frame of queued draws).
    const bool pipelined = UseDrawPipe() && FilterDrawPasses() &&
                           !BbToggle::Disabled(BbToggle::PipelinedIndirectDraws);
    if (!pipelined) {
        DrainDrawPipe();
        scheduler.PopPendingOperations();
        if (!FilterDraw()) {
            return;
        }
    }

    const DrawIndirectParams params = {
        .vertex_sgpr_offset = vertex_sgpr_offset,
        .instance_sgpr_offset = instance_sgpr_offset,
    };
    const GraphicsPipeline* pipeline = pipeline_cache.GetGraphicsPipeline(params, nullptr, true);
    if (!pipeline) {
        return;
    }
    const IndirectDraw indirect = {arg_address + offset, count_address, stride, max_count};
    if (pipelined) {
        PostDraw(pipeline, nullptr, is_indexed, 0, nullptr, &indirect);
        return;
    }
    DrawIndirectRecord(pipeline, is_indexed, indirect);
}

void Rasterizer::DrawIndirectRecord(const GraphicsPipeline* pipeline, bool is_indexed,
                                    const IndirectDraw& indirect) {
    buffer_cache.NewPacket();
    if (DrawPipe::OnStageB()) {
        FrameCapture::Poll();
        scheduler.PopPendingOperations();
    }
    const VAddr count_address = indirect.count;
    const u32 stride = indirect.stride;
    const u32 max_count = indirect.max_count;

    // Indirect arguments may be GPU-written: leave these draws on camera fallback.
    motion_draw = false;
    motion_geometry = 0;
    PrepareRenderState(pipeline);
    NoteUpscalerDraw(pipeline);
    if (!BindResources(pipeline)) {
        return;
    }
    const auto [buffer, base] =
        buffer_cache.ObtainBuffer(indirect.args, stride * max_count, false);
    needs_barrier |= runtime.IsBufferAccessed(buffer, base, stride * max_count);
    // The arguments checked on the GPU first (vk_indirect_guard.h): a dispatch, outside the
    // render pass.
    vk::Buffer checked_args = buffer->Handle();
    u64 checked_offset = base;
    if (IndirectGuard::Enabled()) {
        if (!indirect_guard) {
            indirect_guard = std::make_unique<IndirectGuard>(instance, scheduler);
        }
        if (needs_barrier) {
            runtime.FlushBarriers();
            needs_barrier = false;
        }
        scheduler.EndRendering();
        std::tie(checked_args, checked_offset) =
            indirect_guard->CheckDrawArgs(*buffer, base, stride, max_count, is_indexed);
    }
    const auto state = BeginRendering(pipeline);

    BindVertexBuffers(pipeline);
    if (is_indexed) {
        BindIndexBuffer();
    }

    const VideoCore::Buffer* count_buffer;
    u64 count_offset;
    if (count_address != 0) {
        std::tie(count_buffer, count_offset) = buffer_cache.ObtainBuffer(count_address, 4, false);
        needs_barrier |= runtime.IsBufferAccessed(count_buffer, count_offset, 4);
    }

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    ScaleToTarget(pipeline);
    BeginDraw(pipeline, state, is_indexed, true);

    ASSERT(stride == (is_indexed ? sizeof(VkDrawIndexedIndirectCommand)
                                 : sizeof(VkDrawIndirectCommand)));
    const vk::Pipeline handle = pipeline->Handle();
    const vk::Buffer args = checked_args;
    const u64 args_offset = checked_offset;
    const vk::Buffer counts = count_address != 0 ? count_buffer->Handle() : vk::Buffer{};
    const u64 counts_offset = count_address != 0 ? count_offset : 0;
    const auto* vs_crumb = pipeline->GetStages()[u32(Shader::SwStage::Vertex)];
    const auto* ps_crumb = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
    const Breadcrumbs::Crumb crumb{
        .kind = Breadcrumbs::Kind::DrawIndirect,
        .hash = {vs_crumb ? vs_crumb->pgm_hash : 0, ps_crumb ? ps_crumb->pgm_hash : 0},
        .program = {vs_crumb ? vs_crumb->ProgramBase() : 0,
                    ps_crumb ? ps_crumb->ProgramBase() : 0},
        .address = indirect.args,
        .count = {max_count, stride, 0},
    };
    scheduler.RecordCrumb(crumb, [=](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, handle);
        if (is_indexed) {
            if (counts) {
                cmdbuf.drawIndexedIndirectCount(args, args_offset, counts, counts_offset, max_count,
                                                stride);
            } else {
                cmdbuf.drawIndexedIndirect(args, args_offset, max_count, stride);
            }
        } else if (counts) {
            cmdbuf.drawIndirectCount(args, args_offset, counts, counts_offset, max_count, stride);
        } else {
            cmdbuf.drawIndirect(args, args_offset, max_count, stride);
        }
    });

    ResetBindings(false);
    scheduler.KickRecording();
}

void Rasterizer::DispatchDirect() {
    BbStats::dispatches.fetch_add(1, std::memory_order_relaxed);
    // bbport: like draws, handed to the draw recording thread after the pipeline selection.
    const bool pipelined = UseDrawPipe() && !BbToggle::Disabled(BbToggle::PipelinedDispatch);
    if (!pipelined) {
        DrainDrawPipe();
    }
    const ComputePipeline* pipeline = pipeline_cache.GetComputePipeline();
    if (!pipeline) {
        return;
    }
    if (pipelined) {
        PostDraw(pipeline, nullptr, false, 0, &CsRegs());
        return;
    }
    DispatchRecord(pipeline);
}

void Rasterizer::DispatchRecord(const ComputePipeline* pipeline) {
    BB_SECTION(DispatchRecord);
    buffer_cache.NewPacket();
    FrameCapture::Poll();
    gbuffer_draw = false;

    scheduler.PopPendingOperations();

    const auto& cs_program = CsRegs();

    const auto& cs = pipeline->GetStage(Shader::SwStage::Compute);
    // After the resource binding: its transfers (image uploads, downloads) mark themselves.
    const auto mark = [&] {
        if (auto* profiler = GpuProfiler::Get()) {
            profiler->Mark(cs.pgm_hash ^ 0xD15Aull, [&] {
                return fmt::format("dispatch cs {:016x} ({}x{}x{} groups)", cs.pgm_hash,
                                   cs_program.dim_x, cs_program.dim_y, cs_program.dim_z);
            });
        }
    };
    if (upscaler->Enabled()) {
        upscaler->OnDispatch(cs.pgm_hash);
    }
    bool hle;
    {
        BB_SECTION(ShaderHle);
        hle = ExecuteShaderHLE(cs, Regs(), cs_program, *this);
    }
    if (hle) {
        return;
    }
    // bbport: the game's copy shader running as itself (BB_COPY_SHADER_NATIVE): its destinations stay in
    // the game's memory. They hold data our translator reads on the CPU (next to shader code the
    // game uploads this way); a VRAM copy of them showed it a stale copy (a black scene). The
    // copy list read on the CPU (vk_shader_hle.cpp) kept them in place too.
    buffer_cache.force_writes_in_place =
        Game::BufferCopyShader() != 0 && cs.pgm_hash == Game::BufferCopyShader();
    const bool bound = BindResources(pipeline);
    buffer_cache.force_writes_in_place = false;
    if (!bound) {
        return;
    }

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    scheduler.EndRendering();
    mark();
    pipeline->BindResources(set_writes, push_data, {image_infos.data(), image_infos.size()},
                            {buffer_infos.data(), buffer_infos.size()});

    const vk::Pipeline handle = pipeline->Handle();
    const u32 dim_x = cs_program.dim_x, dim_y = cs_program.dim_y, dim_z = cs_program.dim_z;
    if (FrameCapture::Active()) {
        FrameCapture::Dispatch(cs.pgm_hash, dim_x, dim_y, dim_z);
    }
    const Breadcrumbs::Crumb crumb{
        .kind = Breadcrumbs::Kind::Dispatch,
        .hash = {cs.pgm_hash, 0},
        .program = {cs.ProgramBase(), 0},
        .count = {dim_x, dim_y, dim_z},
    };
    scheduler.RecordCrumb(crumb, [=](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, handle);
        cmdbuf.dispatch(dim_x, dim_y, dim_z);
    });

    ResetBindings(true);
    scheduler.KickRecording();
}

void Rasterizer::DispatchIndirect(VAddr address, u32 offset, u32 size) {
    // bbport: handed to the recording thread like direct dispatches.
    const bool pipelined = UseDrawPipe() && !BbToggle::Disabled(BbToggle::PipelinedIndirectDraws);
    if (!pipelined) {
        DrainDrawPipe();
    }
    const ComputePipeline* pipeline = pipeline_cache.GetComputePipeline();
    if (!pipeline) {
        return;
    }
    if (pipelined) {
        const IndirectDraw indirect = {address + offset, 0, size, 1};
        PostDraw(pipeline, nullptr, false, 0, &CsRegs(), &indirect);
        return;
    }
    DispatchIndirectRecord(pipeline, address + offset, size);
}

void Rasterizer::DispatchIndirectRecord(const ComputePipeline* pipeline, VAddr address,
                                        u32 size) {
    buffer_cache.NewPacket();
    scheduler.PopPendingOperations();
    if (!BindResources(pipeline)) {
        return;
    }
    const auto [buffer, base] = buffer_cache.ObtainBuffer(address, size, false);
    needs_barrier |= runtime.IsBufferAccessed(buffer, base, size);

    if (needs_barrier) {
        runtime.FlushBarriers();
    }

    scheduler.EndRendering();
    // Group counts checked on the GPU first (vk_indirect_guard.h); recorded before the
    // pipeline's bindings, which the check's own pipeline would disturb.
    vk::Buffer args = buffer->Handle();
    u64 args_offset = base;
    if (IndirectGuard::Enabled()) {
        if (!indirect_guard) {
            indirect_guard = std::make_unique<IndirectGuard>(instance, scheduler);
        }
        std::tie(args, args_offset) = indirect_guard->CheckArgs(*buffer, base);
    }
    pipeline->BindResources(set_writes, push_data, {image_infos.data(), image_infos.size()},
                            {buffer_infos.data(), buffer_infos.size()});

    const vk::Pipeline handle = pipeline->Handle();
    const vk::Buffer raw_args = buffer->Handle();
    const u64 raw_offset = base;
    const auto& cs_crumb = pipeline->GetStage(Shader::SwStage::Compute);
    const u32 args_slot = Breadcrumbs::ArgsSlot();
    const Breadcrumbs::Crumb crumb{
        .kind = Breadcrumbs::Kind::DispatchIndirect,
        .hash = {cs_crumb.pgm_hash, 0},
        .program = {cs_crumb.ProgramBase(), 0},
        .address = address,
        .count = {buffer_cache.IsInPlace(address, size) ? 1u : 0u, 0, 0},
        .args_slot = args_slot,
    };
    scheduler.RecordCrumb(crumb, [=](vk::CommandBuffer cmdbuf) {
        Breadcrumbs::CopyArgs(cmdbuf, raw_args, raw_offset, args_slot); // what the game wrote
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, handle);
        cmdbuf.dispatchIndirect(args, args_offset);
    });

    ResetBindings(true);
    scheduler.KickRecording();
}

u64 Rasterizer::Flush() {
    BB_SECTION(Flush);
    DrainDrawPipe();
    PublishSignals(); // the recording thread's (it is idle here, or this is it)
    last_flush_ns.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                        std::memory_order_relaxed);
    const u64 current_tick = scheduler.CurrentTick();
    SubmitInfo info{};
    scheduler.Flush(info);
    BbTimeline::Note(BbTimeline::VulkanSubmit, current_tick, DrawPipe::OnStageB() ? 1 : 0);
    return current_tick;
}

void Rasterizer::Finish() {
    DrainDrawPipe();
    PublishSignals();
    scheduler.Finish();
}

void Rasterizer::OnSubmit() {
    DrainDrawPipe();
    if (fault_process_pending) {
        fault_process_pending = false;
        buffer_cache.ProcessFaultBuffer();
    }
    texture_cache.ProcessDownloadImages();
    texture_cache.RunGarbageCollector();
    runtime.TickFrame();
}

void Rasterizer::MarkPass(const GraphicsPipeline* pipeline, const RenderState& state) {
    auto* profiler = GpuProfiler::Get();
    if (!profiler || !scheduler.WillBeginRendering(state)) {
        return;
    }
    const auto& vs = pipeline->GetStage(Shader::SwStage::Vertex);
    const auto* ps = pipeline->GetStages()[u32(Shader::SwStage::Fragment)];
    std::array<u64, Gcn::NUM_COLOR_BUFFERS + 6> parts{};
    parts[0] = state.width | u64(state.height) << 32;
    parts[1] = vs.pgm_hash;
    parts[2] = ps ? ps->pgm_hash : 0;
    parts[3] = db_desc.first ? u64(texture_cache.GetImage(db_desc.first).info.pixel_format) : 0;
    for (u32 cb = 0; cb < state.num_color_attachments && cb < Gcn::NUM_COLOR_BUFFERS; ++cb) {
        parts[4 + cb] = cb_descs[cb].first
                            ? u64(texture_cache.GetImage(cb_descs[cb].first).info.pixel_format) + 1
                            : 0;
    }
    const u64 key = XXH3_64bits(parts.data(), sizeof(parts));
    profiler->Mark(key, [&] {
        std::string targets;
        for (u32 cb = 0; cb < state.num_color_attachments; ++cb) {
            targets += cb_descs[cb].first
                           ? vk::to_string(texture_cache.GetImage(cb_descs[cb].first).info.pixel_format)
                           : std::string{"-"};
            targets += ' ';
        }
        return fmt::format("pass {}x{} [{}] depth {} vs {:016x} ps {:016x}", state.width,
                           state.height, targets,
                           db_desc.first ? vk::to_string(
                                               texture_cache.GetImage(db_desc.first).info.pixel_format)
                                         : std::string{"-"},
                           vs.pgm_hash, ps ? ps->pgm_hash : 0);
    });
}

void Rasterizer::NoteFrameStart() {
    if (auto* profiler = GpuProfiler::Get()) {
        profiler->BeginFrame();
    }
    const u64 frame = BbStats::gpu_frames.fetch_add(1, std::memory_order_relaxed) + 1;
    // BB_BUFFER_STATS=1: how many earlier frames the GPU has not finished when the GPU thread
    // starts a frame — the lag that decides whether guest memory can be read in place.
    static const bool stats = [] {
        const char* value = std::getenv("BB_BUFFER_STATS");
        return value && value[0] == '1';
    }();
    if (!stats) {
        return;
    }
    static std::array<u64, 16> frame_ticks{};
    static std::array<u64, 17> lag_histogram{};
    u32 lag = 0;
    for (u32 back = 1; back < frame_ticks.size() && back < frame; ++back) {
        const u64 tick = frame_ticks[(frame - back) % frame_ticks.size()];
        if (tick && !scheduler.IsFree(tick)) {
            lag = back;
        }
    }
    ++lag_histogram[lag];
    frame_ticks[frame % frame_ticks.size()] = scheduler.CurrentTick();
    if (frame % 600 == 0) {
        std::printf("GPU lag at frame start (frames not finished: count):");
        for (u32 i = 0; i < lag_histogram.size(); ++i) {
            if (lag_histogram[i]) {
                std::printf(" %u:%llu", i, static_cast<unsigned long long>(lag_histogram[i]));
            }
        }
        std::printf("\n");
        lag_histogram = {};
    }
}

} // namespace Vulkan
