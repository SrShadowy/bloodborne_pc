// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <sys/mman.h>
#include <unistd.h>

namespace BbCodePatch {

inline std::uint8_t* AllocateNearPage(const void* anchor, std::uintptr_t max_dist = 0x6000'0000ull) {
    const auto page_sz = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    const auto base = reinterpret_cast<std::uintptr_t>(anchor) & ~(page_sz - 1);
    for (std::uintptr_t delta = page_sz; delta < max_dist; delta += page_sz) {
        for (int dir : {-1, 1}) {
            const std::uintptr_t cand = dir < 0 ? (base > delta ? base - delta : 0) : base + delta;
            if (!cand) continue;
            void* p = mmap(reinterpret_cast<void*>(cand), page_sz, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if (p != MAP_FAILED) {
                return static_cast<std::uint8_t*>(p);
            }
        }
    }
    return nullptr;
}

inline void SealExecPage(void* page) {
    const auto page_sz = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    mprotect(page, page_sz, PROT_READ | PROT_EXEC);
}

inline void WriteRel32Jump(std::uint8_t* from, const std::uint8_t* to, std::size_t pad_len = 5) {
    const auto rel = static_cast<std::int32_t>(to - (from + 5));
    from[0] = 0xe9;
    std::memcpy(from + 1, &rel, sizeof(rel));
    if (pad_len > 5) {
        std::memset(from + 5, 0x90, pad_len - 5);
    }
}

using HookDispatchFn = std::int64_t (*)(std::uint64_t id, const std::uint64_t* saved_regs);

inline void EmitPrologueHook(std::uint8_t*& c, std::uint8_t* site, const std::uint8_t* displaced,
                             std::size_t n, HookDispatchFn dispatch, std::uint64_t id,
                             const std::uint8_t* resume_target = nullptr) {
    std::uint8_t* stub = c;
    static constexpr std::uint8_t kSave[] = {
        0x50, 0x9c,                                                   // push rax; pushfq
        0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51,             // push rdi rsi rdx rcx r8 r9
        0x48, 0x81, 0xec, 0x88, 0x00, 0x00, 0x00,                   // sub rsp, 136
        0xf3, 0x0f, 0x7f, 0x44, 0x24, 0x00,                         // movdqu [rsp], xmm0
        0xf3, 0x0f, 0x7f, 0x4c, 0x24, 0x10,                         // movdqu [rsp+16], xmm1
        0xf3, 0x0f, 0x7f, 0x54, 0x24, 0x20,                         // movdqu [rsp+32], xmm2
        0xf3, 0x0f, 0x7f, 0x5c, 0x24, 0x30,                         // movdqu [rsp+48], xmm3
        0xf3, 0x0f, 0x7f, 0x64, 0x24, 0x40,                         // movdqu [rsp+64], xmm4
        0xf3, 0x0f, 0x7f, 0x6c, 0x24, 0x50,                         // movdqu [rsp+80], xmm5
        0xf3, 0x0f, 0x7f, 0x74, 0x24, 0x60,                         // movdqu [rsp+96], xmm6
        0xf3, 0x0f, 0x7f, 0x7c, 0x24, 0x70,                         // movdqu [rsp+112], xmm7
        0x48, 0xc7, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0, 0, 0, 0, // mov qword [rsp+128], 0
        0x48, 0x8d, 0xb4, 0x24, 0x88, 0x00, 0x00, 0x00,             // lea rsi, [rsp+136]
    };
    static constexpr std::uint8_t kRestore[] = {
        0x49, 0x89, 0xc3,                                           // mov r11, rax
        0x4c, 0x8b, 0x94, 0x24, 0x80, 0x00, 0x00, 0x00,             // mov r10, [rsp+128]
        0xf3, 0x0f, 0x6f, 0x44, 0x24, 0x00,                         // movdqu xmm0, [rsp]
        0xf3, 0x0f, 0x6f, 0x4c, 0x24, 0x10,                         // movdqu xmm1, [rsp+16]
        0xf3, 0x0f, 0x6f, 0x54, 0x24, 0x20,                         // movdqu xmm2, [rsp+32]
        0xf3, 0x0f, 0x6f, 0x5c, 0x24, 0x30,                         // movdqu xmm3, [rsp+48]
        0xf3, 0x0f, 0x6f, 0x64, 0x24, 0x40,                         // movdqu xmm4, [rsp+64]
        0xf3, 0x0f, 0x6f, 0x6c, 0x24, 0x50,                         // movdqu xmm5, [rsp+80]
        0xf3, 0x0f, 0x6f, 0x74, 0x24, 0x60,                         // movdqu xmm6, [rsp+96]
        0xf3, 0x0f, 0x6f, 0x7c, 0x24, 0x70,                         // movdqu xmm7, [rsp+112]
        0x48, 0x81, 0xc4, 0x88, 0x00, 0x00, 0x00,                   // add rsp, 136
        0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f,             // pop r9 r8 rcx rdx rsi rdi
        0x4d, 0x85, 0xdb,                                           // test r11, r11
        0x74, 0x06,                                                 // jz +6
        0x9d, 0x58,                                                 // popfq; pop rax
        0x4c, 0x89, 0xd0,                                           // mov rax, r10
        0xc3,                                                       // ret
        0x9d, 0x58,                                                 // popfq; pop rax
    };
    std::memcpy(c, kSave, sizeof(kSave));
    c += sizeof(kSave);
    *c++ = 0x48;
    *c++ = 0xbf; // movabs rdi, id
    std::memcpy(c, &id, 8);
    c += 8;
    const auto fn = reinterpret_cast<std::uint64_t>(dispatch);
    *c++ = 0x48;
    *c++ = 0xb8; // movabs rax, fn
    std::memcpy(c, &fn, 8);
    c += 8;
    *c++ = 0xff;
    *c++ = 0xd0; // call rax
    std::memcpy(c, kRestore, sizeof(kRestore));
    c += sizeof(kRestore);
    if (n) {
        std::memcpy(c, displaced, n);
        c += n;
    }
    const std::uint8_t jmp_rip[6] = {0xff, 0x25, 0, 0, 0, 0};
    std::memcpy(c, jmp_rip, 6);
    c += 6;
    const auto resume = reinterpret_cast<std::uint64_t>(resume_target ? resume_target : (site + n));
    std::memcpy(c, &resume, 8);
    c += 8;
    WriteRel32Jump(site, stub, n ? n : 5);
}

} // namespace BbCodePatch
