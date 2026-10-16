// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#include <sys/uio.h>
#include <unistd.h>

#include <fmt/format.h>
#include <magic_enum/magic_enum.hpp>

#include "bbport_write_log.h"
#include "core/emulator_settings.h"
#include "cp/cp_diagnostics.h"
#include "cp/cp_pm4.h"

template <>
struct magic_enum::customize::enum_range<BbCp::Op> {
    static constexpr int min = 0;
    static constexpr int max = 255;
};

extern "C" int runtime_memory_vma_info(uintptr_t address, int* prot, int* type, uintptr_t* end);
extern "C" void runtime_memory_read_backing(uintptr_t address, void* data, u64 size);

namespace BbCp::Diagnostics {

std::string OpName(u32 op) {
    const auto name = magic_enum::enum_name(Op(op));
    return name.empty() ? fmt::format("{:#x}", op) : std::string(name);
}

namespace {

struct LabelRecord {
    std::atomic<u64> when_ns{0}; ///< steady clock; 0: not written yet
    u64 address;
    u64 value;
    u32 bytes;
    LabelStep step;
};
constexpr size_t LabelRingSize = 256;
std::array<LabelRecord, LabelRingSize> label_ring;
std::atomic<u64> label_next{0};

u64 NowNs() {
    return u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count());
}

} // namespace

void NoteLabel(LabelStep step, u64 address, u64 value, u32 bytes) {
    LabelRecord& record = label_ring[label_next.fetch_add(1, std::memory_order_relaxed) %
                                     LabelRingSize];
    record.when_ns.store(0, std::memory_order_relaxed);
    record.address = address;
    record.value = value;
    record.bytes = bytes;
    record.step = step;
    record.when_ns.store(NowNs(), std::memory_order_release);
}

bool ReadGuest(const void* address, void* out, u32 bytes) {
    int prot, type;
    uintptr_t end;
    const auto at = reinterpret_cast<uintptr_t>(address);
    if (bytes == 0 || !runtime_memory_vma_info(at, &prot, &type, &end) || end < at + bytes) {
        return false;
    }
    runtime_memory_read_backing(at, out, bytes);
    return true;
}

std::string RecentLabels(size_t count) {
    static constexpr const char* steps[] = {"compute decoded", "compute GPU write",
                                            "compute written", "graphics GPU write",
                                            "graphics written"};
    const u64 next = label_next.load(std::memory_order_acquire);
    const u64 now = NowNs();
    count = std::min<u64>({count, next, LabelRingSize});
    std::string out;
    for (u64 i = next - count; i < next; ++i) {
        const LabelRecord& record = label_ring[i % LabelRingSize];
        const u64 when = record.when_ns.load(std::memory_order_acquire);
        if (when == 0) {
            continue;
        }
        // What the guest reads there now (unreadable: a trapped or unmapped page).
        u64 current = 0;
        const u32 bytes = std::min<u32>(record.bytes, 8);
        const bool readable =
            ReadGuest(reinterpret_cast<const void*>(record.address), &current, bytes);
        out += fmt::format("  {:>8.1f} ms ago  {:<18} {:#x} = {:#x}{}\n",
                           double(now - when) / 1e6, steps[u32(record.step) % 5], record.address,
                           record.value,
                           !readable ? " (memory unreadable now)"
                           : current != (bytes == 8 ? record.value : record.value & 0xFFFFFFFFull)
                               ? fmt::format(" (memory holds {:#x} now)", current)
                               : std::string{});
    }
    return out;
}

namespace {

struct DcbStats {
    u64 buffers[2]{}, with_draws[2]{}, draws[2]{}, dispatches[2]{}, max_draws[2]{};
    u64 clear_first[2]{}, ctx_before[2]{}, sh_before[2]{}, ibs[2]{}, dwords[2]{};
    u64 hist[2][6]{}; // draws per buffer: 0, 1-9, 10-99, 100-499, 500-1999, 2000+
    // Packets by opcode (any depth), EVENT_WRITE by event type, EOP by data and interrupt
    // select, EOS by command, WAIT_REG_MEM by memory space and engine.
    u64 opcodes[256]{}, events[64]{}, eop_data[8]{}, eop_int[4]{}, eos_cmd[8]{}, wait_mem[2][2]{};
    u64 wait_outcome[4]{};
    double wait_ms = 0;
    std::chrono::steady_clock::time_point window = std::chrono::steady_clock::now();
};
DcbStats stats;

bool IsDraw(Op op) {
    switch (op) {
    case Op::DrawIndex2:
    case Op::DrawIndexOffset2:
    case Op::DrawIndexAuto:
    case Op::DrawIndirect:
    case Op::DrawIndirectMulti:
    case Op::DrawIndexIndirect:
    case Op::DrawIndexIndirectMulti:
    case Op::DrawIndexIndirectCountMulti:
        return true;
    default:
        return false;
    }
}

} // namespace

