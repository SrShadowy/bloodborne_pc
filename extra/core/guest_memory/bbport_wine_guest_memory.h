// SPDX-License-Identifier: GPL-2.0-or-later
// [extra] Wine/Proton: the guest memory model the Windows build needs there.
//
// Under Wine the Windows path (VK_EXT_external_memory_host over VirtualAlloc'd memory) imports
// fine, but the Linux kernel behind it (radv/amdgpu userptr) rejects a command submission as soon
// as one page of an imported range is PAGE_READONLY. The port's write traps do exactly that with
// mprotect, so the Windows build died with a device loss (exit 23) at startup under Wine/Proton.
// These defaults are applied once at startup; a BB_* variable already set still wins.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Applies the Wine/Proton defaults (BB_GUEST_MEMORY=private, BB_IMAGE_TRAPS=0,
/// BB_LAYER_WRITE_TRAPS=0) before anything reads them: call first in main, before the
/// environment is logged. Returns 1 when it applied them, 0 on real Windows and on Linux
/// (nothing changed). BB_UNIFIED_MEMORY is deliberately not among them: the card is still a
/// discrete GPU (VRAM mirrors stay), only the traps that mprotect imported pages go.
int extra_apply_wine_guest_memory_defaults(void);

#ifdef __cplusplus
}
#endif
