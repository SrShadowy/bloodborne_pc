// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: what the disassembler's files share. view_disasm.cpp owns the state and the public API,
// view_disasm_render.cpp draws the address bar and the instruction table, and view_disasm_menu.cpp
// the row's context menu. Only this module needs this header.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace UI::DisasmDetail {

// ---- The guest's addresses ----

/// Where the game's executable is mapped in the guest (its ELF base).
constexpr std::uintptr_t kEbootBase = 0x00400000;
/// Where the runtime maps that base (0x00400000 becomes 0x800000000).
constexpr std::uintptr_t kRuntimeBase = 0x800000000ULL;
/// Anything below this is not worth following (null-ish pointers, headers).
constexpr std::uintptr_t kMinFollowable = 0x10000;
/// How far back a symbol lookup walks to find the function that owns an address.
constexpr std::size_t kSymbolSpan = 0x1000;

// ---- The view's limits ----

/// x86-64 instructions are at most 15 bytes.
constexpr std::size_t kMaxInstructionBytes = 15;
/// How much code the table reads (and decodes) from the current address.
constexpr std::size_t kCodeChunkBytes = 512;
/// One row's address, bytes and text: "0x0000000000000  90 90  ...  nop".
constexpr std::size_t kRowTextBytes = 384;
/// The decoded rows per window height: taller windows show more.
constexpr float kTableHeightSmall = 300.0f;
constexpr float kTableHeightLarge = 500.0f;
constexpr int kRowsSmall = 25;
constexpr int kRowsMedium = 40;
constexpr int kRowsLarge = 60;

/// Everything the view remembers between frames (the patched instructions included).
struct State {
    std::uintptr_t current = kEbootBase; ///< the address the table starts at
    std::uintptr_t selected = 0;         ///< the instruction last clicked
    std::string selected_line;           ///< what Ctrl+C copies
    bool show_symbols = true;
    std::string patch_status;            ///< the "[OK] ..." after the toolbar
    /// The patched instructions and the bytes to restore them (the first patch's originals).
    std::unordered_map<std::uintptr_t, std::vector<std::uint8_t>> patched;
};

State& GetState();

/// Translates an eboot address the user typed into where it lives at runtime.
std::uintptr_t ToRuntime(std::uintptr_t address);

/// The decoded length of an instruction, clamped to what x86-64 allows.
std::size_t InstructionLength(const void* instruction);

/// The toolbar (title, address field, RIP/eboot buttons, symbols checkbox). Reads `code` from the
/// address the field ends on; false when that memory cannot be read (nothing else is drawn).
bool RenderToolbar(std::uintptr_t& address, bool standalone,
                   std::uint8_t (&code)[kCodeChunkBytes]);

/// The table of instructions decoded from `code`, whose first row is `address`
/// (view_disasm_render.cpp).
void RenderTable(std::uintptr_t& address, std::uint8_t* code, float height, bool standalone);

/// One row's context menu (view_disasm_menu.cpp).
void DrawInstructionMenu(std::uintptr_t address, const std::uint8_t* code, std::size_t inst_len,
                         const std::string& raw_text, const std::string& pc_text,
                         const std::string& bytes_text, const std::string& full_line, bool has_bp,
                         bool is_patched, std::uintptr_t& followed_address);

} // namespace UI::DisasmDetail