bool DcbStatsEnabled() {
    static const bool enabled = EmulatorSettingsImpl::Flag("BB_DCB_STATS", false);
    return enabled;
}

void NoteWait(int outcome, double ms) {
    ++stats.wait_outcome[outcome];
    stats.wait_ms += ms;
}

void ScanDcb(std::span<const u32> dcb, int depth) {
    auto& st = stats;
    const int d = depth > 0 ? 1 : 0;
    u64 draws = 0, dispatches = 0, ctx = 0, sh = 0, ibs = 0;
    bool clear_first = false;
    st.dwords[d] += dcb.size();
    for (size_t at = 0; at < dcb.size();) {
        const Header header{dcb[at]};
        if (header.Type() == 2) {
            ++at;
            continue;
        }
        if (header.Type() != 3) {
            break;
        }
        const Op op = header.Opcode();
        const u32* body = dcb.data() + at + 1;
        ++st.opcodes[u8(op)];
        if (op == Op::EventWrite) {
            ++st.events[body[0] & 63];
        } else if (op == Op::EventWriteEop) {
            ++st.eop_data[(body[2] >> 29) & 7];
            ++st.eop_int[(body[2] >> 24) & 3];
        } else if (op == Op::EventWriteEos) {
            ++st.eos_cmd[(body[2] >> 29) & 7];
        } else if (op == Op::WaitRegMem) {
            ++st.wait_mem[(body[0] >> 4) & 1][(body[0] >> 8) & 1];
        }
        if (IsDraw(op)) {
            ++draws;
        } else if (op == Op::DispatchDirect || op == Op::DispatchIndirect) {
            ++dispatches;
        } else if (op == Op::IndirectBuffer) {
            ++ibs;
        } else if (draws == 0) {
            if (op == Op::ClearState) {
                clear_first = true;
            } else if (op == Op::SetContextReg) {
                ctx += header.BodyWords() - 1;
            } else if (op == Op::SetShReg) {
                sh += header.BodyWords() - 1;
            }
        }
        at += header.BodyWords() + 1;
    }
    ++st.buffers[d];
    st.draws[d] += draws;
    st.dispatches[d] += dispatches;
    st.ibs[d] += ibs;
    st.max_draws[d] = std::max(st.max_draws[d], draws);
    if (draws) {
        ++st.with_draws[d];
        st.clear_first[d] += clear_first;
        st.ctx_before[d] += ctx;
        st.sh_before[d] += sh;
    }
    const int bucket = draws == 0 ? 0 : draws < 10 ? 1 : draws < 100 ? 2 : draws < 500 ? 3
                     : draws < 2000 ? 4 : 5;
    ++st.hist[d][bucket];

    const auto now = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double>(now - st.window).count();
    if (depth != 0 || secs < 5.0) {
        return;
    }
    for (int k = 0; k < 2; ++k) {
        const double with = double(std::max<u64>(st.with_draws[k], 1));
        std::printf("DCB stats %s: %.0f buffers/s (%.0f with draws), %.0f draws/s, %.0f "
                    "dispatches/s, max %llu draws in one buffer, %.0f IBs/s, %.0f kdw/s; "
                    "draw buffers: %.0f%% start with ClearState, %.0f ctx + %.0f sh reg "
                    "writes before first draw; draws/buffer hist 0:%llu 1-9:%llu 10-99:%llu "
                    "100-499:%llu 500-1999:%llu 2000+:%llu\n",
                    k ? "indirect" : "submitted", st.buffers[k] / secs, st.with_draws[k] / secs,
                    st.draws[k] / secs, st.dispatches[k] / secs,
                    (unsigned long long)st.max_draws[k], st.ibs[k] / secs,
                    st.dwords[k] / secs / 1000.0, 100.0 * st.clear_first[k] / with,
                    st.ctx_before[k] / with, st.sh_before[k] / with,
                    (unsigned long long)st.hist[k][0], (unsigned long long)st.hist[k][1],
                    (unsigned long long)st.hist[k][2], (unsigned long long)st.hist[k][3],
                    (unsigned long long)st.hist[k][4], (unsigned long long)st.hist[k][5]);
    }
    std::string line;
    for (u32 op = 0; op < 256; ++op) {
        if (st.opcodes[op]) {
            line += fmt::format(" {}:{:.0f}", OpName(op), st.opcodes[op] / secs);
        }
    }
    std::printf("PM4 packets/s:%s\n", line.c_str());
    line.clear();
    for (u32 event = 0; event < 64; ++event) {
        if (st.events[event]) {
            line += fmt::format(" {}:{:.0f}", event, st.events[event] / secs);
        }
    }
    std::printf("PM4 events/s:%s; EOP data_sel %.0f/%.0f/%.0f/%.0f/%.0f (none/32/64/clock/perf)"
                " int_sel %.0f/%.0f/%.0f/%.0f; EOS cmd %.0f/%.0f/%.0f/%.0f/%.0f; WAIT_REG_MEM "
                "reg me/pfp %.0f/%.0f mem me/pfp %.0f/%.0f\n",
                line.c_str(), st.eop_data[0] / secs, st.eop_data[1] / secs, st.eop_data[2] / secs,
                st.eop_data[3] / secs, st.eop_data[4] / secs, st.eop_int[0] / secs,
                st.eop_int[1] / secs, st.eop_int[2] / secs, st.eop_int[3] / secs,
                st.eos_cmd[0] / secs, st.eos_cmd[1] / secs, st.eos_cmd[2] / secs,
                st.eos_cmd[3] / secs, st.eos_cmd[4] / secs, st.wait_mem[0][0] / secs,
                st.wait_mem[0][1] / secs, st.wait_mem[1][0] / secs, st.wait_mem[1][1] / secs);
    std::printf("WAIT_REG_MEM outcomes/s: VO label %.0f, pending fence %.0f, met %.0f, waited "
                "%.0f (%.2f ms/s)\n",
                st.wait_outcome[0] / secs, st.wait_outcome[1] / secs, st.wait_outcome[2] / secs,
                st.wait_outcome[3] / secs, st.wait_ms / secs);
    st = DcbStats{};
}

