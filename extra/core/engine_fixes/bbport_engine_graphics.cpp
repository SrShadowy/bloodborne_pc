// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_engine_graphics.h"

#include "bbport_camera.h"
#include "bbport_code_patch.h"
#include "bbport_settings.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace BbEngineGraphics {
namespace {

// CUSA03173 1.09 offsets relative to eboot image base (bbhost BN VA - 0x400000):
constexpr std::uint64_t kRenderView = 0x229e990, kCaCopy = 0x229faa8, kDofBranch = 0x21d7bbc;
constexpr std::uint64_t kYebisRecord = 0x21d4860, kSsaoDraw = 0xe669d0, kShadowSetup = 0xeac210;
constexpr std::uint64_t kVelocityPost = 0xe64430, kFogUploadJump = 0x22daadf, kTableUpload = 0xe2e6b0;

// Global data slots (relative to eboot image base):
constexpr std::uint64_t kYebisPostBlock = 0x55406e0, kGraphicsManager = 0x5540dd8;
constexpr std::uint64_t kLightGridDiv = 0x513ae80, kResWords = 0x51289f8, kRendManSlot = 0x5540298;

constexpr std::uint8_t kRenderViewPrologue[20] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
    0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0xa8, 0x0e, 0x00, 0x00,
};
constexpr std::uint8_t kYebisPrologue[20] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
    0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x28, 0x4d, 0x89, 0xcd,
};
constexpr std::uint8_t kSsaoPrologue[20] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
    0x41, 0x54, 0x53, 0x48, 0x81, 0xec, 0xa8, 0x02, 0x00, 0x00,
};
constexpr std::uint8_t kShadowPrologue[19] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48,
    0x83, 0xec, 0x18, 0x49, 0x89, 0xf6, 0x48, 0x89, 0xfb,
};
constexpr std::uint8_t kVelocityPrologue[17] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55,
    0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x28,
};

unsigned char* g_image = nullptr;
std::uint64_t g_image_size = 0;
volatile std::uint8_t g_ca_enabled = 1;
volatile std::uint8_t g_dof_enabled = 1;

void SyncRendManAndLightGrid(bool light_grid_opt, int lod_bias) {
    if (!g_image || g_image_size <= kGraphicsManager + 8) return;

    std::uintptr_t rend = 0;
    std::memcpy(&rend, g_image + kRendManSlot, sizeof(rend));
    if (rend >= 0x100000000ULL) {
        std::uintptr_t rend_settings = 0;
        std::memcpy(&rend_settings, reinterpret_cast<const void*>(rend + 0x10), sizeof(rend_settings));
        if (rend_settings >= 0x100000000ULL) {
            *reinterpret_cast<std::int32_t*>(rend_settings + 0x530) = std::clamp(lod_bias, -2, 2);
        }
    }

    auto* div = reinterpret_cast<std::uint32_t*>(g_image + kLightGridDiv);
    if ((div[0] == 3 || div[0] == 1) && (div[1] == 3 || div[1] == 1)) {
        div[0] = light_grid_opt ? 1u : 3u;
        div[1] = light_grid_opt ? 1u : 3u;
    }
    std::uintptr_t gm = 0;
    std::memcpy(&gm, g_image + kGraphicsManager, sizeof(gm));
    if (gm < 0x100000000ULL) return;
    const auto* res = reinterpret_cast<const std::uint32_t*>(g_image + kResWords);
    const std::uint32_t fw = (res[0] >= 256 && res[0] <= 7680) ? res[0] : 1920u;
    const std::uint32_t fh = (res[1] >= 144 && res[1] <= 4320) ? res[1] : 1080u;
    auto* tile = reinterpret_cast<std::uint32_t*>(gm + 0x78a8);
    tile[0] = light_grid_opt ? std::clamp((fw + 3u) / 4u, 128u, 16384u) : 128u;
    tile[1] = light_grid_opt ? std::clamp((fh + 3u) / 4u, 128u, 16384u) : 128u;
}

enum HookId : std::uint64_t {
    kHookRenderView = 1,
    kHookYebis = 2,
    kHookSsao = 3,
    kHookShadow = 4,
    kHookFog = 6,
};

void SyncDofBranch(bool dof_enabled) {
    if (!g_image) return;
    static int last_dof = -1;
    const int want = dof_enabled ? 1 : 0;
    if (want == last_dof) return;
    static bool dof_writable = false;
    if (!dof_writable) {
        const auto page_sz = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        const auto page = reinterpret_cast<std::uintptr_t>(g_image + kDofBranch) & ~(page_sz - 1);
        mprotect(reinterpret_cast<void*>(page), page_sz, PROT_READ | PROT_WRITE | PROT_EXEC);
        dof_writable = true;
    }
    static constexpr std::uint8_t kDofOrig[6] = {0x0f, 0x84, 0xde, 0x00, 0x00, 0x00};
    static constexpr std::uint8_t kDofPatched[6] = {0xe9, 0xdf, 0x00, 0x00, 0x00, 0x90};
    std::memcpy(g_image + kDofBranch, want ? kDofOrig : kDofPatched, 6);
    last_dof = want;
}

