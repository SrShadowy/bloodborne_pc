// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <atomic>

// lives here instead of bloodborne_re.cpp. The launcher points the user folder at
// $BB_GPU_USER_DIR (AppImage: ~/.local/share/bbport/user); upstream's own bases list only the
// portable <cwd>/user and %APPDATA%\shadPS4, so without this root BBSC found no save folder to
// back up and never gave the kit. Reading the environment fresh (not the cached
// Common::FS::GetUserPath, computed at static init) also covers a UserDir cached before the
// variable was set.
// Returns $BB_GPU_USER_DIR when set and non-empty, else nullptr (nothing extra to search). The
// pointer is getenv's own, valid for the process; safe from any thread (no setenv in the game).
extern "C" const char* extra_bbsc_save_root(void);

// Hunter's Kit fix: the bell's param rows (goods + SpEffect) are scannable once the game
// data is mapped, but upstream's cap of 36 attempts (3 min) could expire if the user stayed
// at the title screen. This worker calls prepare every 5 s until ready turns true.
extern "C" void extra_bbsc_bell_scan_forever(std::atomic<bool>* ready, void (*prepare)(void));

// Safely reads memory even on DMABUF or protected pages, preventing SIGSEGV/SIGBUS.
extern "C" bool extra_safe_read(void* dst, const void* src, size_t size);

// Verifies if a memory mapping should be scanned (skips read-only and host .so libraries).
extern "C" bool extra_bbsc_check_mapping(const char* param, const char* line);