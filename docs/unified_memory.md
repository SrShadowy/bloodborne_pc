# Unified memory (integrated GPUs, handhelds, Apple silicon)

On a GPU that shares the system's memory there is no "VRAM" to copy the game's data into: the
port reads the game's memory where the game wrote it. This is what the Steam Deck runs today, and
the mode a port to another unified-memory system (ARM handhelds, Macs through MoltenVK) should
keep.

## What changes on unified memory

`Vulkan::Instance::IsIntegrated()` decides it: the device type (`eIntegratedGpu`), or
`BB_UNIFIED_MEMORY=1` / `0` to override it (a driver that reports another type, a port to a
system where the type is wrong).

- **No VRAM mirrors of the game's data** (`GarlicInVram` is off): vertex, index, constant and
  storage buffers are the game's own memory, bound in place through the layer's memory module
  (`gpu/layer`). No promotions, no copies back, no mirror write traps.
- **No buffer write traps in normal play**: with no second copy, a CPU write needs no
  invalidation. Steam Deck, standing in the world (2026-10-23): `0 write faults/s`,
  `0 mirrors of 0 MiB`, `host copies 0/frame`, `write traps hit 0`.
- **Texture sources are read in place**: an upload detiles straight from the game's memory
  (`ObtainBufferForImage` is the in-place chunk, the detiler runs on the GPU); the CPU copies
  nothing.
- **Memory budget**: all heaps count, at least 8 GiB of the system are left to the system, and
  the texture collector follows the driver's current budget (`texture_cache_memory.cpp`).

What stays: images. The game's textures are tiled (PS4 tiling) and render targets are written by
the GPU in its own layouts, so each texture still has a `VkImage` that the GPU fills by detiling,
and CPU writes under an image are trapped (`BB_IMAGE_TRAPS`) so that it is uploaded again. These
traps fire on texture streaming, not every frame.

## What the guest memory needs from the driver

The game's memory must be GPU-visible at the game's own addresses. One of:

| Way | Where | Needs |
| --- | --- | --- |
| dma-buf chunks mapped at the game's addresses | Linux (Mesa: RADV, ANV, Turnip, Panfrost/PanVK) | `VK_EXT_external_memory_dma_buf`, export of host-visible memory |
| host memory imported by the GPU | Windows (AMD, Intel, NVIDIA), Linux fallback | `VK_EXT_external_memory_host`, `minImportedHostPointerAlignment` ≤ 64 KiB |
| Vulkan memory mapped at the game's addresses | Wine/Proton fallback, systems lacking dma-buf or host import | `VK_EXT_map_memory_placed`, `VK_KHR_map_memory2` |

A third way, Vulkan memory mapped at the game's addresses (`VK_EXT_map_memory_placed`), is
implemented as a fallback for Wine/Proton and environments that lack both `dma-buf` and working `external_memory_host`.

Either way needs `bufferDeviceAddress` too (paged bindings read through a page table) and 64-bit integer and
storage-buffer support in shaders.

## Wine/Proton (the Windows build on Linux)

Host memory import works there, but the kernel underneath does not: one page of an imported range
that the port made read-only (a write trap) makes `radv/amdgpu` reject the next command submission
(`radv/amdgpu: The CS has been rejected`) and the device is lost for the run — the port stopped with
exit 23 at startup. So on Wine/Proton the port picks, once at startup
(`extra/core/guest_memory/bbport_wine_guest_memory.cpp`, a `BB_*` variable you set wins):

| Variable | Value | Why |
| --- | --- | --- |
| `BB_GUEST_MEMORY` | `private` | Wine imports `VirtualAlloc`'d memory, not views of sections |
| `BB_IMAGE_TRAPS` | `0` | image write traps `mprotect` guest pages |
| `BB_LAYER_WRITE_TRAPS` | `0` | layer write traps `mprotect` guest pages |

Each trap family alone is enough to lose the device; both must stay off. `BB_UNIFIED_MEMORY` is
**not** set: the card is still discrete, and forcing it made the port skip every VRAM mirror
(0 MiB valid in VRAM against 193 MiB with 90 mirrors) and read the game's memory over PCIe —
116.7 ms a frame against 100.0 ms, with 950 ms stalls (RX 9070 XT, title screen, 2026-11-06).

The trade-off: an unannounced CPU write under a texture is no longer noticed, so such a texture can
show stale data until the game announces a write of its range. The native Linux build (`run.sh`)
keeps both trap families, so prefer it when Linux is an option.

MoltenVK: Metal's `newBufferWithBytesNoCopy` is what `VK_EXT_external_memory_host` maps to;
check that the MoltenVK build exposes the extension and that the import alignment is the 16 KiB
page of Apple silicon (the game's memory is placed in 64 KiB units, so either works).

## Checking it

The log at start says which way was taken (`Guest memory: ...`), and every 10 s with frame
stats on: `Layer memory: 0 MiB valid in VRAM, 0 mirrors` and `0 write faults/s` in the frame
stats. Mirrors or write faults there mean the device was not taken for unified memory:
set `BB_UNIFIED_MEMORY=1`.
