// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
// Empirical A/B benchmark and correctness harness for Track 1 (shader/pipeline load & cache)
// and Track 2 (GPU barrier narrowing, texture GC query throttling, Stage B bind deduplication).
// Runs headless against the real Vulkan GPU and a temporary /tmp copy of user/cache/CUSA03173.

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <csignal>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <malloc.h>
#include <memory>
#include <new>
#include <numeric>
#include <sched.h>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/resource.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>
#include <vector>

#include "vulkan/vk_common.h"
#include <vk_mem_alloc.h>

#include "bbport_guest_memory.h"
#include "bbport_settings.h"
#include "buffers/buffer.h"
#include "buffers/buffer_cache.h"
#include "common/elf_info.h"
#include "common/path_util.h"
#include "core/emulator_settings.h"
#include "cp/command_processor.h"
#include "games/game_profile.h"
#include "gcn/formats.h"
#include "gcn/sharps.h"
#include "memory/page_watch.h"
#include "render/dynamic_state.h"
#include "render/pipeline.h"
#include "render/pipeline_cache.h"
#include "render/rasterizer.h"
#include "render/runtime.h"
#include "render/scheduler.h"
#include "render/shader_capture.h"
#include "render/vk_occlusion.h"
#include "render/vk_timestamps.h"
#include "shader/backend/bindings.h"
#include "shader/runtime_info.h"
#include "shim/bb_env.h"
#include "textures/image.h"
#include "textures/texture_cache.h"
#include "vulkan/instance.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Allocation counter via global operator new/delete interposition (no recursion)
// ---------------------------------------------------------------------------
namespace {
std::atomic<bool> g_count_allocs{false};
std::atomic<u64> g_alloc_calls{0};
std::atomic<u64> g_alloc_bytes{0};

void RecordAlloc(std::size_t size) noexcept {
    if (g_count_allocs.load(std::memory_order_relaxed)) {
        g_alloc_calls.fetch_add(1, std::memory_order_relaxed);
        g_alloc_bytes.fetch_add(size, std::memory_order_relaxed);
    }
}
} // namespace

void* operator new(std::size_t size) {
    RecordAlloc(size);
    if (void* p = std::malloc(size ? size : 1)) {
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    return ::operator new(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    RecordAlloc(size);
    return std::malloc(size ? size : 1);
}
void* operator new[](std::size_t size, const std::nothrow_t& nt) noexcept {
    return ::operator new(size, nt);
}
void* operator new(std::size_t size, std::align_val_t align) {
    RecordAlloc(size);
    const std::size_t a = std::max<std::size_t>(static_cast<std::size_t>(align), sizeof(void*));
    const std::size_t rounded = ((size ? size : 1) + a - 1) & ~(a - 1);
    void* p = std::aligned_alloc(a, rounded);
    if (!p) {
        throw std::bad_alloc();
    }
    return p;
}
void* operator new[](std::size_t size, std::align_val_t align) {
    return ::operator new(size, align);
}
void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    RecordAlloc(size);
    const std::size_t a = std::max<std::size_t>(static_cast<std::size_t>(align), sizeof(void*));
    const std::size_t rounded = ((size ? size : 1) + a - 1) & ~(a - 1);
    return std::aligned_alloc(a, rounded);
}
void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t& nt) noexcept {
    return ::operator new(size, align, nt);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}

// ---------------------------------------------------------------------------
// 40-bit guest backing arena + shader capture regions + runtime stubs
// ---------------------------------------------------------------------------
extern "C" int bbgpu_handle_fault(void*, void*);

