// SPDX-License-Identifier: GPL-2.0-or-later
#include "mem_scanner.h"
#include "gpu/bbgpu.h"

#include <cstdio>
#include <cstring>
#include <csetjmp>
#include <csignal>
#include <sys/uio.h>
#include <sys/mman.h>
#include <unistd.h>

extern "C" __thread sigjmp_buf* runtime_fault_recover;
extern "C" int runtime_memory_is_mapped(uintptr_t address, uint64_t size);
extern "C" void runtime_memory_read_backing(uintptr_t address, void* data, uint64_t size);
extern "C" int runtime_memory_write_backing(uintptr_t address, const void* data, uint64_t size);

namespace Debugger {

bool MemoryScanner::ReadMemory(uintptr_t addr, void* dest, size_t size) {
    if (!addr || !dest || size == 0) return false;

    // 1. Fast, kernel-level atomic read (zero SIGSEGV signals on unmapped / invalid pages)
    struct iovec local_iov{dest, size};
    struct iovec remote_iov{reinterpret_cast<void*>(addr), size};
    const ssize_t n = process_vm_readv(getpid(), &local_iov, 1, &remote_iov, 1, 0);
    if (n == static_cast<ssize_t>(size)) {
        return true;
    }

    // 2. If process_vm_readv failed or was partial, check if this is guest memory.
    // If GPU page tracking marked this page PROT_NONE, read directly from host backing view!
    if (runtime_memory_is_mapped(addr, size)) {
        runtime_memory_read_backing(addr, dest, size);
        return true;
    }

    // 3. If process_vm_readv transferred a non-zero prefix, fill remaining with 0
    if (n > 0) {
        std::memset(reinterpret_cast<uint8_t*>(dest) + n, 0, size - n);
        return true;
    }

    // 4. If process_vm_readv failed with EFAULT (unmapped page), do not attempt memcpy
    // which would trigger an unnecessary SIGSEGV exception storm.
    if (n < 0 && errno == EFAULT) {
        return false;
    }

    // 5. Last resort fallback (e.g. host memory where process_vm_readv is restricted)
    sigjmp_buf recover;
    if (sigsetjmp(recover, 1) != 0) {
        runtime_fault_recover = nullptr;
        return false;
    }
    runtime_fault_recover = &recover;
    std::memcpy(dest, reinterpret_cast<const void*>(addr), size);
    runtime_fault_recover = nullptr;
    return true;
}

bool MemoryScanner::WriteMemory(uintptr_t addr, const void* src, size_t size) {
    if (!addr || !src || size == 0) return false;

    // 1. Write to guest backing store if direct/flexible memory (bypasses GPU page protection)
    if (runtime_memory_write_backing(addr, src, size)) {
        return true;
    }

    // 2. Try direct write with sigsetjmp recovery
    sigjmp_buf recover;
    if (sigsetjmp(recover, 1) == 0) {
        runtime_fault_recover = &recover;
        std::memcpy(reinterpret_cast<void*>(addr), src, size);
        runtime_fault_recover = nullptr;
        return true;
    }
    runtime_fault_recover = nullptr;

    // 3. Direct write faulted (likely a read-only code page in eboot.bin for patches/breakpoints).
    // Temporarily unprotect page with mprotect, write bytes, and restore protection.
    constexpr uintptr_t PAGE_MASK = ~static_cast<uintptr_t>(4095);
    const uintptr_t page_start = addr & PAGE_MASK;
    const size_t page_size = ((addr + size + 4095) & PAGE_MASK) - page_start;

    if (mprotect(reinterpret_cast<void*>(page_start), page_size, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
        bool ok = false;
        if (sigsetjmp(recover, 1) == 0) {
            runtime_fault_recover = &recover;
            std::memcpy(reinterpret_cast<void*>(addr), src, size);
            runtime_fault_recover = nullptr;
            ok = true;
        } else {
            runtime_fault_recover = nullptr;
        }
        mprotect(reinterpret_cast<void*>(page_start), page_size, PROT_READ | PROT_EXEC);
        return ok;
    }

    return false;
}

std::vector<MemoryRegion> MemoryScanner::QueryRegions(ScanScope scope) {
    std::vector<MemoryRegion> regions;
    FILE* f = std::fopen("/proc/self/maps", "r");
    if (!f) return regions;

    const uintptr_t img_base = bbgpu_get_guest_image_base();
    const uint64_t img_size = bbgpu_get_guest_image_size();
    const uintptr_t img_end = (img_base && img_size) ? (img_base + img_size) : (img_base + 0x20000000ULL);

    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        uintptr_t start = 0, end = 0;
        char perms[5] = {0};
        char path[256] = {0};
        int parsed = std::sscanf(line, "%lx-%lx %4s %*x %*s %*d %255s", &start, &end, perms, path);
        if (parsed >= 3) {
            bool readable = (perms[0] == 'r');
            if (!readable) {
                // If GPU dirty tracking marked this guest page PROT_NONE, it can still be read safely via backing view
                if (runtime_memory_is_mapped(start, 4096)) {
                    readable = true;
                }
            }
            if (!readable) continue;

            // Filter out device nodes and kernel mappings
            if (parsed >= 4) {
                if (std::strncmp(path, "/dev/", 5) == 0 && std::strncmp(path, "/dev/shm", 8) != 0) continue;
                if (std::strcmp(path, "[vsyscall]") == 0 || std::strcmp(path, "[vvar]") == 0) continue;
            }

            if (scope == ScanScope::ExecutableOnly) {
                if (img_base != 0) {
                    if (start >= img_base && end <= img_end) {
                        regions.push_back({start, end});
                    }
                } else if (start >= 0x400000 && end <= 0x20000000) {
                    regions.push_back({start, end});
                }
            } else if (scope == ScanScope::GuestHeap) {
                // PS4 user / guest space is [0x400000, 0x100000000000)
                // Includes non-PIE [heap], eboot.bin, PRXs, direct memory, and flexible memory
                if (start >= 0x400000ULL && end <= 0x100000000000ULL) {
                    if (!regions.empty() && regions.back().end == start) {
                        regions.back().end = end;
                    } else {
                        regions.push_back({start, end});
                    }
                }
            } else {
                // FullProcess: avoid vsyscall and kernel space
                if (end <= 0x7fffffffffffULL) {
                    if (!regions.empty() && regions.back().end == start) {
                        regions.back().end = end;
                    } else {
                        regions.push_back({start, end});
                    }
                }
            }
        }
    }
    std::fclose(f);
    return regions;
}

} // namespace Debugger
