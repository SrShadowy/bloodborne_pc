// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_frame_rate.h"

#include "bbport_code_patch.h"
#include "bbport_frame_rate_pacer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace BbFrameRate {
namespace {

constexpr std::uint32_t kGuestBase = 0x400000;
constexpr std::uint32_t kCodeEnd = 0x54d96dc;
constexpr std::uint32_t kFrameManagerAddr = 0x02434770;
constexpr std::uint32_t kStepImmAddr = 0x02418e39;
constexpr std::uint32_t kFlipperTargetImmAddr = 0x02434845;
constexpr std::uint32_t kStepLoadAddr = 0x02418e3d;
constexpr std::uint32_t kClothScalarsAddr = 0x04d3bd00;
constexpr std::uint32_t kWindowObjectSlot = 0x05940e00;
constexpr int kUncappedMaxFps = 240;
constexpr int kUncappedRefFps = 120;

constexpr std::uint8_t kFrameManagerPrologue[17] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41,
    0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x38,
};

float BitsToFloat(std::uint32_t u) {
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

std::uint32_t FloatToBits(float f) {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

const Rated* FindRated(std::uint32_t at) {
    for (const Rated& r : kRated) {
        if (r.at == at) return &r;
    }
    return nullptr;
}

std::vector<std::uint8_t> HexToBytes(const char* hex) {
    std::vector<std::uint8_t> out;
    auto nib = [](char c) -> std::uint8_t {
        return static_cast<std::uint8_t>(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    };
    for (std::size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
        out.push_back(static_cast<std::uint8_t>((nib(hex[i]) << 4) | nib(hex[i + 1])));
    }
    return out;
}

void FormatCountBytes(const Counted& c, int fps, std::uint8_t* out) {
    const std::uint64_t v =
        c.kind == Count::Frames
            ? static_cast<std::uint64_t>(c.was) * static_cast<std::uint64_t>(fps) / 30
            : static_cast<std::uint64_t>(c.was) * 30 / static_cast<std::uint64_t>(fps);
    if (c.kind == Count::Digits) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%05u",
                      static_cast<unsigned>(std::min<std::uint64_t>(v, 99999)));
        std::memcpy(out, buf, c.n);
        return;
    }
    const auto u = static_cast<std::uint32_t>(v);
    std::memcpy(out, &u, c.n);
}

void EmitFlipperHookStub(std::uint8_t* exec_page, std::uint8_t* fn_entry) {
    std::uint8_t* out = exec_page;
    auto emit = [&](const void* b, std::size_t n) { std::memcpy(out, b, n); out += n; };
    static constexpr std::uint8_t kPrologue[] = {
        0x55, 0x48, 0x89, 0xe5, 0x48, 0x83, 0xe4, 0xf0,
        0x48, 0x81, 0xec, 0x50, 0x01, 0x00, 0x00,
        0x48, 0x89, 0x04, 0x24, 0x48, 0x89, 0x4c, 0x24, 0x08,
        0x48, 0x89, 0x54, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
        0x48, 0x89, 0x7c, 0x24, 0x20, 0x4c, 0x89, 0x44, 0x24, 0x28,
        0x4c, 0x89, 0x4c, 0x24, 0x30, 0x4c, 0x89, 0x54, 0x24, 0x38,
        0x4c, 0x89, 0x5c, 0x24, 0x40,
        0xc5, 0xf8, 0x29, 0x44, 0x24, 0x50, 0xc5, 0xf8, 0x29, 0x4c, 0x24, 0x60,
        0xc5, 0xf8, 0x29, 0x54, 0x24, 0x70, 0xc5, 0xf8, 0x29, 0x9c, 0x24, 0x80, 0, 0, 0,
        0xc5, 0xf8, 0x29, 0xa4, 0x24, 0x90, 0, 0, 0, 0xc5, 0xf8, 0x29, 0xac, 0x24, 0xa0, 0, 0, 0,
        0xc5, 0xf8, 0x29, 0xb4, 0x24, 0xb0, 0, 0, 0, 0xc5, 0xf8, 0x29, 0xbc, 0x24, 0xc0, 0, 0, 0,
        0xc5, 0x78, 0x29, 0x84, 0x24, 0xd0, 0, 0, 0, 0xc5, 0x78, 0x29, 0x8c, 0x24, 0xe0, 0, 0, 0,
        0xc5, 0x78, 0x29, 0x94, 0x24, 0xf0, 0, 0, 0, 0xc5, 0x78, 0x29, 0x9c, 0x24, 0x00, 1, 0, 0,
        0xc5, 0x78, 0x29, 0xa4, 0x24, 0x10, 1, 0, 0, 0xc5, 0x78, 0x29, 0xac, 0x24, 0x20, 1, 0, 0,
        0xc5, 0x78, 0x29, 0xb4, 0x24, 0x30, 1, 0, 0, 0xc5, 0x78, 0x29, 0xbc, 0x24, 0x40, 1, 0, 0,
        0x48, 0xb8,
    };
    emit(kPrologue, sizeof(kPrologue));
    const auto fn = reinterpret_cast<std::uint64_t>(&SprjFlipperUpdateHook);
    emit(&fn, sizeof(fn));
    static constexpr std::uint8_t kEpilogue[] = {
        0xff, 0xd0,
        0xc5, 0x78, 0x28, 0xbc, 0x24, 0x40, 1, 0, 0, 0xc5, 0x78, 0x28, 0xb4, 0x24, 0x30, 1, 0, 0,
        0xc5, 0x78, 0x28, 0xac, 0x24, 0x20, 1, 0, 0, 0xc5, 0x78, 0x28, 0xa4, 0x24, 0x10, 1, 0, 0,
        0xc5, 0x78, 0x28, 0x9c, 0x24, 0x00, 1, 0, 0, 0xc5, 0x78, 0x28, 0x94, 0x24, 0xf0, 0, 0, 0,
        0xc5, 0x78, 0x28, 0x8c, 0x24, 0xe0, 0, 0, 0, 0xc5, 0x78, 0x28, 0x84, 0x24, 0xd0, 0, 0, 0,
        0xc5, 0xf8, 0x28, 0xbc, 0x24, 0xc0, 0, 0, 0, 0xc5, 0xf8, 0x28, 0xb4, 0x24, 0xb0, 0, 0, 0,
        0xc5, 0xf8, 0x28, 0xac, 0x24, 0xa0, 0, 0, 0, 0xc5, 0xf8, 0x28, 0xa4, 0x24, 0x90, 0, 0, 0,
        0xc5, 0xf8, 0x28, 0x9c, 0x24, 0x80, 0, 0, 0, 0xc5, 0xf8, 0x28, 0x54, 0x24, 0x70,
        0xc5, 0xf8, 0x28, 0x4c, 0x24, 0x60, 0xc5, 0xf8, 0x28, 0x44, 0x24, 0x50,
        0x4c, 0x8b, 0x5c, 0x24, 0x40, 0x4c, 0x8b, 0x54, 0x24, 0x38,
        0x4c, 0x8b, 0x4c, 0x24, 0x30, 0x4c, 0x8b, 0x44, 0x24, 0x28,
        0x48, 0x8b, 0x7c, 0x24, 0x20, 0x48, 0x8b, 0x74, 0x24, 0x18,
        0x48, 0x8b, 0x54, 0x24, 0x10, 0x48, 0x8b, 0x4c, 0x24, 0x08,
        0x48, 0x8b, 0x04, 0x24, 0x48, 0x89, 0xec, 0x5d, 0xc3,
    };
    emit(kEpilogue, sizeof(kEpilogue));
    BbCodePatch::WriteRel32Jump(fn_entry, exec_page, sizeof(kFrameManagerPrologue));
}

int ResolveWantedFps(std::uint8_t* image) {
    auto va = [&](std::uint32_t bn) -> std::uint8_t* { return image + (bn - kGuestBase); };
    if (const char* env = std::getenv("BB_FPS"); env && *env) {
        if (std::strcmp(env, "30") == 0) return 30;
        if (std::strcmp(env, "60") == 0) return 60;
        if (std::strcmp(env, "90") == 0) return 90;
        if (std::strcmp(env, "120") == 0) return 120;
        if (std::strcmp(env, "uncap") == 0 || std::strcmp(env, "0") == 0) return 0;
    }
    std::uint32_t step_imm = 0;
    std::memcpy(&step_imm, va(kStepImmAddr), 4);
    if (step_imm == 0x3c888889u) {
        std::uint32_t ft = 0;
        std::memcpy(&ft, va(kFlipperTargetImmAddr), 4);
        return (ft == 0x3b888889u) ? 0 : (ft == 0x3c360b61u) ? 90 : 60;
    }
    return 0;
}

void ApplyStaticTables(std::uint8_t* image, int mode, int fps) {
    auto va = [&](std::uint32_t bn) -> std::uint8_t* { return image + (bn - kGuestBase); };
    for (const Constant& c : kConstants) {
        std::uint32_t now = (mode == 30) ? c.was : c.now;
        if (const Rated* r = FindRated(c.at); r && mode != 60 && mode != 30) {
            now = FloatToBits(RatedValue(r->kind, BitsToFloat(c.was), fps));
        }
        std::memcpy(va(c.at), &now, 4);
    }
    for (const Counted& c : kCounts) {
        std::uint8_t now[8];
        FormatCountBytes(c, (c.kind == Count::Digits && mode == 0) ? kUncappedMaxFps : fps, now);
        std::memcpy(va(c.at), now, c.n);
    }
    for (const Bytes& b : kBytes) {
        std::memcpy(va(b.at), b.now, b.n);
    }
    auto apply_sites = [&](const auto& sites, bool skip_cam30) {
        for (const Site& s : sites) {
            if (skip_cam30 && (s.at == 0x0183dc97 || s.at == 0x0183dcfe ||
                               s.at == 0x0183ff29 || s.at == 0x01840799)) {
                continue;
            }
            const auto bytes = HexToBytes(s.now);
            std::memcpy(va(s.at), bytes.data(), bytes.size());
        }
    };
    apply_sites(kSixtySites, mode == 30);
    const std::uint32_t step_bits = FloatToBits(1.0f / static_cast<float>(fps));
    std::memcpy(va(0x0575a908), &step_bits, 4);
    apply_sites(kFrameTimeSites, false);
    if (mode != 60 && mode != 30) apply_sites(kClothSites, false);
}

void InstallDynamicSlots(std::uint8_t* image, std::uint8_t* data_page, int mode, int fps) {
    auto va = [&](std::uint32_t bn) -> std::uint8_t* { return image + (bn - kGuestBase); };
    auto& st = State();
    std::size_t data_off = 0;

    st.step_ptr = reinterpret_cast<float*>(data_page + data_off);
    data_off += 16;
    *st.step_ptr = 1.0f / static_cast<float>(fps);
    {
        std::uint8_t* step_site = va(kStepLoadAddr);
        const auto step_disp = static_cast<std::int32_t>(
            reinterpret_cast<std::uint8_t*>(st.step_ptr) - (step_site + 8));
        std::uint8_t step_load[21] = {
            0xf3, 0x0f, 0x10, 0x05, 0, 0, 0, 0,
            0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x0f, 0x1f, 0x44, 0x00, 0x00,
        };
        std::memcpy(step_load + 4, &step_disp, 4);
        std::memcpy(step_site, step_load, sizeof(step_load));
    }

    if (mode != 60 && mode != 30) {
        st.cloth_scalars = reinterpret_cast<float*>(data_page + data_off);
        data_off += 16;
        st.cloth_dt = 1.0f / static_cast<float>(fps);
        const float s = std::min(1.0f, 60.0f / static_cast<float>(fps));
        for (int k = 0; k < 4; ++k) st.cloth_scalars[k] = s * s;
        std::memcpy(va(kClothScalarsAddr), st.cloth_scalars, 16);
        for (const Site& s_cloth : kClothSites) {
            if (std::strlen(s_cloth.now) == 50) {
                std::uint8_t* cave = va(s_cloth.at);
                const auto disp = static_cast<std::int32_t>(
                    reinterpret_cast<std::uint8_t*>(st.cloth_scalars) - (cave + 10));
                std::memcpy(cave + 6, &disp, 4);
            }
        }
    }

    for (const DtLoad& d : kDtLoads) {
        auto* slot_ptr = reinterpret_cast<float*>(data_page + data_off);
        data_off += 16;
        const float* live_src =
            (d.constant >= kCodeEnd) ? reinterpret_cast<const float*>(va(d.constant)) : nullptr;
        for (std::uint8_t k = 0; k < d.lanes; ++k) {
            slot_ptr[k] = RatedValue(d.kind, live_src ? live_src[k] : d.was, fps);
        }
        st.dt_slots.push_back({slot_ptr, live_src, d.was, d.kind, d.lanes});
        const auto orig_insn = HexToBytes(d.bytes);
        std::uint8_t* insn_va = va(d.insn);
        std::memcpy(insn_va, orig_insn.data(), orig_insn.size());
        const auto disp = static_cast<std::int32_t>(
            reinterpret_cast<std::uint8_t*>(slot_ptr) - (insn_va + d.len));
        std::memcpy(insn_va + d.disp, &disp, 4);
    }
}

} // namespace