namespace {
struct MappedSpan {
    uintptr_t start{};
    uintptr_t end{};
};
std::vector<MappedSpan> g_guest_spans;
uintptr_t g_arena_base = 0;
constexpr u64 ArenaSize = 32ull << 20;
std::string g_user_dir_str;

const MappedSpan* FindGuestSpan(uintptr_t addr) {
    for (const auto& s : g_guest_spans) {
        if (addr >= s.start && addr < s.end) {
            return &s;
        }
    }
    return nullptr;
}

bool MapFixedRegion(uintptr_t addr, u64 size) {
    if (size == 0) {
        return true;
    }
    const uintptr_t p0 = addr & ~uintptr_t(4095);
    const uintptr_t p1 = (addr + size + 4095) & ~uintptr_t(4095);
    if (g_arena_base && p0 < g_arena_base + ArenaSize && p1 > g_arena_base) {
        return false;
    }
    for (uintptr_t page = p0; page < p1; page += 4096) {
        if (FindGuestSpan(page)) {
            continue;
        }
        void* mapped = mmap(reinterpret_cast<void*>(page), 4096, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (mapped == MAP_FAILED) {
            return false;
        }
        g_guest_spans.push_back({page, page + 4096});
    }
    std::sort(g_guest_spans.begin(), g_guest_spans.end(),
              [](const MappedSpan& a, const MappedSpan& b) { return a.start < b.start; });
    size_t write = 0;
    for (size_t i = 0; i < g_guest_spans.size(); ++i) {
        if (write > 0 && g_guest_spans[write - 1].end >= g_guest_spans[i].start) {
            g_guest_spans[write - 1].end =
                std::max(g_guest_spans[write - 1].end, g_guest_spans[i].end);
        } else {
            g_guest_spans[write++] = g_guest_spans[i];
        }
    }
    g_guest_spans.resize(write);
    return true;
}

int g_pool_fd = -1;
int (*g_chunk_alloc)(uint64_t, uint64_t) = nullptr;
bool g_chunk_bound = false;

bool InitGuestArena() {
    g_pool_fd = memfd_create("bb_perf_pool", MFD_ALLOW_SEALING);
    if (g_pool_fd < 0 || ftruncate(g_pool_fd, ArenaSize) != 0) {
        return false;
    }
    (void)fcntl(g_pool_fd, F_ADD_SEALS, F_SEAL_SHRINK);
    for (uintptr_t base = 0x1000000000ULL; base < 0x8000000000ULL; base += 0x10000000ULL) {
        void* p = mmap(reinterpret_cast<void*>(base), ArenaSize, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_FIXED_NOREPLACE, g_pool_fd, 0);
        if (p != MAP_FAILED) {
            g_arena_base = reinterpret_cast<uintptr_t>(p);
            g_guest_spans.push_back({g_arena_base, g_arena_base + ArenaSize});
            std::memset(p, 0, ArenaSize);
            return true;
        }
    }
    return false;
}

void EnsureArenaChunk() {
    if (g_chunk_bound || !g_chunk_alloc || !g_arena_base) {
        return;
    }
    const int fd = g_chunk_alloc(0, ArenaSize);
    if (fd >= 0) {
        void* p = mmap(reinterpret_cast<void*>(g_arena_base), ArenaSize, PROT_READ | PROT_WRITE,
                       MAP_SHARED | MAP_FIXED, fd, 0);
        assert(p != MAP_FAILED);
        std::memset(p, 0, ArenaSize);
        close(fd);
        g_chunk_bound = true;
    } else if (fd == -3) {
        g_chunk_bound = true;
    }
}

void OnSegvRecover(int sig, siginfo_t* info, void* ctx) {
    if (bbgpu_handle_fault(ctx, info->si_addr)) {
        return;
    }
    std::fprintf(stderr, "Unhandled SIGSEGV (sig=%d) at %p\n", sig, info->si_addr);
    _exit(91);
}

void InstallFaultRecovery() {
    struct sigaction sa{};
    sa.sa_sigaction = OnSegvRecover;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    assert(sigaction(SIGSEGV, &sa, nullptr) == 0);
}
} // namespace

extern "C" {
__thread sigjmp_buf* runtime_fault_recover = nullptr;
uint64_t runtime_disabled_optimizations = 0;
uint64_t runtime_experiment_bits = 0;
uint64_t runtime_tsc_frequency() {
    return 1000000000;
}
int runtime_file_translate(const char*, char*, size_t) {
    return -1;
}
uint64_t runtime_memory_clamp(uintptr_t addr, uint64_t size) {
    if (const auto* s = FindGuestSpan(addr)) {
        return std::min<uint64_t>(size, s->end - addr);
    }
    return 0;
}
uint64_t runtime_process_time_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
int runtime_memory_region(uintptr_t addr, uintptr_t* start, uintptr_t* end, int* mapped) {
    if (const auto* s = FindGuestSpan(addr)) {
        if (start)
            *start = s->start;
        if (end)
            *end = s->end;
        if (mapped)
            *mapped = 1;
        return 1;
    }
    return 0;
}
int runtime_memory_is_mapped(uintptr_t addr, uint64_t size) {
    const auto* s = FindGuestSpan(addr);
    return s && addr + size <= s->end ? 1 : 0;
}
void runtime_memory_set_gpu_hooks(void (*)(uintptr_t, uint64_t), void (*)(uintptr_t, uint64_t),
                                  void (*)(uintptr_t, uint64_t)) {}
void runtime_thread_attach_host(const char*) {}
void runtime_memory_gpu_protect(uintptr_t, uint64_t, int, int) {}
void runtime_restart() {
    std::abort();
}
uint64_t runtime_process_time_counter() {
    return runtime_process_time_us() * 1000;
}
int32_t* runtime_errno() {
    static int32_t err = 0;
    return &err;
}
int runtime_memory_write_backing(uintptr_t addr, const void* data, uint64_t size) {
    if (runtime_memory_is_mapped(addr, size) && data) {
        std::memcpy(reinterpret_cast<void*>(addr), data, size);
        return 0;
    }
    return -1;
}
void runtime_memory_read_backing(uintptr_t addr, void* data, uint64_t size) {
    if (runtime_memory_is_mapped(addr, size) && data) {
        std::memcpy(data, reinterpret_cast<const void*>(addr), size);
    } else if (data && size) {
        std::memset(data, 0, size);
    }
}
const uint64_t* runtime_memory_generation() {
    static const uint64_t generation = 1;
    return &generation;
}
void runtime_memory_note_write(uintptr_t, uint64_t) {}
void runtime_memory_set_note_write_hook(void (*)(uintptr_t, uint64_t)) {}
void runtime_memory_set_cpu_write_hook(void (*)(uintptr_t, uint64_t)) {}
void runtime_memory_set_write_watch(uintptr_t, uint64_t, int) {}
void runtime_memory_set_guest_chunk_allocator(int (*fn)(uint64_t, uint64_t)) {
    g_chunk_alloc = fn;
}
void runtime_memory_set_guest_chunk_free(void (*)(uint64_t, uint64_t)) {}
void runtime_memory_set_guest_chunk_private(int (*)(uint64_t, uint64_t, void*)) {}
void runtime_memory_set_guest_chunk_whole(int) {}
void runtime_memory_set_guest_lazy(int) {}
void runtime_memory_set_host_only_allocations(uint64_t) {}
int runtime_memory_vma_info(uintptr_t addr, int* prot, int* type, uintptr_t* end) {
    if (const auto* s = FindGuestSpan(addr)) {
        if (prot)
            *prot = 0x3;
        if (type)
            *type = 1;
        if (end)
            *end = s->end;
        return 1;
    }
    return 0;
}
void runtime_memory_trap(uintptr_t, uint64_t, unsigned, int) {}
unsigned runtime_memory_trap_reasons(uintptr_t) {
    return 0;
}
int runtime_memory_direct_phys(uintptr_t addr, uint64_t* phys, uintptr_t* end) {
    if (g_arena_base && addr >= g_arena_base && addr < g_arena_base + ArenaSize) {
        if (phys)
            *phys = addr - g_arena_base;
        if (end)
            *end = g_arena_base + ArenaSize;
        return 1;
    }
    return 0;
}
int runtime_extra_direct_map(uint64_t, uint64_t, void**) {
    return -1;
}
void* runtime_memory_backing_pointer(uint64_t phys) {
    return g_arena_base && phys < ArenaSize ? reinterpret_cast<void*>(g_arena_base + phys)
                                            : nullptr;
}
uintptr_t runtime_memory_resolve(const char*) {
    return 0;
}
void runtime_wait_report(double) {}
void runtime_sleep_stats(uint64_t* calls, uint64_t* ns) {
    if (calls)
        *calls = 0;
    if (ns)
        *ns = 0;
}
uint64_t runtime_heap_growths() {
    return 0;
}
void* runtime_low_map(size_t, int) {
    return nullptr;
}
const char* runtime_file_user_dir() {
    return g_user_dir_str.c_str();
}
int runtime_memory_commit(uint64_t, uint64_t) {
    return -1;
}
int runtime_memory_decommit(uint64_t, uint64_t) {
    return -1;
}
int runtime_memory_deferred(uint64_t*, uint64_t*) {
    return 0;
}
int runtime_memory_pool_fd() {
    return g_pool_fd;
}
void runtime_memory_commit_ahead(uint64_t, uint64_t) {}
void runtime_memory_discard_direct(uint64_t, uint64_t) {}
void runtime_memory_note_pinned(uint64_t, uint64_t) {}
void runtime_memory_set_grow(const uint64_t*, uint64_t) {}
void runtime_memory_trap_stats(uint64_t* w, uint64_t* a, uint64_t* c, uint64_t* b) {
    if (w)
        *w = 0;
    if (a)
        *a = 0;
    if (c)
        *c = 0;
    if (b)
        *b = 0;
}
int runtime_savedata_active_slot(char* dir, size_t) {
    if (dir)
        dir[0] = 0;
    return -1;
}
int runtime_pad_prompt_device() {
    return 0;
}
unsigned runtime_pad_prompt_generation() {
    return 0;
}
int runtime_pad_mouse_look() {
    return 0;
}
int runtime_pad_prompt(const char*, int, char*, int) {
    return 0;
}
int runtime_pad_is_nintendo() {
    return 0;
}
int runtime_file_host_path(const char*, char*, size_t) {
    return -1;
}
void runtime_re_watch(uint64_t) {}
}

// ---------------------------------------------------------------------------
// Measurement helpers (proc io, quantiles, rusage, cache staging)
// ---------------------------------------------------------------------------
namespace {
using Clock = std::chrono::steady_clock;

double MsSince(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

struct ProcIo {
    u64 rchar = 0;
    u64 syscr = 0;
    u64 read_bytes = 0;
};

ProcIo ReadProcIo() {
    ProcIo io{};
    std::ifstream ifs("/proc/self/io");
    std::string key;
    u64 val = 0;
    while (ifs >> key >> val) {
        if (key == "rchar:")
            io.rchar = val;
        else if (key == "syscr:")
            io.syscr = val;
        else if (key == "read_bytes:")
            io.read_bytes = val;
    }
    return io;
}

struct TaskSample {
    u32 sched_idle_seen = 0;
    std::vector<std::string> preload_names;
};

void SampleTasks(TaskSample& out) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/proc/self/task", ec)) {
        const pid_t tid = static_cast<pid_t>(std::strtol(entry.path().filename().c_str(), nullptr, 10));
        if (tid <= 0 || tid == gettid()) {
            continue;
        }
        std::string comm;
        {
            std::ifstream f(entry.path() / "comm");
            std::getline(f, comm);
        }
        const int policy = sched_getscheduler(tid);
        if (policy == SCHED_IDLE) {
            ++out.sched_idle_seen;
        }
        if (comm.find("Preload") != std::string::npos || comm.find("Pipeline") != std::string::npos) {
            if (std::find(out.preload_names.begin(), out.preload_names.end(), comm) ==
                out.preload_names.end()) {
                out.preload_names.push_back(comm);
            }
        }
    }
}

struct StatsSummary {
    double avg = 0, p50 = 0, p95 = 0, p99 = 0, max = 0;
};

StatsSummary SummarizeMs(std::vector<double> samples) {
    assert(!samples.empty());
    std::sort(samples.begin(), samples.end());
    const double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    const auto at = [&](double q) {
        const size_t idx = std::min<size_t>(samples.size() - 1, size_t(q * (samples.size() - 1)));
        return samples[idx];
    };
    return {
        .avg = sum / double(samples.size()),
        .p50 = at(0.50),
        .p95 = at(0.95),
        .p99 = at(0.99),
        .max = samples.back(),
    };
}

void CopyCacheToTemp(const fs::path& src_cache, const fs::path& dst_user, bool cold_driver) {
    const fs::path dst_cache = dst_user / "cache" / "CUSA03173";
    std::error_code ec;
    fs::remove_all(dst_user, ec);
    fs::create_directories(dst_cache, ec);
    if (!fs::exists(src_cache, ec)) {
        return;
    }
    for (const auto& e : fs::directory_iterator(src_cache, ec)) {
        if (!e.is_regular_file()) {
            continue;
        }
        const std::string name = e.path().filename().string();
        if (name.starts_with("grimoire_")) {
            continue;
        }
        if (cold_driver && name == "pipeline_driver.cache") {
            continue;
        }
        fs::copy_file(e.path(), dst_cache / name, fs::copy_options::overwrite_existing, ec);
    }
}

struct ShaderFixture {
    BbShaderCapture::Input vs;
    BbShaderCapture::Input ps;
    bool mapped = false;
};

std::vector<ShaderFixture> LoadShaderPairs(const fs::path& cache_dir, size_t max_pairs) {
    std::vector<fs::path> paths;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(cache_dir, ec)) {
        if (entry.path().extension() == ".in") {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());

    std::vector<BbShaderCapture::Input> vss, pss;
    std::unordered_set<u64> seen_pgms;
    for (const auto& path : paths) {
        std::ifstream f(path, std::ios::binary);
        std::vector<u8> bytes((std::istreambuf_iterator<char>(f)), {});
        BbShaderCapture::Input in;
        if (!BbShaderCapture::ReadInput(bytes, in, false) || in.code.size() < 2) {
            continue;
        }
        if (in.code[0] != 0xBEEB03FFu || in.code[1] >= 0x2000u ||
            size_t(in.code[1] + 1) * 2 < in.code.size() || (in.pgm_base & 255u) != 0 ||
            in.pgm_base == 0 || seen_pgms.contains(in.pgm_base)) {
            continue;
        }
        const bool want_vs = in.hw_stage == Shader::HwStage::Vertex && vss.size() < max_pairs;
        const bool want_ps = in.hw_stage == Shader::HwStage::Fragment && pss.size() < max_pairs;
        if (!want_vs && !want_ps) {
            continue;
        }
        const size_t header_off = size_t(in.code[1] + 1) * 8;
        if (!MapFixedRegion(in.pgm_base, header_off + sizeof(Gcn::BinaryInfo))) {
            continue;
        }
        bool regions_ok = true;
        for (const auto& r : in.regions) {
            if (r.bytes.empty() || !MapFixedRegion(r.address, r.bytes.size())) {
                regions_ok = false;
                break;
            }
        }
        if (!regions_ok) {
            continue;
        }
        std::memset(reinterpret_cast<void*>(in.pgm_base), 0,
                    header_off + sizeof(Gcn::BinaryInfo));
        std::memcpy(reinterpret_cast<void*>(in.pgm_base), in.code.data(),
                    in.code.size() * sizeof(u32));
        Gcn::BinaryInfo bi{};
        bi.signature = Gcn::BinaryInfo::signature_ref;
        bi.length = static_cast<u32>(in.code.size() * sizeof(u32));
        bi.shader_hash = in.pgm_hash;
        std::memcpy(reinterpret_cast<void*>(in.pgm_base + header_off), &bi, sizeof(bi));
        for (const auto& r : in.regions) {
            std::memcpy(reinterpret_cast<void*>(r.address), r.bytes.data(), r.bytes.size());
        }
        seen_pgms.insert(in.pgm_base);
        if (want_vs) {
            vss.push_back(std::move(in));
        } else {
            pss.push_back(std::move(in));
        }
        if (vss.size() >= max_pairs && pss.size() >= max_pairs) {
            break;
        }
    }
    std::vector<ShaderFixture> pairs;
    const size_t n = std::min(vss.size(), pss.size());
    for (size_t i = 0; i < n; ++i) {
        pairs.push_back({std::move(vss[i]), std::move(pss[i]), true});
    }
    return pairs;
}

void ConfigureStageRegs(Gcn::Regs& regs, const ShaderFixture& pair, u32 num_cb,
                        u32 variant_idx) {
    regs.SetDefaults();
    const auto apply_props = [](Gcn::ShaderProgram& prog,
                                const Shader::RuntimeInfo::Properties& props, u64 pgm_base,
                                const std::array<u32, 16>& user_data) {
        prog.address = pgm_base >> 8;
        prog.settings = {};
        prog.settings.num_vgprs = props.num_allocated_vgprs ? (props.num_allocated_vgprs / 4 - 1) : 0;
        prog.settings.fp_round_mode32 = props.fp_round_mode32;
        prog.settings.fp_round_mode64 = props.fp_round_mode16_64;
        prog.settings.fp_denorm_mode32 = props.fp_denorm_mode32;
        prog.settings.fp_denorm_mode64 = props.fp_denorm_mode16_64;
        prog.settings.vgpr_comp_cnt = props.num_input_vgprs;
        prog.settings.num_user_regs = props.num_user_data;
        prog.user_data = user_data;
    };

    regs.primitive_type = Gcn::PrimitiveType::TriangleList;
    regs.stage_enable.raw = Gcn::ShaderStageEnable::VgtStages::Vs;
    apply_props(regs.vs_program, pair.vs.runtime_info.props, pair.vs.pgm_base, pair.vs.user_data);
    apply_props(regs.ps_program, pair.ps.runtime_info.props, pair.ps.pgm_base, pair.ps.user_data);

    const auto& vs_sw = pair.vs.runtime_info.sw.vs;
    regs.vgt_instance_step_rate_0 = vs_sw.step_rate_0;
    regs.vgt_instance_step_rate_1 = vs_sw.step_rate_1;
    const auto& vs_hw = pair.vs.runtime_info.hw.vs;
    regs.clipper_control.user_clip_plane_enable = vs_hw.user_clip_plane_mask;
    regs.clipper_control.clip_disable = (num_cb >= 5) ? 0 : (vs_hw.clip_disable ? 1 : 0);

    const auto& fs_hw = pair.ps.runtime_info.hw.fs;
    regs.ps_input_ena = fs_hw.en_flags;
    regs.ps_input_addr = fs_hw.addr_flags;
    regs.barycentric_control.front_face_all_bits = fs_hw.front_face_all_bits;
    regs.num_interp = fs_hw.num_inputs;
    for (u32 i = 0; i < fs_hw.num_inputs && i < regs.ps_inputs.size(); ++i) {
        regs.ps_inputs[i].input_offset = fs_hw.inputs[i].param_index;
        regs.ps_inputs[i].use_default = fs_hw.inputs[i].is_default;
        regs.ps_inputs[i].flat_shade = fs_hw.inputs[i].is_flat;
        regs.ps_inputs[i].default_value = fs_hw.inputs[i].default_value;
    }
    regs.z_export_format = fs_hw.z_export_format;
    regs.depth_shader_control.z_export_enable = fs_hw.mrtz_mask & 1u;
    regs.depth_shader_control.stencil_test_val_export_enable = (fs_hw.mrtz_mask >> 1) & 1u;
    regs.depth_shader_control.mask_export_enable = (fs_hw.mrtz_mask >> 2) & 1u;
    regs.depth_shader_control.coverage_to_mask_enable = (fs_hw.mrtz_mask >> 3) & 1u;

    regs.color_control.mode = Gcn::ColorControl::OperationMode::Normal;
    regs.color_control.rop3 =
        static_cast<Gcn::ColorControl::LogicOp>(variant_idx & 1 ? 0xcc : 0x55);
    regs.color_shader_mask.raw = 0;
    regs.color_target_mask.raw = 0;
    regs.color_export_format.raw = 0;
    for (u32 cb = 0; cb < 7; ++cb) {
        const auto& src_cb = fs_hw.color_buffers[cb];
        if (cb >= num_cb && src_cb.data_format == Gcn::DataFormat::FormatInvalid) {
            continue;
        }
        regs.color_buffers[cb].base_address = u32((g_arena_base + 0x100000 + cb * 0x100000) >> 8);
        regs.color_buffers[cb].pitch.tile_max = 31; // 256 px
        regs.color_buffers[cb].slice.tile_max = 1023;
        regs.color_buffers[cb].info.format = u32(Gcn::DataFormat::Format8_8_8_8);
        regs.color_buffers[cb].info.number_type = u32(Gcn::NumberFormat::Unorm);
        const u32 exp_fmt =
            src_cb.export_format != Gcn::ShaderExportFormat::Zero ? u32(src_cb.export_format) : 4u;
        regs.color_export_format.raw |= exp_fmt << (cb * 4);
        regs.color_shader_mask.raw |= 0xfu << (cb * 4);
        regs.color_target_mask.raw |= 0xfu << (cb * 4);
        regs.blend_control[cb].enable = (variant_idx >> 1) & 1;
        regs.blend_control[cb].color_src_factor = Gcn::BlendControl::BlendFactor::One;
        regs.blend_control[cb].color_dst_factor = Gcn::BlendControl::BlendFactor::Zero;
        regs.blend_control[cb].color_func = Gcn::BlendControl::BlendFunc::Add;
    }
    regs.depth_buffer.z_info.format = Gcn::DepthBuffer::ZFormat::Z32Float;
    regs.depth_buffer.z_read_base = u32((g_arena_base + 0x900000) >> 8);
    regs.depth_buffer.z_write_base = regs.depth_buffer.z_read_base;
    regs.depth_buffer.depth_size.pitch_tile_max = 31;
    regs.depth_buffer.depth_size.height_tile_max = 31;
    regs.depth_buffer.depth_slice.tile_max = 1023;
    regs.depth_control.depth_enable = 1;
    regs.depth_control.depth_write_enable = 1;
    regs.depth_control.depth_func = Gcn::CompareFunc::Less;
    regs.polygon_control.cull_back = (variant_idx >> 2) & 1;
    regs.polygon_control.cull_front = (variant_idx >> 3) & 1;
    regs.viewports[0] = {.xscale = 128.0f, .xoffset = 128.0f, .yscale = 128.0f, .yoffset = 128.0f,
                         .zscale = 1.0f, .zoffset = 0.0f};
    regs.viewport_scissors[0].bottom_right_x = 256;
    regs.viewport_scissors[0].bottom_right_y = 256;
    regs.screen_scissor.bottom_right_x = 256;
    regs.screen_scissor.bottom_right_y = 256;
    regs.window_scissor.bottom_right_x = 256;
    regs.window_scissor.bottom_right_y = 256;
    regs.generic_scissor.bottom_right_x = 256;
    regs.generic_scissor.bottom_right_y = 256;
    regs.num_indices = 3;
    regs.num_instances.num_instances = 1;
    regs.index_buffer_type.index_type = Gcn::IndexType::Index16;
    const VAddr idx_addr = g_arena_base + 0x4000;
    regs.index_base_address.base_addr_lo = u32(idx_addr);
    regs.index_base_address.base_addr_hi = u32(idx_addr >> 32);
}

// ---------------------------------------------------------------------------
// Suites
// ---------------------------------------------------------------------------
void RunWarmUpAndProgramSuite(const fs::path& src_cache, const fs::path& temp_user,
                              bool cold_driver, bool run_warmup, bool run_program) {
    CopyCacheToTemp(src_cache, temp_user, cold_driver);
    const fs::path temp_cache = temp_user / "cache" / "CUSA03173";

    Vulkan::Instance instance(0, false);
    std::printf("Device: %s (Driver %#x)\n", instance.GetProperties().deviceName.data(),
                instance.GetProperties().driverVersion);
    Vulkan::Scheduler scheduler(instance);
    BbCp::CommandProcessor liverpool;
    Vulkan::PipelineCache pc(instance, scheduler, &liverpool, 16);

    if (run_warmup) {
        std::atomic<bool> stop_sample{false};
        TaskSample samples;
        std::thread sampler([&] {
            while (!stop_sample.load(std::memory_order_relaxed)) {
                SampleTasks(samples);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });

        const ProcIo io0 = ReadProcIo();
        const auto t0 = Clock::now();
        pc.Preload();
        const auto t1 = Clock::now();
        const ProcIo io1 = ReadProcIo();
        pc.Sync();
        const auto t2 = Clock::now();
        stop_sample.store(true, std::memory_order_relaxed);
        sampler.join();

        std::printf("[SUITE 1: WarmUp + QueuePreloaded] (%s driver cache)\n",
                    cold_driver ? "COLD" : "WARM");
        std::printf("  preload_ms:     %.2f ms\n", MsSince(t0, t1));
        std::printf("  total_sync_ms:  %.2f ms\n", MsSince(t0, t2));
        std::printf("  io_rchar_mb:    %.2f MiB\n", double(io1.rchar - io0.rchar) / (1024.0 * 1024.0));
        std::printf("  io_syscr:       %llu\n", (unsigned long long)(io1.syscr - io0.syscr));
        std::printf("  sched_idle_obs: %u (worker names seen: %zu)\n", samples.sched_idle_seen,
                    samples.preload_names.size());
    }

    if (!run_program) {
        return;
    }

    const auto pairs = LoadShaderPairs(temp_cache, 4);
    if (pairs.empty()) {
        std::printf("[SUITE 2: Program/Layouts] SKIPPED (no .in captures found in %s)\n",
                    temp_cache.c_str());
        return;
    }

    // 2a. GetGraphicsPipeline + unique VkPipelineLayout handles
    std::unordered_set<VkPipelineLayout> unique_layouts;
    u32 pipelines_built = 0;
    const auto t_pipe0 = Clock::now();
    for (size_t p = 0; p < pairs.size(); ++p) {
        for (u32 v = 0; v < 8; ++v) {
            ConfigureStageRegs(liverpool.regs, pairs[p], 1, v);
            if (const auto* pipe = pc.GetGraphicsPipeline()) {
                ++pipelines_built;
                unique_layouts.insert(VkPipelineLayout(pipe->GetLayout()));
            }
        }
    }
    const auto t_pipe1 = Clock::now();
    std::printf("[SUITE 2a: Pipeline Creation & Layout Sharing]\n");
    std::printf("  shader_pairs:           %zu\n", pairs.size());
    std::printf("  pipelines_built:        %u\n", pipelines_built);
    std::printf("  unique_pipeline_layouts:%zu\n", unique_layouts.size());
    std::printf("  total_create_ms:        %.2f ms (%.1f us/pipeline)\n", MsSince(t_pipe0, t_pipe1),
                pipelines_built ? MsSince(t_pipe0, t_pipe1) * 1000.0 / pipelines_built : 0.0);

    // 2b. PrepareGraphicsPipeline + LookUpProgram (warm hit allocs + motion_possible + MT stress)
    const auto run_prepare_bench = [&](const char* label, bool enable_motion, u32 iters) {
        Shader::MotionVectors::positions_address = g_arena_base + 0x10000;
        BbSettings::Get().upscaler.store(enable_motion ? BbSettings::UpscalerFsr3
                                                       : BbSettings::UpscalerOff);
        ConfigureStageRegs(liverpool.regs, pairs[0], enable_motion ? 5 : 1, 0);

        // Warm once outside timed/counted section so all shaders/perms exist in cache
        (void)pc.GetGraphicsPipeline();

        Vulkan::PipelineSelection wsel{};
        Vulkan::PrepWorker pw{};
        wsel.regs = &liverpool.regs;
        wsel.worker = &pw;
        u32 ok_count = 0;
        if (pc.PrepareGraphicsPipeline(wsel)) {
            ++ok_count;
        }
        pw.infos.clear();
        pw.stages.clear();
        pw.failed = false;

        g_alloc_calls.store(0, std::memory_order_relaxed);
        g_alloc_bytes.store(0, std::memory_order_relaxed);
        const struct mallinfo2 m0 = mallinfo2();
        g_count_allocs.store(true, std::memory_order_release);
        const auto t0 = Clock::now();
        for (u32 i = 0; i < iters; ++i) {
            pw.infos.clear();
            pw.stages.clear();
            pw.failed = false;
            wsel.fetch_shader.reset();
            wsel.motion = false;
            if (pc.PrepareGraphicsPipeline(wsel)) {
                ++ok_count;
            }
        }
        const auto t1 = Clock::now();
        g_count_allocs.store(false, std::memory_order_release);
        const struct mallinfo2 m1 = mallinfo2();

        const u64 allocs = g_alloc_calls.load(std::memory_order_relaxed);
        const u64 abytes = g_alloc_bytes.load(std::memory_order_relaxed);
        const double ns_per_call = MsSince(t0, t1) * 1e6 / iters;
        std::printf("[SUITE 2b: PrepareGraphicsPipeline (%s)]\n", label);
        std::printf("  iters:        %u (ok=%u)\n", iters, ok_count);
        std::printf("  ns_per_call:  %.1f ns\n", ns_per_call);
        std::printf("  new_calls:    %llu (%.2f/call), new_bytes: %llu (mallinfo_delta=%lld)\n",
                    (unsigned long long)allocs, double(allocs) / iters, (unsigned long long)abytes,
                    (long long)(m1.uordblks - m0.uordblks));
    };

    run_prepare_bench("motion=off", false, 20000);
    run_prepare_bench("motion_possible", true, 10000);

    // 2c. Multi-threaded PrepareGraphicsPipeline stress (exercises Program::last_used atomic)
    ConfigureStageRegs(liverpool.regs, pairs[0], 1, 0);
    std::vector<std::thread> workers;
    std::atomic<u32> mt_ok{0};
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&] {
            Gcn::Regs local_regs = liverpool.regs;
            Vulkan::PipelineSelection wsel{};
            Vulkan::PrepWorker pw{};
            wsel.regs = &local_regs;
            wsel.worker = &pw;
            u32 local_ok = 0;
            for (int i = 0; i < 4000; ++i) {
                pw.infos.clear();
                pw.stages.clear();
                pw.failed = false;
                wsel.fetch_shader.reset();
                if (pc.PrepareGraphicsPipeline(wsel)) {
                    ++local_ok;
                }
            }
            mt_ok.fetch_add(local_ok, std::memory_order_relaxed);
        });
    }
    for (auto& w : workers) {
        w.join();
    }
    std::printf("[SUITE 2c: Concurrent LookUpProgram] 4 threads x 4000 ok=%u\n", mt_ok.load());
}