void OnRenderView(const BbSettings::Values& s, const std::uint64_t* saved) {
    BbCamera::Tick();
    g_ca_enabled = s.effects[0].load(std::memory_order_relaxed) ? 1 : 0;
    g_dof_enabled = s.effects[1].load(std::memory_order_relaxed) ? 1 : 0;
    SyncDofBranch(g_dof_enabled != 0);
    SyncRendManAndLightGrid(s.light_grid_opt.load(std::memory_order_relaxed),
                            s.model_lod.load(std::memory_order_relaxed));

    auto* r = reinterpret_cast<std::uint8_t*>(saved[0]); // r9 = renderer
    if (r >= reinterpret_cast<std::uint8_t*>(0x100000000ULL)) {
        std::uint64_t caps = 0;
        std::memcpy(&caps, r + 0x48, sizeof(caps));
        const bool ssao = s.effects[3].load(std::memory_order_relaxed);
        const bool aa = s.effects[4].load(std::memory_order_relaxed);
        r[0x2968] = (ssao && (caps & 0x8ULL)) ? 1 : 0;
        r[0x296a] = (aa && (caps & 0x10ULL)) ? 1 : 0;
    }
}

void OnYebisRecord(const BbSettings::Values& s, const std::uint64_t* saved) {
    const auto* yebis = reinterpret_cast<const std::uint8_t*>(saved[5]); // rdi
    if (yebis < reinterpret_cast<const std::uint8_t*>(0x100000000ULL) || !g_image || yebis[0x2710]) {
        return;
    }
    auto* block = g_image + kYebisPostBlock;
    const std::uint32_t zero_u = 0;
    if (!g_ca_enabled) std::memcpy(block + 0xac, &zero_u, 4);

    const float sat_s = std::clamp(s.color_saturation.load(std::memory_order_relaxed), 0.0f, 3.0f);
    if (sat_s != 1.0f) {
        float sat = 0.0f;
        std::memcpy(&sat, block + 0xc8, 4);
        sat *= sat_s;
        std::memcpy(block + 0xc8, &sat, 4);
    }
    if (!s.vignette.load(std::memory_order_relaxed)) {
        const float zero_f = 0.0f;
        std::memcpy(block + 0xd0, &zero_f, 4);
    }
    const float bloom_s = std::clamp(s.bloom_scale.load(std::memory_order_relaxed), 0.0f, 3.0f);
    if (bloom_s != 1.0f) {
        float glare = 0.0f;
        std::memcpy(&glare, block + 0xdc, 4);
        glare *= bloom_s;
        std::memcpy(block + 0xdc, &glare, 4);
    }
}

void OnSsaoDraw(const BbSettings::Values& s, const std::uint64_t* saved) {
    auto* ssao = reinterpret_cast<std::uint8_t*>(saved[5]); // rdi
    const float ao_s = std::clamp(s.ao_strength.load(std::memory_order_relaxed), 0.0f, 3.0f);
    if (ssao < reinterpret_cast<std::uint8_t*>(0x100000000ULL) || ao_s == 1.0f || ssao[0x148]) return;
    float v = 0.0f, o = 0.0f;
    std::memcpy(&v, ssao + 0x14c + 0x44, 4);
    std::memcpy(&o, ssao + 0x14c + 0x48, 4);
    const float sc = v * ao_s;
    const float off = o * ao_s + (1.0f - ao_s);
    std::memcpy(ssao + 0x14c + 0x44, &sc, 4);
    std::memcpy(ssao + 0x14c + 0x48, &off, 4);
}

void OnShadowSetup(const BbSettings::Values& s, const std::uint64_t* saved) {
    auto* block = reinterpret_cast<std::uint8_t*>(saved[4]); // rsi
    const float sh_s = std::clamp(s.shadow_distance.load(std::memory_order_relaxed), 0.25f, 4.0f);
    if (block < reinterpret_cast<std::uint8_t*>(0x100000000ULL) || sh_s == 1.0f || block[0x297c - 0x80]) return;
    for (std::size_t off : {0x38u, 0x3cu, 0x44u, 0x48u}) {
        float v = 0.0f;
        std::memcpy(&v, block + off, 4);
        v *= sh_s;
        std::memcpy(block + off, &v, 4);
    }
}

