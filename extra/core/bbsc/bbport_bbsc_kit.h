// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// Returns $BB_GPU_USER_DIR or $BB_USER_DIR when set and non-empty, else nullptr.
extern "C" const char* extra_bbsc_save_root(void);

// BBSC save directory automatic resolution across title IDs (CUSA00207, CUSA03173, etc.)
// [Teefpyre patch: dynamic save folder detection]
extern "C" void extra_bbsc_setup_save_dir(void);

// Hunter's Kit fix: the bell's param rows (goods + SpEffect) are scannable once the game
// data is mapped, but upstream's cap of 36 attempts (3 min) could expire if the user stayed
// at the title screen. This worker calls prepare every 2 s until ready turns true.
extern "C" void extra_bbsc_bell_scan_forever(std::atomic<bool>* ready, void (*prepare)(void));

// Safely reads memory even on DMABUF or protected pages, preventing SIGSEGV/SIGBUS.
extern "C" bool extra_safe_read(void* dst, const void* src, size_t size);

// Verifies if a memory mapping should be scanned (skips read-only and host .so libraries).
extern "C" bool extra_bbsc_check_mapping(const char* param, const char* line);

// Memory scan for BBSC param tables and symbols [Teefpyre patch / Linux BBSC Fix]:
// Handles low mappings (< GuestMin) via /proc/self/maps and guest mappings (>= GuestMin)
// via runtime_memory_read_backing.
extern "C" void extra_bbsc_find_in_memory(
    const std::uint8_t* needle_ptr, std::size_t needle_len,
    std::uint64_t lo, std::uint64_t hi, std::size_t max_hits,
    std::vector<std::uint64_t>* hits);