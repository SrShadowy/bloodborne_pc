#include "bbport_engine_fixes.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace BbEngineFixes {
namespace {

// CUSA03173 1.09 offsets relative to eboot image base (bbhost BN VA - 0x400000):
constexpr std::uint64_t kPlayTimeAdd = 0x19419d6;
constexpr std::uint64_t kCharaStep = 0x1c43ee0;
constexpr std::uint64_t kGameDataMan = 0x553b130;
constexpr std::uint64_t kBlockHeapSize = 0x1f5a465;
constexpr std::uint64_t kPageHeapSize = 0x1f5a479;
constexpr std::uint64_t kMenuSize = 0x4736e88;
constexpr std::uint64_t kPushBack96 = 0xb84ce0;
constexpr std::uint64_t kHavokMemMgr = 0x54018b0;
constexpr std::uint64_t kReserveMore = 0x419100;

constexpr std::uint8_t kPlayTimeOrig[13] = {
    0xc5, 0xf2, 0x59, 0x05, 0x12, 0x67, 0xfe, 0x02, // vmulss xmm0, xmm1, [rip+0x2fe6712]
    0xc4, 0xe1, 0xfa, 0x2c, 0xc8,                   // vcvttss2si rcx, xmm0
};
constexpr std::uint8_t kCharaStepPrologue[15] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56,
    0x41, 0x54, 0x53, 0x48, 0x83, 0xec, 0x40,
};
constexpr std::uint8_t kPushBack96Prologue[17] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x56, 0x53, 0x48, 0x89, 0xf3,
    0x49, 0x89, 0xfe, 0x41, 0x8b, 0x46, 0x28,
};

unsigned char* g_image = nullptr;

// Carries sub-millisecond fractional frame time across frames so high-FPS play time stays exact.
extern "C" std::uint64_t PlayTimeStep(float dt_sec) {
    static double carry_ms = 0.0;
    const double total = static_cast<double>(dt_sec) * 1000.0 + carry_ms;
    if (total <= 0.0) {
        return 0;
    }
    const auto whole = static_cast<std::uint64_t>(total);
    carry_ms = total - static_cast<double>(whole);
    return whole;
}

// Clears invalid 0x8000000000000000ULL CharaId in PlayerGameData (+0x8 -> +0x690) before save serialization.
extern "C" void CharaStepHook() {
    if (!g_image) return;
    std::uint64_t gdm = 0, pgd = 0, id = 0;
    std::memcpy(&gdm, g_image + kGameDataMan, sizeof(gdm));
    if (gdm < 0x100000000ULL) return;
    std::memcpy(&pgd, reinterpret_cast<const void*>(gdm + 0x8), sizeof(pgd));
    if (pgd < 0x100000000ULL) return;
    auto* field = reinterpret_cast<std::uint8_t*>(pgd + 0x690);
    std::memcpy(&id, field, sizeof(id));
    if (id == 0x8000000000000000ULL) {
        const std::uint64_t none = 0;
        std::memcpy(field, &none, sizeof(none));
    }
}

// Guard against runaway raycast hit collector allocations and null pointer dereferences (SIGSEGV at 0xf00010).
// Caps degenerate queries (e.g. from zero-length rays during world pause) to 2048 hits.
extern "C" void PushBackSafe(std::uint8_t* array_ptr, const std::uint8_t* elem_ptr) {
    if (!array_ptr || !elem_ptr || !g_image) return;
    std::int32_t size = *reinterpret_cast<std::int32_t*>(array_ptr + 0x28);
    // Cap runaway queries (prevents freezing the CPU by traversing entire map partition):
    if (size >= 2048) return;

    const std::int32_t cap = *reinterpret_cast<std::int32_t*>(array_ptr + 0x2c) & 0x3fffffff;
    if (size >= cap) {
        typedef void (*ReserveFn)(void* mem_mgr, void* array_buf, int elem_size);
        auto reserve = reinterpret_cast<ReserveFn>(g_image + kReserveMore);
        void* mem_mgr = reinterpret_cast<void*>(g_image + kHavokMemMgr);
        reserve(mem_mgr, array_ptr + 0x20, 0x60);
        size = *reinterpret_cast<std::int32_t*>(array_ptr + 0x28);
    }

    auto* data = *reinterpret_cast<std::uint8_t**>(array_ptr + 0x20);
    if (!data) return; // Allocation failed, guard against null pointer write!

    std::uint8_t* dst = data + static_cast<std::size_t>(size) * 96;
    std::memcpy(dst, elem_ptr, 96);
    *reinterpret_cast<std::int32_t*>(array_ptr + 0x28) = size + 1;
}

