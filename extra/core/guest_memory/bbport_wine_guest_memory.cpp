// SPDX-License-Identifier: GPL-2.0-or-later
// [extra] Wine/Proton: the guest memory model the Windows build needs there (see the header).
//
// Why these three, measured on Wine 11.21 + radv/amdgpu (RX 9070 XT, 2026-11-06):
//  - BB_GUEST_MEMORY=private: Wine's winvulkan imports VirtualAlloc'd memory
//    (VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT) but not views of sections, and the
//    private-region model already exists for Windows drivers; its startup probe passes.
//  - BB_IMAGE_TRAPS=0 and BB_LAYER_WRITE_TRAPS=0: one PAGE_READONLY page inside an imported range
//    makes the kernel reject the next submission ("radv/amdgpu: The CS has been rejected") and
//    loses the device for the rest of the run. Both trap families mprotect guest pages, and each
//    one alone is enough to lose it (both measured).
// With these the port boots to the title screen and exits cleanly; with either trap family on it
// exits 23.
//
// NOT set: BB_UNIFIED_MEMORY. The card is still discrete, and forcing unified memory made the port
// skip every VRAM mirror (0 MiB valid in VRAM against 193 MiB with 90 mirrors) and read the game's
// memory over PCIe: 116.7 ms a frame against 100.0 ms, with 950 ms stalls. Only the traps go.
//
// Trade-off, not a fix: an unannounced CPU write under a texture is no longer noticed, so such a
// texture can show stale data until the game announces a write of its range.
#include "core/guest_memory/bbport_wine_guest_memory.h"

#include <stdio.h>

#ifdef _WIN32
#include <windows.h>

extern "C" int extra_apply_wine_guest_memory_defaults(void) {
    static const bool applied = [] {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (!ntdll || GetProcAddress(ntdll, "wine_get_version") == nullptr) {
            return false; // real Windows: its drivers import section views, traps work
        }
        // overwrite = 0: what run.py, the launcher or the player set stays as it is.
        setenv("BB_GUEST_MEMORY", "private", 0);
        setenv("BB_IMAGE_TRAPS", "0", 0);
        setenv("BB_LAYER_WRITE_TRAPS", "0", 0);
        printf("Wine/Proton: private VirtualAlloc imports, write traps off (the kernel rejects "
               "submissions over read-only imported pages); VRAM mirrors stay on. A BB_* variable "
               "set wins over these defaults.\n");
        fflush(stdout);
        return true;
    }();
    return applied ? 1 : 0;
}
#else
extern "C" int extra_apply_wine_guest_memory_defaults(void) {
    return 0; // Linux: run.sh and the native build decide (dma-buf, host import)
}
#endif