void PatchImage(unsigned char* image, std::uint64_t size) {
    if (!image || size <= (kWindowObjectSlot - kGuestBase) + 8) return;
    State().image = image;
    auto va = [&](std::uint32_t bn) -> std::uint8_t* { return image + (bn - kGuestBase); };
    if (std::memcmp(va(kFrameManagerAddr), kFrameManagerPrologue, sizeof(kFrameManagerPrologue)) != 0) return;

    std::uint8_t* exec_page = BbCodePatch::AllocateNearPage(va(kFrameManagerAddr));
    std::uint8_t* data_page = BbCodePatch::AllocateNearPage(va(kFrameManagerAddr));
    if (!exec_page || !data_page) return;

    const int mode = ResolveWantedFps(image);
    State().game_fps = mode;
    const int fps = (mode == 0) ? kUncappedRefFps : mode;
    ApplyStaticTables(image, mode, fps);
    InstallDynamicSlots(image, data_page, mode, fps);
    EmitFlipperHookStub(exec_page, va(kFrameManagerAddr));
    BbCodePatch::SealExecPage(exec_page);

    std::printf("[BBPort] Frame rate & flipper hook installed (mode=%s, %zu dt loads).\n",
                mode == 0 ? "uncapped" : (mode == 90 ? "90" : (mode == 30 ? "30" : "60")),
                State().dt_slots.size());
}

} // namespace BbFrameRate