void MakeWritable(void* ptr, std::size_t size) {
    const auto page_sz = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    const auto start = reinterpret_cast<std::uintptr_t>(ptr) & ~(page_sz - 1);
    const auto end = (reinterpret_cast<std::uintptr_t>(ptr) + size + page_sz - 1) & ~(page_sz - 1);
    mprotect(reinterpret_cast<void*>(start), end - start, PROT_READ | PROT_WRITE | PROT_EXEC);
}

void WriteRel32Jump(std::uint8_t* from, const std::uint8_t* to, std::size_t pad_len) {
    MakeWritable(from, pad_len);
    const auto rel = static_cast<std::int32_t>(to - (from + 5));
    from[0] = 0xe9;
    std::memcpy(from + 1, &rel, 4);
    if (pad_len > 5) std::memset(from + 5, 0x90, pad_len - 5);
}

void PatchHeapHeadroom(unsigned char* image, std::uint64_t size) {
    static constexpr std::uint8_t kBlockOrig[5] = {0xbe, 0x00, 0x00, 0x19, 0x00};
    static constexpr std::uint8_t kPageOrig[5] = {0xbe, 0x00, 0x00, 0x05, 0x00};
    static constexpr std::uint64_t kMenuOrig = 0xa00000;

    if (size <= kMenuSize + 8) return;
    if (std::memcmp(image + kBlockHeapSize, kBlockOrig, 5) == 0 &&
        std::memcmp(image + kPageHeapSize, kPageOrig, 5) == 0) {
        const std::uint32_t block_2x = 0x320000;
        const std::uint32_t page_2x = 0xa0000;
        MakeWritable(image + kBlockHeapSize, 5);
        std::memcpy(image + kBlockHeapSize + 1, &block_2x, 4);
        MakeWritable(image + kPageHeapSize, 5);
        std::memcpy(image + kPageHeapSize + 1, &page_2x, 4);
    }
    std::uint64_t menu_cur = 0;
    std::memcpy(&menu_cur, image + kMenuSize, sizeof(menu_cur));
    if (menu_cur == kMenuOrig) {
        const std::uint64_t menu_2x = 0x1400000;
        MakeWritable(image + kMenuSize, sizeof(menu_2x));
        std::memcpy(image + kMenuSize, &menu_2x, sizeof(menu_2x));
    }
}

} // namespace