void ReportBadPacket(uintptr_t base, std::size_t dwords, const u32* at, u64 seq, int depth) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(at);
    int prot = 0, type = -1;
    uintptr_t end = 0;
    runtime_memory_vma_info(address, &prot, &type, &end);
    std::fprintf(stderr,
                 "Bad PM4 packet at %#llx: dword %#x, %#llx bytes into a %#llx-byte buffer at %#llx "
                 "(IB depth %d, submission %llu; memory type %d, prot %#x)\n",
                 (unsigned long long)address, *at, (unsigned long long)(address - base),
                 (unsigned long long)(dwords * 4), (unsigned long long)base, depth,
                 (unsigned long long)seq, type, prot);
    const u32* first = reinterpret_cast<const u32*>(std::max(base, address - 64));
    const u32* last = std::min(reinterpret_cast<const u32*>(base) + dwords, at + 16);
    for (const u32* p = first; p < last; p += 8) {
        std::fprintf(stderr, "  %#llx:", (unsigned long long)reinterpret_cast<uintptr_t>(p));
        for (const u32* q = p; q < std::min(p + 8, last); ++q) {
            std::fprintf(stderr, q == at ? " [%08x]" : " %08x", *q);
        }
        std::fprintf(stderr, "\n");
    }
    BbWriteLog::DumpRange(base, dwords * 4);
    std::fflush(stderr);
}

