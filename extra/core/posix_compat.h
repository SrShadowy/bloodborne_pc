// SPDX-License-Identifier: GPL-2.0-or-later
// [extra] POSIX shims for the C++ side of extra/ on Windows. bbport_posix_c.h is C-only
// (#ifndef __cplusplus), so the C++ files declare the bb_* implementations from
// gpu/platform/win32/posix_files.cpp themselves. memmem is already an inline in the forced
// bbport_win32.h. On Linux these come from the real headers.
#pragma once

#ifdef _WIN32
#include <cstddef>
#include <cstdint>

extern "C" {
ssize_t bb_pread(int fd, void* buffer, size_t size, int64_t offset);
ssize_t bb_pwrite(int fd, const void* buffer, size_t size, int64_t offset);
int memfd_create(const char* name, unsigned flags);
}

#define MFD_CLOEXEC 1
#define pread bb_pread
#define pwrite bb_pwrite
#else
#include <unistd.h>
#include <sys/mman.h>
#endif