struct CoreCaches {
    Vulkan::Instance& instance;
    Vulkan::Scheduler scheduler;
    Vulkan::Runtime runtime;
    BbCp::CommandProcessor liverpool;
    BbMem::PageWatch page_watch{nullptr};
    VideoCore::BufferCache buffer_cache;
    VideoCore::TextureCache texture_cache;

    explicit CoreCaches(Vulkan::Instance& inst)
        : instance{inst}, scheduler{inst}, runtime{inst, scheduler},
          buffer_cache{inst, scheduler, runtime, &liverpool, texture_cache, page_watch},
          texture_cache{inst, scheduler, runtime, &liverpool, buffer_cache, page_watch} {}
};

void RunGpuBarriersAndGc(Vulkan::Instance& instance, bool run_gpu, bool run_gc) {
    BbGuestMemory::Install(instance);
    EnsureArenaChunk();
    auto caches = std::make_unique<CoreCaches>(instance);
    auto& scheduler = caches->scheduler;
    auto& runtime = caches->runtime;
    auto& buffer_cache = caches->buffer_cache;
    auto& texture_cache = caches->texture_cache;

    if (run_gpu) {
        Vulkan::GpuTimestamps timestamps(instance, scheduler, buffer_cache);
        Vulkan::OcclusionQueries occlusion(instance, scheduler, buffer_cache);
        scheduler.SetCarriedScope(&occlusion);

        const auto device = instance.GetDevice();
        const auto allocator = instance.GetAllocator();
        constexpr u32 W = 256, H = 256;
        VideoCore::HostImage color_img(device, allocator);
        color_img.Create({
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eR8G8B8A8Unorm,
            .extent = {W, H, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc,
            .initialLayout = vk::ImageLayout::eUndefined,
        });
        VideoCore::HostImage depth_img(device, allocator);
        depth_img.Create({
            .imageType = vk::ImageType::e2D,
            .format = vk::Format::eD32Sfloat,
            .extent = {W, H, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
                     vk::ImageUsageFlagBits::eTransferDst,
            .initialLayout = vk::ImageLayout::eUndefined,
        });

        static vk::detail::DynamicLoader loader;
        vk::detail::DispatchLoaderDynamic dispatch;
        dispatch.init(loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr"));
        dispatch.init(instance.GetInstance());
        dispatch.init(device);

        const auto [r_cv, color_view] = device.createImageView(
            {
                .image = vk::Image(color_img),
                .viewType = vk::ImageViewType::e2D,
                .format = vk::Format::eR8G8B8A8Unorm,
                .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            nullptr, dispatch);
        assert(r_cv == vk::Result::eSuccess);
        const auto [r_dv, depth_view] = device.createImageView(
            {
                .image = vk::Image(depth_img),
                .viewType = vk::ImageViewType::e2D,
                .format = vk::Format::eD32Sfloat,
                .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
            },
            nullptr, dispatch);
        assert(r_dv == vk::Result::eSuccess);

        // Transition attachments once before frame loop
        {
            const auto cmd = scheduler.CommandBuffer();
            const std::array barriers{
                vk::ImageMemoryBarrier2{
                    .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                    .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                    .oldLayout = vk::ImageLayout::eUndefined,
                    .newLayout = vk::ImageLayout::eGeneral,
                    .image = vk::Image(color_img),
                    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                },
                vk::ImageMemoryBarrier2{
                    .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                    .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests,
                    .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                    .oldLayout = vk::ImageLayout::eUndefined,
                    .newLayout = vk::ImageLayout::eGeneral,
                    .image = vk::Image(depth_img),
                    .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
                },
            };
            cmd.pipelineBarrier2({.imageMemoryBarrierCount = u32(barriers.size()),
                                  .pImageMemoryBarriers = barriers.data()},
                                 dispatch);
            scheduler.Finish();
        }

        // Scratch 1 MiB device buffer to generate real GPU fill/copy work alongside queries
        VideoCore::Buffer work_buf(instance, 0, 1ull << 20, VideoCore::MemoryType::DeviceLocal,
                                   "PerfWorkBuf");
        VkBuffer staging{};
        VmaAllocation staging_alloc{};
        VmaAllocationInfo staging_ai{};
        const VkBufferCreateInfo sbi{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                     .size = 4096,
                                     .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
        const VmaAllocationCreateInfo sac{.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
                                                 VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
                                          .usage = VMA_MEMORY_USAGE_AUTO,
                                          .requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        assert(vmaCreateBuffer(allocator, &sbi, &sac, &staging, &staging_alloc, &staging_ai) ==
               VK_SUCCESS);

        Vulkan::RenderState rstate{};
        rstate.width = W;
        rstate.height = H;
        rstate.num_layers = 1;
        rstate.num_color_attachments = 1;
        rstate.color_attachments[0].image_view = color_view;
        rstate.color_attachments[0].image_layout = vk::ImageLayout::eGeneral;
        rstate.depth_stencil_attachment.image_view = depth_view;
        rstate.depth_stencil_attachment.image_layout = vk::ImageLayout::eGeneral;
        rstate.depth_stencil_attachment.has_depth = true;

        const VAddr ts_base = g_arena_base + 0x20000;
        const VAddr occ_base = g_arena_base + 0x30000;
        // Clean dirty-tracking bits once before the timed loop so readback ObtainBuffer won't re-upload zeros.
        (void)buffer_cache.ObtainBuffer(ts_base, 4096, false);
        (void)buffer_cache.ObtainBuffer(occ_base, 4096, false);
        scheduler.Finish();

        constexpr u32 WarmupFrames = 30;
        constexpr u32 MeasureFrames = 300;
        constexpr u32 EventsPerFrame = 16;

        std::vector<double> frame_ms;
        frame_ms.reserve(MeasureFrames);
        for (u32 f = 0; f < WarmupFrames + MeasureFrames; ++f) {
            const auto t0 = Clock::now();
            for (u32 e = 0; e < EventsPerFrame; ++e) {
                scheduler.EndRendering();
                runtime.FillBuffer(&work_buf, (e & 7) * 65536u, 65536u, f + e);
                scheduler.BeginRendering(rstate);
                const VAddr occ_pair = occ_base + u64(e) * 16;
                occlusion.Event(occ_pair, 4);
                occlusion.Event(occ_pair + 8, 4);
                occlusion.WriteBeforeLabel();
                timestamps.Write(ts_base + u64(e) * 8, true);
            }
            scheduler.EndRendering();
            scheduler.Flush();
            scheduler.Finish();
            const auto t1 = Clock::now();
            if (f >= WarmupFrames) {
                frame_ms.push_back(MsSince(t0, t1));
            }
        }

        // Read back timestamps & occlusion counter from last frame to verify correctness
        {
            scheduler.EndRendering();
            const auto [ts_buf, ts_off] = buffer_cache.ObtainBuffer(ts_base, EventsPerFrame * 8, false);
            const auto [occ_buf, occ_off] = buffer_cache.ObtainBuffer(occ_base, 16, false);
            scheduler.Record([=, sb = vk::Buffer(staging), src_ts = ts_buf->Handle(),
                              src_occ = occ_buf->Handle()](vk::CommandBuffer cmd) {
                const vk::BufferCopy c0{ts_off, 0, EventsPerFrame * 8};
                const vk::BufferCopy c1{occ_off, 1024, 16};
                cmd.copyBuffer(src_ts, sb, 1, &c0, dispatch);
                cmd.copyBuffer(src_occ, sb, 1, &c1, dispatch);
            });
            scheduler.Finish();
        }
        const auto* ts_vals = static_cast<const u64*>(staging_ai.pMappedData);
        const auto* occ_vals = reinterpret_cast<const u64*>(static_cast<const u8*>(staging_ai.pMappedData) + 1024);
        bool ts_monotonic = !timestamps.Supported() || ts_vals[0] != 0;
        if (timestamps.Supported()) {
            for (u32 i = 1; i < EventsPerFrame; ++i) {
                if (ts_vals[i] < ts_vals[i - 1]) {
                    ts_monotonic = false;
                }
            }
        }
        const bool occ_valid = (occ_vals[1] & (1ull << 63)) != 0;
        if (!ts_monotonic || !occ_valid) {
            std::fprintf(stderr, "Suite 3 check failed: ts0=%#llx ts1=%#llx ts15=%#llx occ0=%#llx occ1=%#llx\n",
                         (unsigned long long)ts_vals[0], (unsigned long long)ts_vals[1],
                         (unsigned long long)ts_vals[EventsPerFrame - 1],
                         (unsigned long long)occ_vals[0], (unsigned long long)occ_vals[1]);
        }
        assert(ts_monotonic);
        assert(occ_valid);
        assert(timestamps.Written() >= u64(WarmupFrames + MeasureFrames) * EventsPerFrame);
        assert(occlusion.Events() >= u64(WarmupFrames + MeasureFrames) * EventsPerFrame * 2);

        const auto s = SummarizeMs(frame_ms);
        std::printf("[SUITE 3: GPU Barriers (GpuTimestamps + OcclusionQueries)]\n");
        std::printf("  frames: %u x (%u timestamps + %u occlusion resolves)\n", MeasureFrames,
                    EventsPerFrame, EventsPerFrame);
        std::printf("  frame_ms: avg=%.4f p50=%.4f p95=%.4f p99=%.4f max=%.4f\n", s.avg, s.p50,
                    s.p95, s.p99, s.max);
        std::printf("  correctness: ts_monotonic=%s occ_valid_bit=%s\n",
                    ts_monotonic ? "OK" : "FAIL", occ_valid ? "OK" : "FAIL");

        vmaDestroyBuffer(allocator, staging, staging_alloc);
        device.destroyImageView(color_view, nullptr, dispatch);
        device.destroyImageView(depth_view, nullptr, dispatch);
    }

    if (run_gc) {
        constexpr u32 GcIters = 5000;
        struct rusage ru0{}, ru1{};
        getrusage(RUSAGE_SELF, &ru0);
        const auto t0 = Clock::now();
        for (u32 i = 0; i < GcIters; ++i) {
            texture_cache.RunGarbageCollector();
        }
        const auto t1 = Clock::now();
        getrusage(RUSAGE_SELF, &ru1);
        const double wall_ms = MsSince(t0, t1);
        const double sys_ms =
            ((ru1.ru_stime.tv_sec - ru0.ru_stime.tv_sec) * 1e6 +
             (ru1.ru_stime.tv_usec - ru0.ru_stime.tv_usec)) /
            1000.0;
        const double usr_ms =
            ((ru1.ru_utime.tv_sec - ru0.ru_utime.tv_sec) * 1e6 +
             (ru1.ru_utime.tv_usec - ru0.ru_utime.tv_usec)) /
            1000.0;
        std::printf("[SUITE 4: TextureCache::RunGarbageCollector]\n");
        std::printf("  iters:    %u\n", GcIters);
        std::printf("  wall_ms:  %.2f ms (%.1f ns/call)\n", wall_ms, wall_ms * 1e6 / GcIters);
        std::printf("  cpu_time: user=%.2f ms sys=%.2f ms\n", usr_ms, sys_ms);
    }
}

struct StageBState {
    Vulkan::Scheduler scheduler;
    Vulkan::Runtime runtime;
    BbCp::CommandProcessor liverpool;
    Vulkan::Rasterizer rasterizer;

    explicit StageBState(Vulkan::Instance& inst)
        : scheduler{inst}, runtime{inst, scheduler},
          rasterizer{inst, scheduler, runtime, &liverpool} {}
};

void RunStageBDrawSuite(Vulkan::Instance& instance) {
    setenv("BB_DRAW_PIPE", "0", 1);
    setenv("BB_DYNSTATE_CHECK", "1", 1);

    BbGuestMemory::Install(instance);
    EnsureArenaChunk();
    auto state = std::make_unique<StageBState>(instance);
    EnsureArenaChunk();
    auto& scheduler = state->scheduler;
    auto& liverpool = state->liverpool;
    auto& rasterizer = state->rasterizer;

    // Seed self-contained index buffer, vertex buffer, V# table, fetch shader, VS, and PS inside
    // g_arena_base so Stage B exercises bindPipeline + setVertexInputEXT/bindVertexBuffers +
    // bindIndexBuffer with zero unmapped external memory accesses.
    const VAddr idx_addr = g_arena_base + 0x4000;
    const VAddr vtx_addr = g_arena_base + 0x5000;
    const VAddr vsharp_addr = g_arena_base + 0x6000;
    const VAddr fetch_addr = g_arena_base + 0x7000;
    const VAddr vs_addr = g_arena_base + 0x8000;
    const VAddr ps_addr = g_arena_base + 0x9000;

    auto* idx = reinterpret_cast<u16*>(idx_addr);
    idx[0] = 0;
    idx[1] = 1;
    idx[2] = 2;

    constexpr std::array<float, 12> kVertices = {
        -0.5f, -0.5f, 0.0f, 1.0f,
         0.5f, -0.5f, 0.0f, 1.0f,
         0.0f,  0.5f, 0.0f, 1.0f,
    };
    std::memcpy(reinterpret_cast<void*>(vtx_addr), kVertices.data(), sizeof(kVertices));

    Gcn::Buffer vsharp{};
    vsharp.base_address = vtx_addr;
    vsharp.stride = 16;
    vsharp.num_records = 3;
    vsharp.dst_sel_x = u32(Gcn::CompSwizzle::Red);
    vsharp.dst_sel_y = u32(Gcn::CompSwizzle::Green);
    vsharp.dst_sel_z = u32(Gcn::CompSwizzle::Blue);
    vsharp.dst_sel_w = u32(Gcn::CompSwizzle::Alpha);
    vsharp.num_format = u32(Gcn::NumberFormat::Float);
    vsharp.data_format = u32(Gcn::DataFormat::Format32_32_32_32);
    std::memcpy(reinterpret_cast<void*>(vsharp_addr), &vsharp, sizeof(vsharp));

    const std::array<u32, 4> kFetchCode = {
        (0b11000u << 27) | (2u << 22) | (8u << 15) | (0u << 9) | (1u << 8) | 0u, // s_load_dwordx4 s[8:11], s[0:1], 0x0
        (0b111010u << 26) | (u32(Gcn::NumberFormat::Float) << 23) |
            (u32(Gcn::DataFormat::Format32_32_32_32) << 19) | (3u << 16) | (1u << 13), // tbuffer_load_format_xyzw w0
        (0u) | (4u << 8) | (2u << 16) | (0x80u << 24),                                   // tbuffer_load_format_xyzw v4..v7
        (0b101111101u << 23) | (32u << 8),                                               // s_setpc_b64 s[0:1]
    };
    std::memcpy(reinterpret_cast<void*>(fetch_addr), kFetchCode.data(), sizeof(kFetchCode));

    constexpr std::array<u32, 8> kVsCode = {
        0xBEEB03FFu, 0x00000003u, // s_mov_b32 vcc_hi, 3 (BinaryInfo follows at +32 B)
        0xBE802102u,              // s_swappc_b64 s[0:1], s[2:3]
        0xF80008CFu, 0x07060504u, // exp pos0, v4, v5, v6, v7 done
        0xF800020Fu, 0x07060504u, // exp param0, v4, v5, v6, v7
        0xBF810000u,              // s_endpgm
    };
    std::memcpy(reinterpret_cast<void*>(vs_addr), kVsCode.data(), sizeof(kVsCode));
    Gcn::BinaryInfo vs_bi{};
    vs_bi.signature = Gcn::BinaryInfo::signature_ref;
    vs_bi.length = static_cast<u32>(sizeof(kVsCode));
    vs_bi.shader_hash = 0x5A7E5B0000000001ULL;
    std::memcpy(reinterpret_cast<void*>(vs_addr + sizeof(kVsCode)), &vs_bi, sizeof(vs_bi));

    constexpr std::array<u32, 8> kPsCode = {
        0xBEEB03FFu, 0x00000003u, // s_mov_b32 vcc_hi, 3 (BinaryInfo follows at +32 B)
        0x7E000280u,              // v_mov_b32 v0, 0
        0x5E000100u,              // v_cvt_pkrtz_f16_f32 v0, v0, v0
        0xBF800000u,              // s_nop
        0xF8001C0Fu, 0x00000000u, // exp mrt0, v0, v0 compr vm done
        0xBF810000u,              // s_endpgm
    };
    std::memcpy(reinterpret_cast<void*>(ps_addr), kPsCode.data(), sizeof(kPsCode));
    Gcn::BinaryInfo ps_bi{};
    ps_bi.signature = Gcn::BinaryInfo::signature_ref;
    ps_bi.length = static_cast<u32>(sizeof(kPsCode));
    ps_bi.shader_hash = 0x5A7E5B0000000002ULL;
    std::memcpy(reinterpret_cast<void*>(ps_addr + sizeof(kPsCode)), &ps_bi, sizeof(ps_bi));

    auto& regs = liverpool.regs;
    regs.SetDefaults();
    regs.primitive_type = Gcn::PrimitiveType::TriangleList;
    regs.stage_enable.raw = Gcn::ShaderStageEnable::VgtStages::Vs;
    regs.vs_program.address = vs_addr >> 8;
    regs.vs_program.settings = {};
    regs.vs_program.settings.num_vgprs = 2;
    regs.vs_program.settings.num_user_regs = 4;
    regs.vs_program.user_data[0] = u32(vsharp_addr);
    regs.vs_program.user_data[1] = u32(vsharp_addr >> 32);
    regs.vs_program.user_data[2] = u32(fetch_addr);
    regs.vs_program.user_data[3] = u32(fetch_addr >> 32);

    regs.ps_program.address = ps_addr >> 8;
    regs.ps_program.settings = {};
    regs.ps_program.settings.num_vgprs = 1;
    regs.ps_input_ena.persp_center_ena = 1;
    regs.ps_input_addr.persp_center_ena = 1;
    regs.num_interp = 0;

    regs.color_control.mode = Gcn::ColorControl::OperationMode::Normal;
    regs.color_control.rop3 = Gcn::ColorControl::LogicOp::Copy;
    regs.color_buffers[0].base_address = u32((g_arena_base + 0x100000) >> 8);
    regs.color_buffers[0].pitch.tile_max = 31; // 256 px
    regs.color_buffers[0].slice.tile_max = 1023;
    regs.color_buffers[0].info.format = u32(Gcn::DataFormat::Format8_8_8_8);
    regs.color_buffers[0].info.number_type = u32(Gcn::NumberFormat::Unorm);
    regs.color_export_format.raw = u32(Gcn::ShaderExportFormat::ABGR_FP16);
    regs.color_shader_mask.raw = 0xFu;
    regs.color_target_mask.raw = 0xFu;

    regs.depth_buffer.z_info.format = Gcn::DepthBuffer::ZFormat::Z32Float;
    regs.depth_buffer.z_read_base = u32((g_arena_base + 0x200000) >> 8);
    regs.depth_buffer.z_write_base = regs.depth_buffer.z_read_base;
    regs.depth_buffer.depth_size.pitch_tile_max = 31;
    regs.depth_buffer.depth_size.height_tile_max = 31;
    regs.depth_buffer.depth_slice.tile_max = 1023;
    regs.depth_control.depth_enable = 1;
    regs.depth_control.depth_write_enable = 1;
    regs.depth_control.depth_func = Gcn::CompareFunc::Less;

    regs.viewports[0] = {.xscale = 128.0f, .xoffset = 128.0f, .yscale = 128.0f, .yoffset = 128.0f,
                         .zscale = 1.0f, .zoffset = 0.0f};
    regs.viewport_scissors[0].bottom_right_x = 256;
    regs.viewport_scissors[0].bottom_right_y = 256;
    regs.screen_scissor.bottom_right_x = 256;
    regs.screen_scissor.bottom_right_y = 256;
    regs.window_scissor.bottom_right_x = 256;
    regs.window_scissor.bottom_right_y = 256;
    regs.generic_scissor.bottom_right_x = 256;
    regs.generic_scissor.bottom_right_y = 256;
    regs.num_indices = 3;
    regs.num_instances.num_instances = 1;
    regs.index_buffer_type.index_type = Gcn::IndexType::Index16;
    regs.index_base_address.base_addr_lo = u32(idx_addr);
    regs.index_base_address.base_addr_hi = u32(idx_addr >> 32);

    // Warm up pipeline once before timed Stage B draw loop
    rasterizer.Draw(true, 0);
    scheduler.Finish();

    constexpr u32 Draws = 3000;
    const auto t0 = Clock::now();
    for (u32 i = 0; i < Draws; ++i) {
        if ((i % 250) == 249) {
            scheduler.EndRendering();
            scheduler.Flush();
        }
        rasterizer.Draw(true, 0);
    }
    scheduler.EndRendering();
    scheduler.Finish();
    const auto t1 = Clock::now();

    std::printf("[SUITE 5: Stage B Consecutive Draw Recording (BB_DYNSTATE_CHECK=1)]\n");
    std::printf("  draws:      %u\n", Draws);
    std::printf("  record_ms:  %.2f ms (%.1f ns/draw)\n", MsSince(t0, t1),
                MsSince(t0, t1) * 1e6 / Draws);
}
} // namespace

int main(int argc, char** argv) {
    std::string suite = "all";
    bool cold_driver = false;
    fs::path src_cache = fs::current_path() / "user" / "cache" / "CUSA03173";
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg.starts_with("--suite=")) {
            suite = std::string(arg.substr(8));
        } else if (arg == "--cold") {
            cold_driver = true;
        } else if (arg.starts_with("--cache=")) {
            src_cache = std::string(arg.substr(8));
        }
    }

    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const fs::path temp_user = fs::temp_directory_path() / "bbport_perf_emp_user";
    g_user_dir_str = temp_user.string();
    setenv("BB_GPU_USER_DIR", g_user_dir_str.c_str(), 1);
    setenv("BB_SHADER_DISCOVERY", "1", 1);
    setenv("BB_GUEST_IN_PLACE", "1", 1);
    setenv("BB_LAYER_MEMORY", "1", 1);
    setenv("BB_GUEST_HOOKS", "0", 1);

    assert(InitGuestArena());
    InstallFaultRecovery();
    Common::ElfInfo::Instance().Set("CUSA03173", "Bloodborne", 0x01700000, 0);
    Game::Select("CUSA03173");

    std::printf("================================================================================\n");
    std::printf("BBPort Empirical Performance & Verification Harness (suite=%s)\n", suite.c_str());
    std::printf("================================================================================\n");

    const bool do_all = suite == "all";
    if (do_all || suite == "warmup" || suite == "program") {
        RunWarmUpAndProgramSuite(src_cache, temp_user, cold_driver,
                                 do_all || suite == "warmup", do_all || suite == "program");
    }
    std::unique_ptr<Vulkan::Instance> shared_instance;
    if (do_all || suite == "gpu" || suite == "gc" || suite == "stageb") {
        shared_instance = std::make_unique<Vulkan::Instance>(0, false);
    }
    if (do_all || suite == "gpu" || suite == "gc") {
        RunGpuBarriersAndGc(*shared_instance, do_all || suite == "gpu", do_all || suite == "gc");
    }
    if (do_all || suite == "stageb") {
        if (!fs::exists(temp_user / "cache" / "CUSA03173")) {
            CopyCacheToTemp(src_cache, temp_user, cold_driver);
        }
        RunStageBDrawSuite(*shared_instance);
    }

    std::printf("================================================================================\n");
    std::printf("ALL REQUESTED EMPIRICAL SUITES PASSED.\n");
    return 0;
}