void PatchImage(unsigned char* image, std::uint64_t size) {
    if (!image || size <= kGameDataMan + 8) return;
    PatchHeapHeadroom(image, size);

    if (std::memcmp(image + kPlayTimeAdd, kPlayTimeOrig, sizeof(kPlayTimeOrig)) != 0 ||
        std::memcmp(image + kCharaStep, kCharaStepPrologue, sizeof(kCharaStepPrologue)) != 0) {
        return;
    }

    const std::size_t page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    const auto base = reinterpret_cast<std::uintptr_t>(image + kPlayTimeAdd) & ~(page - 1);
    std::uint8_t* stub_page = nullptr;
    for (std::uintptr_t delta = page; delta < 0x6000'0000ull && !stub_page; delta += page) {
        void* p = mmap(reinterpret_cast<void*>(base - delta), page, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != MAP_FAILED) stub_page = static_cast<std::uint8_t*>(p);
    }
    if (!stub_page) return;

    g_image = image;
    std::uint8_t* out = stub_page;
    auto emit = [&](const void* b, std::size_t n) { std::memcpy(out, b, n); out += n; };
    auto emit_u64 = [&](std::uint64_t v) { emit(&v, 8); };

    // 1. PlayTimeAdd stub: passes xmm1 in xmm0 to PlayTimeStep and returns result in rcx
    std::uint8_t* play_stub = out;
    static constexpr std::uint8_t kPlayPrefix[] = {
        0x50, 0x52, 0x56, 0x57, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x53,
        0x48, 0x89, 0xe3,                         // mov rbx, rsp
        0x48, 0x83, 0xe4, 0xf0,                   // and rsp, -16
        0x48, 0x81, 0xec, 0x80, 0x00, 0x00, 0x00, // sub rsp, 128
        0xc5, 0xfa, 0x7f, 0x4c, 0x24, 0x10,       // vmovdqu [rsp+0x10], xmm1
        0xc5, 0xfa, 0x7f, 0x54, 0x24, 0x20,       // vmovdqu [rsp+0x20], xmm2
        0xc5, 0xfa, 0x7f, 0x5c, 0x24, 0x30,       // vmovdqu [rsp+0x30], xmm3
        0xc5, 0xfa, 0x7f, 0x64, 0x24, 0x40,       // vmovdqu [rsp+0x40], xmm4
        0xc5, 0xfa, 0x7f, 0x6c, 0x24, 0x50,       // vmovdqu [rsp+0x50], xmm5
        0xc5, 0xfa, 0x7f, 0x74, 0x24, 0x60,       // vmovdqu [rsp+0x60], xmm6
        0xc5, 0xfa, 0x7f, 0x7c, 0x24, 0x70,       // vmovdqu [rsp+0x70], xmm7
        0xc5, 0xf8, 0x28, 0xc1,                   // vmovaps xmm0, xmm1
        0x48, 0xb8,                               // movabs rax, PlayTimeStep
    };
    emit(kPlayPrefix, sizeof(kPlayPrefix));
    emit_u64(reinterpret_cast<std::uint64_t>(&PlayTimeStep));
    static constexpr std::uint8_t kPlaySuffix[] = {
        0xff, 0xd0,                               // call rax
        0x48, 0x89, 0xc1,                         // mov rcx, rax
        0xc5, 0xfa, 0x6f, 0x4c, 0x24, 0x10,       // vmovdqu xmm1, [rsp+0x10]
        0xc5, 0xfa, 0x6f, 0x54, 0x24, 0x20,       // vmovdqu xmm2, [rsp+0x20]
        0xc5, 0xfa, 0x6f, 0x5c, 0x24, 0x30,       // vmovdqu xmm3, [rsp+0x30]
        0xc5, 0xfa, 0x6f, 0x64, 0x24, 0x40,       // vmovdqu xmm4, [rsp+0x40]
        0xc5, 0xfa, 0x6f, 0x6c, 0x24, 0x50,       // vmovdqu xmm5, [rsp+0x50]
        0xc5, 0xfa, 0x6f, 0x74, 0x24, 0x60,       // vmovdqu xmm6, [rsp+0x60]
        0xc5, 0xfa, 0x6f, 0x7c, 0x24, 0x70,       // vmovdqu xmm7, [rsp+0x70]
        0x48, 0x89, 0xdc,                         // mov rsp, rbx
        0x5b, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, 0x5f, 0x5e, 0x5a, 0x58,
    };
    emit(kPlaySuffix, sizeof(kPlaySuffix));
    WriteRel32Jump(out, image + kPlayTimeAdd + sizeof(kPlayTimeOrig), 5);
    out += 5;
    WriteRel32Jump(image + kPlayTimeAdd, play_stub, sizeof(kPlayTimeOrig));

    // 2. CharaStep save-corruption guard stub
    std::uint8_t* chara_stub = out;
    static constexpr std::uint8_t kCharaPrefix[] = {
        0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51, // push rdi rsi rdx rcx r8 r9 (48B + 8B ret = 56B)
        0x48, 0x81, 0xec, 0x88, 0x00, 0x00, 0x00,       // sub rsp, 136 (192B aligned)
        0xc5, 0xfa, 0x7f, 0x04, 0x24,                   // vmovdqu [rsp], xmm0
        0xc5, 0xfa, 0x7f, 0x4c, 0x24, 0x10,             // vmovdqu [rsp+16], xmm1
        0x48, 0xb8,                                     // movabs rax, CharaStepHook
    };
    emit(kCharaPrefix, sizeof(kCharaPrefix));
    emit_u64(reinterpret_cast<std::uint64_t>(&CharaStepHook));
    static constexpr std::uint8_t kCharaSuffix[] = {
        0xff, 0xd0,
        0xc5, 0xfa, 0x6f, 0x04, 0x24,
        0xc5, 0xfa, 0x6f, 0x4c, 0x24, 0x10,
        0x48, 0x81, 0xc4, 0x88, 0x00, 0x00, 0x00,
        0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f,
    };
    emit(kCharaSuffix, sizeof(kCharaSuffix));
    emit(kCharaStepPrologue, sizeof(kCharaStepPrologue));
    WriteRel32Jump(out, image + kCharaStep + sizeof(kCharaStepPrologue), 5);
    out += 5;
    WriteRel32Jump(image + kCharaStep, chara_stub, sizeof(kCharaStepPrologue));

    // 3. Havok collector pushBack runaway guard stub
    if (size > kPushBack96 + sizeof(kPushBack96Prologue) &&
        std::memcmp(image + kPushBack96, kPushBack96Prologue, sizeof(kPushBack96Prologue)) == 0) {
        std::uint8_t* push_stub = out;
        static constexpr std::uint8_t kPushPrefix[] = {
            0x48, 0xb8, // movabs rax, PushBackSafe
        };
        emit(kPushPrefix, sizeof(kPushPrefix));
        emit_u64(reinterpret_cast<std::uint64_t>(&PushBackSafe));
        static constexpr std::uint8_t kPushSuffix[] = {
            0xff, 0xe0, // jmp rax
        };
        emit(kPushSuffix, sizeof(kPushSuffix));
        WriteRel32Jump(image + kPushBack96, push_stub, sizeof(kPushBack96Prologue));
    }

    mprotect(stub_page, page, PROT_READ | PROT_EXEC);
    std::printf("[BBPort] High-FPS play-time clock, CharaId save guard, Havok collector safety & 2x heap headroom installed.\n");
}

} // namespace BbEngineFixes
