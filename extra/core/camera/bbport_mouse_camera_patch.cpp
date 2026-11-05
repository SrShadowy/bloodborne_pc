// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the mouse camera's binary hooks. The game's camera update is replaced by a stub that
// copies the instruction it overwrote, saves the vector registers, calls ApplyMouseTurn (which
// writes the camera from the mouse), restores them and jumps back. The three lock-on hold sites
// get a stub that runs the original store only while the port is not holding the camera itself.
// Every site is verified byte-for-byte first, so a game build that does not match stays untouched.
#include "bbport_mouse_camera.h"

#include <cstdio>
#include <cstring>
#include <iterator>

#include "bbport_code_patch.h"
#include "bbport_mouse_camera_internal.h"

namespace BbMouseCamera {
namespace detail {
namespace {

/// The instruction at kTurnStore: "vmovss [r13+0x140], xmm0" then "vextractps [r13+0x144], xmm1, 1".
constexpr std::uint8_t kTurnBytes[19] = {
    0xc4, 0xc1, 0x7a, 0x11, 0x85, 0x40, 0x01, 0x00, 0x00, 0xc4, 0xc3, 0x79, 0x17,
    0x8d, 0x44, 0x01, 0x00, 0x00, 0x01,
};
/// The camera update's prologue ("push rbp; mov rbp, rsp; push r15/r14/r13/r12/rbx;
/// sub rsp, 0x3d8").
constexpr std::uint8_t kUpdatePrologue[20] = {
    0x55, 0x48, 0x89, 0xe5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41,
    0x54, 0x53, 0x48, 0x81, 0xec, 0xd8, 0x03, 0x00, 0x00,
};
/// The last store each auto-rotation exit makes before jumping back into the update.
constexpr std::uint8_t kRampExitBytes[2][14] = {
    {0xc4, 0xc1, 0x7a, 0x11, 0x85, 0x40, 0x01, 0x00, 0x00, 0xe9, 0x9d, 0xd4, 0xff, 0xff},
    {0xc4, 0xc1, 0x7a, 0x11, 0x85, 0x40, 0x01, 0x00, 0x00, 0xe9, 0x9a, 0xd3, 0xff, 0xff},
};
/// The stores the lock-on hold makes: pitch, then the two stick-driven axes.
constexpr std::uint8_t kHoldBytes[3][9] = {
    {0xc4, 0xc1, 0x7a, 0x11, 0x85, 0x44, 0x01, 0x00, 0x00}, // vmovss [r13+0x144], xmm0
    {0xc4, 0xc1, 0x7a, 0x11, 0x8d, 0x30, 0x01, 0x00, 0x00}, // vmovss [r13+0x130], xmm1
    {0xc4, 0xc1, 0x7a, 0x11, 0x85, 0x94, 0x02, 0x00, 0x00}, // vmovss [r13+0x294], xmm0
};

/// The bytes of the instruction the turn hook replaces, and of the jump that resumes the game.
constexpr std::size_t kTurnJumpBytes = 5;
/// How many bytes each lock-on hold site has replaced.
constexpr std::size_t kHoldJumpBytes = 9;

// The stub saves the registers the game's convention makes the caller keep, then the xmm ones the
// camera math uses (all 16, 16 bytes each on a 16-byte aligned stack).
constexpr std::uint8_t kStubSaveRegs[] = {
    0x50, 0x51, 0x52, 0x56, 0x57, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53, 0x53,
    0x48, 0x89, 0xe3, 0x48, 0x83, 0xe4, 0xf0, 0x48, 0x81, 0xec, 0x00, 0x01, 0x00, 0x00,
    0xc5, 0xfa, 0x7f, 0x04, 0x24, 0xc5, 0xfa, 0x7f, 0x4c, 0x24, 0x10,
    0xc5, 0xfa, 0x7f, 0x54, 0x24, 0x20, 0xc5, 0xfa, 0x7f, 0x5c, 0x24, 0x30,
    0xc5, 0xfa, 0x7f, 0x64, 0x24, 0x40, 0xc5, 0xfa, 0x7f, 0x6c, 0x24, 0x50,
    0xc5, 0xfa, 0x7f, 0x74, 0x24, 0x60, 0xc5, 0xfa, 0x7f, 0x7c, 0x24, 0x70,
    0xc5, 0x7a, 0x7f, 0x84, 0x24, 0x80, 0, 0, 0, 0xc5, 0x7a, 0x7f, 0x8c, 0x24, 0x90, 0, 0, 0,
    0xc5, 0x7a, 0x7f, 0x94, 0x24, 0xa0, 0, 0, 0, 0xc5, 0x7a, 0x7f, 0x9c, 0x24, 0xb0, 0, 0, 0,
    0xc5, 0x7a, 0x7f, 0xa4, 0x24, 0xc0, 0, 0, 0, 0xc5, 0x7a, 0x7f, 0xac, 0x24, 0xd0, 0, 0, 0,
    0xc5, 0x7a, 0x7f, 0xb4, 0x24, 0xe0, 0, 0, 0, 0xc5, 0x7a, 0x7f, 0xbc, 0x24, 0xf0, 0, 0, 0,
    // rdi = r13 (the camera), rsi = rbp, rdx = &stack[a0] (the last four xmm registers).
    0x4c, 0x89, 0xef, 0x48, 0x89, 0xee, 0x48, 0x8d, 0x94, 0x24, 0xa0, 0x00, 0x00, 0x00,
    0x48, 0xb8, // movabs rax, ApplyMouseTurn
};
constexpr std::uint8_t kStubCallAndRestore[] = {
    0xff, 0xd0, // call rax
    0xc5, 0xfa, 0x6f, 0x04, 0x24, 0xc5, 0xfa, 0x6f, 0x4c, 0x24, 0x10,
    0xc5, 0xfa, 0x6f, 0x54, 0x24, 0x20, 0xc5, 0xfa, 0x6f, 0x5c, 0x24, 0x30,
    0xc5, 0xfa, 0x6f, 0x64, 0x24, 0x40, 0xc5, 0xfa, 0x6f, 0x6c, 0x24, 0x50,
    0xc5, 0xfa, 0x6f, 0x74, 0x24, 0x60, 0xc5, 0xfa, 0x6f, 0x7c, 0x24, 0x70,
    0xc5, 0x7a, 0x6f, 0x84, 0x24, 0x80, 0, 0, 0, 0xc5, 0x7a, 0x6f, 0x8c, 0x24, 0x90, 0, 0, 0,
    0xc5, 0x7a, 0x6f, 0x94, 0x24, 0xa0, 0, 0, 0, 0xc5, 0x7a, 0x6f, 0x9c, 0x24, 0xb0, 0, 0, 0,
    0xc5, 0x7a, 0x6f, 0xa4, 0x24, 0xc0, 0, 0, 0, 0xc5, 0x7a, 0x6f, 0xac, 0x24, 0xd0, 0, 0, 0,
    0xc5, 0x7a, 0x6f, 0xb4, 0x24, 0xe0, 0, 0, 0, 0xc5, 0x7a, 0x6f, 0xbc, 0x24, 0xf0, 0, 0, 0,
    0x48, 0x89, 0xdc,                                     // mov rsp, rbx
    0x5b, 0x41, 0x5b, 0x41, 0x5a, 0x41, 0x59, 0x41, 0x58, // pop rbx/r11/r10/r9/r8
    0x5f, 0x5e, 0x5a, 0x59, 0x58,                         // pop rdi/rsi/rdx/rcx/rax
    0xc4, 0xc1, 0x7a, 0x10, 0x85, 0x40, 0x01, 0x00, 0x00, // vmovups xmm0, [r13+0x140]
};

// The hold stub: push rax; pushfq; movabs rax, &g_hold_active; cmp byte [rax], 0; jne +9.
constexpr std::uint8_t kHoldPrefix[] = {0x50, 0x9c, 0x48, 0xb8};
constexpr std::uint8_t kHoldCheck[] = {0x80, 0x38, 0x00, 0x75, 0x09};
constexpr std::uint8_t kHoldSuffix[] = {0x9d, 0x58}; // popfq; pop rax

/// Copies `bytes` at `out` and advances it.
void Emit(std::uint8_t*& out, const void* bytes, std::size_t size) {
    std::memcpy(out, bytes, size);
    out += size;
}

/// Each hook site still holds what the game shipped? (Any mismatch: no hooks at all.)
bool VerifyCameraSites(const unsigned char* image, std::uint64_t size) {
    if (!image || size <= kRampExits[1] + sizeof(kRampExitBytes[1])) {
        return false;
    }
    if (std::memcmp(image + kUpdate, kUpdatePrologue, sizeof(kUpdatePrologue)) != 0 ||
        std::memcmp(image + kTurnStore, kTurnBytes, sizeof(kTurnBytes)) != 0) {
        return false;
    }
    for (std::size_t i = 0; i < std::size(kRampExits); ++i) {
        if (std::memcmp(image + kRampExits[i], kRampExitBytes[i], sizeof(kRampExitBytes[i])) != 0) {
            return false;
        }
    }
    for (std::size_t i = 0; i < std::size(kHoldSites); ++i) {
        if (std::memcmp(image + kHoldSites[i], kHoldBytes[i], sizeof(kHoldBytes[i])) != 0) {
            return false;
        }
    }
    return true;
}

/// The stub that runs in place of the camera store: store as the game did, call the port's turn,
/// resume.
std::uint8_t* EmitTurnStoreStub(std::uint8_t*& out, const std::uint8_t* resume_addr) {
    std::uint8_t* const stub = out;
    Emit(out, kTurnBytes, sizeof(kTurnBytes));
    Emit(out, kStubSaveRegs, sizeof(kStubSaveRegs));
    const auto function = reinterpret_cast<std::uint64_t>(&ApplyMouseTurn);
    Emit(out, &function, sizeof(function));
    Emit(out, kStubCallAndRestore, sizeof(kStubCallAndRestore));
    BbCodePatch::WriteRel32Jump(out, resume_addr, kTurnJumpBytes);
    out += kTurnJumpBytes;
    return stub;
}

/// The stub that runs in place of a lock-on hold store: skip it while the port holds the camera.
std::uint8_t* EmitHoldStub(std::uint8_t*& out, const std::uint8_t* orig_bytes,
                           const std::uint8_t* resume_addr) {
    std::uint8_t* const stub = out;
    Emit(out, kHoldPrefix, sizeof(kHoldPrefix));
    const auto flag = reinterpret_cast<std::uint64_t>(&g_hold_active);
    Emit(out, &flag, sizeof(flag));
    Emit(out, kHoldCheck, sizeof(kHoldCheck));
    Emit(out, orig_bytes, kHoldJumpBytes);
    Emit(out, kHoldSuffix, sizeof(kHoldSuffix));
    BbCodePatch::WriteRel32Jump(out, resume_addr, kTurnJumpBytes);
    out += kTurnJumpBytes;
    return stub;
}

} // namespace
} // namespace detail

void PatchImage(unsigned char* image, std::uint64_t size) {
    if (!detail::VerifyCameraSites(image, size)) {
        return;
    }
    std::uint8_t* const stub_page = BbCodePatch::AllocateNearPage(image + detail::kTurnStore);
    if (!stub_page) {
        return;
    }

    std::uint8_t* out = stub_page;
    std::uint8_t* const turn_stub = detail::EmitTurnStoreStub(out, image + detail::kTurnResume);
    std::uint8_t* hold_stubs[std::size(detail::kHoldSites)]{};
    for (std::size_t i = 0; i < std::size(detail::kHoldSites); ++i) {
        hold_stubs[i] = detail::EmitHoldStub(out, detail::kHoldBytes[i],
                                             image + detail::kHoldSites[i] + detail::kHoldJumpBytes);
    }
    BbCodePatch::SealExecPage(stub_page);

    BbCodePatch::WriteRel32Jump(image + detail::kTurnStore, turn_stub, sizeof(detail::kTurnBytes));
    for (std::size_t i = 0; i < std::size(detail::kHoldSites); ++i) {
        BbCodePatch::WriteRel32Jump(image + detail::kHoldSites[i], hold_stubs[i],
                                    detail::kHoldJumpBytes);
    }
    detail::g_installed.store(true, std::memory_order_release);
    detail::RefreshHoldState();
    std::printf("[BBPort] 1:1 raw mouse camera & auto-rotation hooks installed.\n");
}

} // namespace BbMouseCamera