void OnFogUpload(const BbSettings::Values& s, const std::uint64_t* saved) {
    const float fog_s = std::clamp(s.fog_opacity.load(std::memory_order_relaxed), 0.0f, 3.0f);
    if (fog_s == 1.0f || !g_image) return;
    std::uint64_t gm = 0, fm = 0;
    std::memcpy(&gm, g_image + kGraphicsManager, 8);
    if (gm >= 0x100000000ULL) {
        std::memcpy(&fm, reinterpret_cast<const std::uint8_t*>(gm) + 0x5888, 8);
    }
    if (fm < 0x100000000ULL) return;
    const auto* f = reinterpret_cast<const std::uint8_t*>(fm);
    std::uint64_t map = 0, table = 0;
    std::uint32_t count = 0;
    std::memcpy(&map, f + 0x248, 8);
    std::memcpy(&table, f + 0x250, 8);
    std::memcpy(&count, f + 0x258, 4);
    if (map != saved[5] || table != saved[3] || table < 0x100000000ULL || count == 0 || count > 4096) return;
    auto* blocks = reinterpret_cast<float*>(table);
    for (std::uint32_t i = 0; i < count; ++i) {
        float* b = blocks + i * 24;
        b[0] *= fog_s;
        b[8] *= fog_s;
        b[12] *= fog_s;
    }
}

extern "C" std::int64_t GraphicsHookDispatch(std::uint64_t id, const std::uint64_t* saved) {
    const auto& s = BbSettings::Get();
    switch (id) {
    case kHookRenderView: OnRenderView(s, saved); return 0;
    case kHookYebis: OnYebisRecord(s, saved); return 0;
    case kHookSsao: OnSsaoDraw(s, saved); return 0;
    case kHookShadow: OnShadowSetup(s, saved); return 0;
    case kHookFog: OnFogUpload(s, saved); return 0;
    default: return 0;
    }
}

void RestoreStartupEffectPatches(unsigned char* image, int& installed) {
    static constexpr std::uint8_t kCaOrig[12] = {
        0x8b, 0x85, 0x90, 0xf5, 0xff, 0xff, 0x89, 0x83, 0xac, 0x00, 0x00, 0x00,
    };
    static constexpr std::uint8_t kCaPatched[12] = {
        0xc7, 0x83, 0xac, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x90,
    };
    if (std::memcmp(image + kCaCopy, kCaPatched, 12) == 0) {
        std::memcpy(image + kCaCopy, kCaOrig, 12);
        ++installed;
    }
    static constexpr std::uint64_t kModelLodSite = 0x1d6fc09;
    static constexpr std::uint8_t kModelLodOrig[6] = {0x8b, 0x0d, 0x11, 0x81, 0x81, 0x03};
    if (image[kModelLodSite] == 0xb9 && image[kModelLodSite + 5] == 0x90) {
        std::memcpy(image + kModelLodSite, kModelLodOrig, 6);
    }
}

} // namespace

void PatchImage(unsigned char* image, std::uint64_t size) {
    if (!image || size <= kGraphicsManager + 8) return;
    g_image = image;
    g_image_size = size;

    std::uint8_t* near_page = BbCodePatch::AllocateNearPage(image);
    if (!near_page) return;
    std::uint8_t* c = near_page;
    int installed = 0;

    auto try_hook = [&](std::uint64_t off, const std::uint8_t* prol, std::size_t len, HookId id) {
        if (std::memcmp(image + off, prol, len) == 0) {
            BbCodePatch::EmitPrologueHook(c, image + off, prol, len, &GraphicsHookDispatch, id);
            ++installed;
        }
    };
    try_hook(kRenderView, kRenderViewPrologue, sizeof(kRenderViewPrologue), kHookRenderView);
    try_hook(kYebisRecord, kYebisPrologue, sizeof(kYebisPrologue), kHookYebis);
    try_hook(kSsaoDraw, kSsaoPrologue, sizeof(kSsaoPrologue), kHookSsao);
    try_hook(kShadowSetup, kShadowPrologue, sizeof(kShadowPrologue), kHookShadow);

    std::uint8_t* fog_site = image + kFogUploadJump;
    if (fog_site[0] == 0xe9) {
        std::int32_t rel = 0;
        std::memcpy(&rel, fog_site + 1, 4);
        if (fog_site + 5 + rel == image + kTableUpload) {
            BbCodePatch::EmitPrologueHook(c, fog_site, nullptr, 0, &GraphicsHookDispatch, kHookFog,
                                          image + kTableUpload);
            ++installed;
        }
    }
    RestoreStartupEffectPatches(image, installed);

    BbCodePatch::SealExecPage(near_page);
    std::printf("[BBPort] Installed %d live engine graphics hooks (RenderView/YEBIS/SSAO/Shadow/Fog/CA/DOF).\n",
                installed);
}

} // namespace BbEngineGraphics