void CheckSubmittedCopy(const SubmittedCopy& copy, u64 seq) {
    // The guest memory may be unmapped by now: read it without faulting.
    static std::atomic<u64> changed{0}, checked{0};
    const auto first_change = [](const u32* guest, const u32* kept, std::size_t dwords) -> s64 {
        std::array<u32, 1024> buf;
        for (std::size_t at = 0; at < dwords; at += buf.size()) {
            const std::size_t n = std::min(buf.size(), dwords - at);
            iovec local{buf.data(), n * 4}, remote{const_cast<u32*>(guest + at), n * 4};
            if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) != ssize_t(n * 4)) {
                // bbport BB_GUEST_IN_PLACE: dma-buf guest memory is not readable that way; still
                // mapped, it is read directly.
                int prot = 0, type = -1;
                uintptr_t end = 0;
                const auto from = reinterpret_cast<uintptr_t>(guest + at);
                if (!runtime_memory_vma_info(from, &prot, &type, &end) || end < from + n * 4) {
                    return -2 - s64(at); // unmapped
                }
                std::memcpy(buf.data(), guest + at, n * 4);
            }
            for (std::size_t i = 0; i < n; ++i) {
                if (buf[i] != kept[at + i]) {
                    return s64(at + i);
                }
            }
        }
        return -1;
    };
    checked.fetch_add(1, std::memory_order_relaxed);
    const s64 dcb_change = first_change(copy.guest_dcb, copy.data.data(), copy.dcb_dwords);
    const s64 ccb_change =
        first_change(copy.guest_ccb, copy.data.data() + copy.dcb_dwords, copy.ccb_dwords);
    if (dcb_change == -1 && ccb_change == -1) {
        return;
    }
    const u64 n = changed.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n > 16 && n % 100 != 0) {
        return;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                                copy.submitted)
                          .count();
    const s64 at = dcb_change != -1 ? dcb_change : ccb_change;
    std::printf("Command buffers: the guest changed submission %llu's %s at %p +%#llx (%s) "
                "%.3f ms after submitting it, before it was decoded; %llu of %llu so far "
                "(decoded from the copy)\n",
                (unsigned long long)seq, dcb_change != -1 ? "dcb" : "ccb",
                static_cast<const void*>(dcb_change != -1 ? copy.guest_dcb : copy.guest_ccb),
                (unsigned long long)((at < -1 ? -2 - at : at) * 4),
                at < -1 ? "unmapped" : "rewritten", ms, (unsigned long long)n,
                (unsigned long long)checked.load());
    if (n > 4 || at < 0) {
        return;
    }
    // What changed, and the packets before it (a WaitRegMem the guest releases after patching,
    // or a write of ours into its own command buffer).
    const u32* kept = dcb_change != -1 ? copy.data.data() : copy.data.data() + copy.dcb_dwords;
    const u32* guest = dcb_change != -1 ? copy.guest_dcb : copy.guest_ccb;
    const std::size_t dwords = dcb_change != -1 ? copy.dcb_dwords : copy.ccb_dwords;
    const std::size_t from = at >= 8 ? at - 8 : 0, to = std::min<std::size_t>(dwords, at + 8);
    std::array<u32, 16> now{};
    iovec local{now.data(), (to - from) * 4}, remote{const_cast<u32*>(guest + from), (to - from) * 4};
    const bool readable = process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t((to - from) * 4);
    std::printf("  submitted:");
    for (std::size_t i = from; i < to; ++i) {
        std::printf(i == std::size_t(at) ? " [%08x]" : " %08x", kept[i]);
    }
    std::printf("\n  now:      ");
    for (std::size_t i = from; readable && i < to; ++i) {
        std::printf(i == std::size_t(at) ? " [%08x]" : " %08x", now[i - from]);
    }
    std::printf("\n  packets before it (dword offset:opcode):");
    std::array<std::pair<u32, u32>, 12> last{};
    std::size_t count = 0;
    for (std::size_t p = 0; p < std::size_t(at) && p < dwords;) {
        const u32 header = kept[p];
        if ((header >> 30) == 3) {
            last[count++ % last.size()] = {u32(p), (header >> 8) & 0xff};
            p += ((header >> 16) & 0x3fff) + 2;
        } else if ((header >> 30) == 2) {
            ++p;
        } else {
            break;
        }
    }
    for (std::size_t i = count > last.size() ? count - last.size() : 0; i < count; ++i) {
        std::printf(" %#x:%#x", last[i % last.size()].first, last[i % last.size()].second);
    }
    std::printf("\n");
}

} // namespace BbCp::Diagnostics
