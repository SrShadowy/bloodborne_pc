> [!IMPORTANT]
> **THIS PROJECT IS NOT RELATED TO SHADPS4.**
> All questions regarding the original porting project should be directed to the original creator's Discord server: **https://discord.gg/KYZRKk9CB**, NOT to the shadPS4 server.
>
> **Original Upstream Repository:** [https://github.com/deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc) (created by [deadinside28](https://github.com/deadinside28))

# bbport (Custom Fork) — Native Linux Port of Bloodborne

> [!NOTE]
> **About this Fork:**
> This repository is a **simple personal fork** of the original [bbport](https://github.com/deadinside28/bloodborne_pc) by **deadinside28**. It introduces a collection of quality-of-life enhancements, refined user interfaces, expanded gamepad integration, and visual indicators tailored to personal taste, while preserving the high-performance native execution architecture of the upstream project.

---

## ✨ Fork Enhancements & Highlights

Here are some of the key additions and custom improvements featured in this fork:

### 1. In-Game Virtual Keyboard with Full Gamepad Navigation
Creating a character name no longer requires a physical keyboard! An on-screen virtual keyboard is integrated directly into the character creation screen with complete controller navigation, button glyphs, and responsive physical keyboard support.

![In-Game Virtual Keyboard](prints/controller_input.png)

- **Gamepad Controls:**
  - **D-Pad / Left Stick**: Move cursor across letters and buttons
  - **(A / ✕)**: Insert character
  - **(X / ▢)**: Backspace / Delete
  - **(Y / △)**: Space
  - **(LB / RB / L1 / R1)**: Toggle Caps Lock
  - **(Start)**: Confirm / OK
  - **(B / ◯)**: Cancel / Close
- **Keyboard Support**: Physical typing with Backspace, Space, Enter, and Esc.

---

### 2. AMD FSR 3.1 Frame Generation (FG)
Experience Bloodborne with dramatically elevated smoothness via integrated **FSR 3.1 Frame Generation**, switchable directly in real-time from the in-game overlay menu.

![FSR 3.1 Frame Generation](prints/frame_gen.png)

- Interpolates intermediate frames using native motion vectors, scene depth, and reactivity masks.
- Easily reach 60 to 120+ FPS on compatible hardware for fluid combat.
- **Native execution.** The eboot is converted offline into a flat memory image; PS4 libc and
  libSceFios2 are linked into it as native code. No CPU emulation and no per-instruction
  translation: the game code runs at full speed.
- **Two memory models** (launcher → *Mode → Memory and translation model*; *Auto*, the default,
  is the new one on every GPU):
  - **The new memory and translation model — the default on every GPU (0.6).** The game's memory lives
    in system RAM and the GPU reads it where it is, as a PC game's buffers; data it reads often
    is kept in VRAM and given back when unused (textures after 20 s, buffers after 60 s). The
    layer's memory module tracks CPU writes to mirrored blocks to keep those copies coherent;
    it does not copy the whole GPU-visible memory. The PS4 command processor's work
    is translated into Vulkan commands rather than emulated on the CPU: memory writes
    (`WRITE_DATA`, DMA) are done by the GPU in command-stream order, and fences are written by the
    GPU itself at the end of the pipeline, at the same point of the stream as the PS4's command
    processor writes them. On every GPU the game's memory goes through the layer's memory module:
    bound in place, VRAM copies as separate buffers, no sparse binding (NVIDIA's sparse binding
    froze the GPU for seconds; on AMD the module is faster than the former sparse arena too);
    `BB_LAYER_MEMORY=0` gives AMD's sparse arena. Tested thoroughly on an RX 7800 XT; on NVIDIA
    by testers. A driver that fails the startup checks falls back to VRAM copies by itself.
    Without the launcher: `BB_PC_MODEL=1/0` (unset: the new one, as *Auto*).
  - **The old memory model, as in 0.3** (*Old*). VRAM copies of the game's memory, writes
    tracked through page protection, with every fix made since 0.3.

  [Changes in 0.6-pre1](docs/CHANGES_0.6-pre1.md), [0.51](docs/CHANGES_0.51.md),
  [0.5](docs/CHANGES_0.5.md).

  Unused textures are freed in both modes, so VRAM no longer grows with every area visited.
- **Unlocked frame rate.** Hooks in the game's code make the simulation use the real frame time
  (the community patches in `patches/Bloodborne.xml` remain for comparison, `BB_FRAME_HOOKS=0`);
  ~90 FPS at 4K with FSR 4 Balanced on an RX 7800 XT, ~150 FPS at 1440p with FSR 4 Quality.
  Also 30/60/90 FPS modes.
- **Any aspect ratio, live.** 4:3, 16:10, 16:9, 21:9 and 32:9 with the HUD at the screen's edges,
  through hooks (the game's code is not patched).
- **Keyboard and mouse.** Mouse look turns the game's camera directly; a mouse pointer in the
  game's menus; a layout close to Dark Souls III's; button prompts show the bound key, or the
  buttons of the gamepad in use (Xbox and others), PlayStation icons only with a PlayStation pad.
- **Online play and seamless co-op.** Messages, bloodstains, bells and summons through a shadNet
  server; seamless co-op (BBSC). Seamless Hunt's servers by default (`play.seamlesshunt.com`,
  an account from seamlesshunt.com).
- **Temporal upscaling built for this game.** Bloodborne has no velocity buffer, so bbport
  computes motion vectors itself: camera motion from depth and the scene matrices, and object
  motion (characters, cloth, weapons) from the vertex positions of the previous frame. The
  scene is jittered sub-pixel (Halton) and rendered at a reduced resolution; the upscaler fills
  the output (720p for the Steam Deck, 1080p, 1440p or 2160p) and the UI is drawn natively at the output resolution.
  - **FSR 3.1** (FireBurn/FSR-Vulkan).
  - **FSR 4 (INT8, model v07)** on GPUs exposing the required Vulkan shader features —
    RDNA2/3 included (see Requirements).
  - **FSR 4.1.1**: AMD's 4.1.1 DLL is recorded once under vkd3d-proton and its passes are
    replayed natively on Vulkan; the output is **bit-exact** with the DLL. Two variants, as in
    the DLL: INT8 on any GPU with the required shader features, and FP8 matrices on RDNA4 (RX
    9000; picked automatically). The assets are built on your machine from one DLL of your own
    (4.1.x): the launcher's *FSR 4.1.1 from your own AMD DLL → Choose DLL…* button (2–5 minutes,
    twice that on RDNA4; needs a recent Proton), or `tools/fsr4cap`.
  - Faster than AMD's own shaders on RDNA3: the final passes of FSR 4 and 4.1.1 were rewritten
    to store through workgroup memory (3.5× and 2.3× faster, bit-exact); FSR 4 costs ~4 ms at
    4K on an RX 7800 XT instead of ~6 ms.
- **Multi-threaded GPU command processing.** The PS4 command stream is decoded on one thread
  and draws are bound and recorded on another (two-stage pipeline), with a Vulkan recording
  thread and helper threads for memory copies. Early on the single GPU thread capped the game
  at ~26 FPS; now it runs at 90–150 FPS depending on resolution and scene.
- **Settings inside the game's own menu:** *System → Display / Effects* (after *Screen/Sound*),
  built and drawn by the game itself — output resolution, upscaler, preset, sharpness, FPS
  counter, model detail and the game's effects, in the game's language.
- **Overlay menu** (Insert or L3+R3), styled like the game's dialogs: everything above plus the
  advanced settings, with gamepad, keyboard and mouse.
- **GTK4 launcher** and an **AppImage** for the Steam Deck.
---

### 3. Redesigned In-Game Overlay Menu (Insert / L3+R3)
The in-game configuration overlay has been overhauled for better organization, clarity, and instant control over rendering technologies.

![Redesigned Overlay Menu](prints/redesing_menu.png)
| | shadPS4 | bbport |
|---|---|---|
| Scope | General PS4 emulator, many games | One game: Bloodborne v1.09 |
| Loading | Its own ELF loader and kernel emulation at run time | The eboot is converted offline (`scripts/`) into an image with PS4 libc/Fios2 linked in; a C loader maps it and jumps into the game (loader and runtime: ~5k lines) |
| Memory | The GPU's view of PS4 memory is kept in VRAM copies, synchronized through page-protection write tracking | By default the new memory and translation model: the game's memory in system RAM, used by the GPU in place, frequently read data in VRAM, freed when unused (the 0.3 model as an option) |
| Command processor | Emulated: memory writes, DMA and fences are done by the CPU while decoding | Translated into Vulkan commands that the GPU runs in stream order, fences written after the work has really finished (the old model emulates, as shadPS4) |
| System libraries | Broad HLE of the PS4 OS | A small runtime (`src/runtime_*.c`) that implements exactly what Bloodborne calls: memory, threads, sync, files, audio (incl. ATRAC9), pad, saves, AppContent |
| GPU | shadPS4 video core and shader recompiler | Its own command processor, renderer, caches and shader translator (they started from shadPS4's, `gpu/VENDOR.txt`): two-stage draw pipeline, render-state and texture-set memoization, render-scale proxies, motion vectors, FSR 3.1/4/4.1.1, DLSS, frame capture and GPU profiler |
| GPU thread | One thread processes the whole command stream (the bottleneck in Bloodborne) | Decode and draw recording run on separate threads; the work scales with the hardware threads (Steam Deck included) |
| Upscaling | — | Temporal (FSR 3.1, FSR 4, FSR 4.1.1) with the game's own motion vectors and jitter |
| Game patches | Patch files applied by the emulator | The same community patches, compiled at start (`scripts/patches.py`); render resolution, effects and FPS from the launcher |

Without shadPS4 there would be no bbport: its renderer and shader recompiler were the starting
point of the graphics side.
- **Multilingual UI**: Seamless switching between **English**, **Português (Brasil)**, and **Русский**.
- **Real-Time Upscaler Tweaks**: Instant toggles for FSR 4, FSR 4.1.1, FSR 3.1, and TAA.
- **Reactivity Masks & Motion Vectors**: Fine-tune reactivity scale, threshold, max values, and character motion vectors with live visual debug modes.
- **Live Output Resolution & Visual Effects**: Adjust display resolution, RCAS sharpness slider, subpixel jitter, Chromatic Aberration, and Depth of Field (DoF).

---
- **CPU.** The PS4 CPU is x86-64, so the game's code runs directly on the PC's CPU, with no
  emulation and no instruction translation.
- **System libraries.** As Wine replaces the Windows API, the bbport runtime (`src/runtime_*.c`)
  implements exactly the PS4 OS functions Bloodborne calls: memory, threads, files, audio, pad,
  saves.
- **Memory.** As in a PC game (the default model): the game's data in system RAM, VRAM used the
  way a PC game uses it.
- **Graphics.** The PS4 GPU's command stream (PM4) is a recording of the game's graphics API
  calls: it is written by 99 functions of the statically linked libGnm, so decoding the stream
  and translating the calls themselves come to the same thing. GCN shaders are translated to
  SPIR-V. In the new mode the command processor's work (memory writes, DMA, fences) is
  translated into Vulkan commands as well; fences are written by the GPU itself. Resource
  descriptors (textures, buffers) are read by the translator on the CPU, as DXVK reads resource
  bindings: a PS4 texture has to be converted from the PS4 tiling into a Vulkan image before the
  draw runs, so the CPU must know its descriptor beforehand — translation, not emulation. The
  descriptor tables (the constant engine's dumps) are read only by the translator, so the CPU
  performs them. The game's occlusion queries (~60 a frame) are
  Vulkan occlusion queries; predication, `COPY_DATA` and `COND_EXEC` are translated too (check:
  `BB_PM4_SELFTEST=1`). Details:
  [docs/EMULATION_REMOVAL_PLAN.ru.md](docs/EMULATION_REMOVAL_PLAN.ru.md), "Шаг 4" (Russian).
### 4. Background Shader Compilation HUD Indicator
No more wondering why a micro-stutter occurred! A sleek, non-intrusive status spinner and progress bar appear on the HUD whenever background asynchronous shaders are compiling.

![Shader Compilation Indicator](prints/shader_compiler.png)

- Real-time framerate, frametime, upscaler state, and compilation status: `59 \ 59 FPS 16.9 ms FSR 4 + FG | ◷ Shaders`.

---

### 5. Organized GTK4 Launcher & Native Gamepad Navigation
The launcher has been reorganized into clean, dedicated preference groups with full Brazilian Portuguese (`pt-BR`) localization and complete gamepad navigation.

![Organized Launcher in Portuguese](prints/organiz_launcher_pt-br.png)

- **Launcher Gamepad Navigation**: Navigate tabs, rows, toggles, sliders, and launch the game directly using your controller (Xbox, DualShock 4, DualSense, Switch Pro, Steam Deck).
- **Dynamic Hints Bar**: Displays connected controller model and available button shortcuts (`[LB/RB] Tabs`, `[D-Pad] Navigate`, `[A] Select`, `[B] Back`, `[Y] Focus Play`, `[Start] Launch`, `[RS] Scroll`).
- **Bloodborne Red Outline**: Prominent focus outline for couch gaming or handheld display modes.

---

### 6. In-Game Memory Scanner & Reverse Engineering Debugger Suite
Explore, reverse-engineer, and modify Bloodborne's runtime state live without external tools! Directly accessible from the in-game overlay menu (`Insert` / `L3+R3` → *Memory & Reverse Engineering*), this floating window provides a Cheat Engine-grade memory and debugging suite running natively on Linux.

![In-Game Memory Operations & Debugger](prints/MemoryOperations.png)

- **Asynchronous Memory Scanner (`[Scanner]`)**:
  - Non-blocking multithreaded scanning across 10 data types (`u8` to `u64`, `Float`, `Double`).
  - Scans across `Executable & .data`, `Guest Memory (PS4 Direct Heap: 0x1000000000)`, or `Full Process`.
  - Filter by `Exact Value`, `Changed`, `Unchanged`, `Increased`, and `Decreased`.
  - Instant 1-click actions: Add to Watchlist (`[+ Watch]`), inspect in Hex view (`[Hex]`), or track what writes (`[Who Writes?]`).
- **Interactive Watchlist & Multi-Level Pointer Chains (`[Watchlist]`)**:
  - Support for multi-level pointer chains (`Base + Offsets`, e.g. `[[0x80553B130] + 0x08] + 0x94`) with real-time resolution, live preview, and crash-safe dereferencing.
  - Pin addresses with real-time value updates and live 60Hz background memory freezing.
  - Native **`.bbt` (Bloodborne Table)** table persistence: saves and restores structured JSON cheat tables directly in `user/cheats/` (`Save .bbt` / `Load .bbt`) with auto-persistence.
  - Interactive data type switching, click-to-copy address, and quick deletion.
- **Hex Inspector & Data Card (`[Hex Inspector]`)**:
  - 16-byte aligned live memory grid with ASCII view, navigation history (`<` / `>`), address offsets, and jump presets (`eboot Base`, `Heap`, `Params`).
  - Multi-type decoder card simultaneously interpreting bytes as `u8`, `u16`, `u32`, `i32`, `Float`, and `Double` with direct memory write capabilities.
- **Hardware Watchpoints & Real-Time Debugger (`[Debugger]`)**:
  - Memory write protection via `mprotect(PROT_READ)` with lock-free `SIGSEGV`/`SIGTRAP` ring buffers.
  - Interactive x86-64 disassembler powered by Zydis with jump/call highlighting and active RIP tracking.
  - Dynamic instruction NOP patching (`[NOP]`) with instant, non-destructive restoration (`[Restore]`).
  - Full 64-bit CPU register snapshot viewer (`RAX`–`R15`, `RIP`, `RFLAGS`) with 1-click clipboard export.

---

## 🏛️ What is bbport?

![The Plain Doll Curious Head Tilt](prints/ue.GIF)

`bbport` is the equivalent of Wine + DXVK engineered specifically for a single title: **Bloodborne** for PlayStation 4 (CUSA03173, version 1.09) running natively on x86-64 Linux:

- **As in Wine**, the game's x86-64 code executes directly on the CPU without instruction emulation; a tailored runtime replaces the PS4 OS system libraries.
- **As in DXVK**, the game's graphics are translated to Vulkan by a renderer derived from [shadPS4](https://github.com/shadps4-emu/shadPS4) and heavily extended with custom motion vectors, temporal upscaling (AMD FSR 3.1, FSR 4, FSR 4.1.1, NVIDIA DLSS), and multi-threading.
- **Memory & GPU pipeline**: Implements a PC-style memory model on AMD GPUs (direct system RAM access, cached VRAM, translated Vulkan command streams) alongside the classic page-protection model for NVIDIA and other GPUs.

> **No game files or copyrighted assets are included.**
> You must supply your own decrypted dump of Bloodborne with update 1.09 (any retail edition: CUSA03173, CUSA00900, CUSA03023, etc.).
> This project is not affiliated with Sony Interactive Entertainment, FromSoftware, or AMD.

---

## 📊 Comparison with shadPS4

| Feature | shadPS4 | bbport |
| :--- | :--- | :--- |
| **Scope** | General PS4 emulator for multiple titles | Dedicated native runtime specifically for Bloodborne v1.09 |
| **Execution** | Dynamic ELF loader & kernel emulation | Offline conversion into a flat memory image; native PS4 libc/Fios2 linkage |
| **CPU Speed** | Emulation layer overhead | Full native x86-64 CPU speed with zero translation overhead |
| **GPU Command Stream** | Decoded on a single CPU thread | Decoded on one thread, draw calls recorded on another (two-stage pipeline) |
| **Memory Model** | VRAM mirror copies with page protection | Dual models: PC-style memory arena (AMD) & classic page protection (NVIDIA) |
| **Upscaling** | Basic or none | Temporal upscaling (FSR 3.1, FSR 4 INT8, FSR 4.1.1 bit-exact, DLSS, TAA) with custom motion vectors |
| **Frame Generation** | Not available | AMD FSR 3.1 Frame Generation integrated with motion vector interpolation |
| **Virtual Keyboard** | Standard dialog | Fully navigated in-game virtual keyboard for controllers |

---

## ⚙️ System Requirements

- **Operating System:** Linux x86-64 (Arch Linux, Ubuntu 24.04+, Fedora 40+, NixOS, Steam Deck, or compatible distributions).
- **Graphics Card:** GPU supporting **Vulkan 1.3** (tested thoroughly on AMD Radeon RX 6000/7000 / RDNA 2/3 with Mesa RADV; NVIDIA GeForce RTX/GTX supported via classic memory model).
- **Shader Features for FSR 4 / 4.1.1:** Requires Float16, Int8/Int16, integer dot products, and linear compute derivatives (`VK_VALVE_shader_mixed_float_dot_product` for FSR 4.1.1). Unsupported hardware seamlessly falls back to FSR 3.1 or TAA.
- **Game Dump:** Decrypted Bloodborne dump folder (`CUSA03173` or regional variants) updated to version **1.09**.

---

## 📦 Installing Prerequisites & Dependencies

Before building, install the required compiler tools, Vulkan headers, SDL3, and multimedia libraries for your Linux distribution:

### Arch Linux / Manjaro / EndeavourOS
```bash
sudo pacman -S --needed base-devel git cmake ninja pkgconf python \
    vulkan-headers vulkan-icd-loader sdl3 glslang boost fmt xxhash \
    zydis xbyak ffmpeg protobuf openssl zlib miniupnpc nlohmann-json \
    libadwaita python-gobject
```

### Ubuntu (24.04 LTS+) / Debian (Trixie/Sid) / Linux Mint
```bash
sudo apt update && sudo apt install -y build-essential git cmake ninja-build pkg-config \
    python3 python3-pip libvulkan-dev libsdl3-dev glslang-tools libboost-all-dev \
    libfmt-dev libxxhash-dev libavcodec-dev libavutil-dev libswresample-dev \
    libprotobuf-dev protobuf-compiler libssl-dev zlib1g-dev libminiupnpc-dev \
    nlohmann-json3-dev python3-gi python3-gi-cairo gir1.2-adw-1 gir1.2-gtk-4.0
```
> [!NOTE]
> If `libsdl3-dev` is not available in your Ubuntu/Debian repositories, you can install it via the official SDL GitHub releases or compile SDL3 from source.

### Fedora (40+) / Nobara / Bazzite
```bash
sudo dnf install -y gcc-c++ git cmake ninja-build pkgconf-pkg-config python3 \
    vulkan-headers vulkan-loader-devel SDL3-devel glslang boost-devel fmt-devel \
    xxhash-devel ffmpeg-free-devel protobuf-devel openssl-devel zlib-devel \
    miniupnpc-devel json-devel libadwaita-devel python3-gobject
```

### NixOS / Steam Deck (using Nix)
No manual dependency installation is needed! The project includes a fully defined [`shell.nix`](shell.nix). Simply enter the environment:
```bash
nix-shell --run "bash build.sh"
```

---

## 🚀 Building & Compiling

### 1. Clone this Repository (with submodules)
Clone this fork recursively to pull all third-party components (FSR-Vulkan, Dear ImGui, LibAtrac9, sirit, etc.):
```bash
git clone --recursive https://github.com/SrShadowy/bloodborne_pc.git bbport
cd bbport
```

> [!TIP]
> If you already cloned the repository without `--recursive`, initialize and update the submodules with:
> ```bash
> git submodule update --init --recursive
> ```

### 2. Compile the Binaries
Run the automated build script:
```bash
bash build.sh
```
This builds:
* `out/gpu/libbbgpu.so`: The Vulkan translation renderer, shader recompiler, FSR pipeline, and in-game overlay.
* `out/gpu/libbbnet.so`: The optional online module (shadNet client for spectres, bloodstains, player messages, and co-op networking).
* `out/libatrac9.a`: High-performance native ATRAC9 audio decoder.
* `out/bb-probe`: The native x86-64 Linux loader and PS4 HLE runtime.
* `out/bb-gpu-capabilities`: GPU capability probe for automatic resolution and controller detection.

### 3. Advanced Compilation Flags & Options
| Path | Contents |
|---|---|
| `src/` | Loader (`probe.c`) and the HLE runtime |
| `scripts/` | Offline preparation of the game image, module linking, patch compiler |
| `gpu/` | Renderer library, the port's own (`gpu/VENDOR.txt`): command processor, shader translator, renderer, caches, ImGui menu, FSR 4.1.1 runtime (`gpu/upscale/fsr411`); the online module (`gpu/bbnet`) |
| `launcher/`, `packaging/` | GTK4 launcher; Nix package and AppImage |
| `patches/` | Community patches for Bloodborne |
| `tools/` | Developer tools: scripted runs, A/B toggles, FSR benchmark helpers, FSR 4 shader rewrites, `fsr4cap` (FSR 4.1.1 recording/extraction) |
| `tests/` | Loader, runtime, patch and renderer tests |
| `docs/` | Design notes and measurements ([upscaler](docs/upscaler.md), [parallel GPU](docs/parallel_gpu.md), [motion vectors](docs/motion_vectors.md), [roadmap](docs/ROADMAP.md)) |
You can customize the compilation by prepending environment variables to `bash build.sh`:

| Environment Variable | Description | Example |
| :--- | :--- | :--- |
| `BB_MARCH=native` | Compiles with host CPU optimizations (AVX2, FMA, BMI2) for maximum framerate | `BB_MARCH=native bash build.sh` |
| `BB_LTO=OFF` | Disables Link-Time Optimization for significantly faster build times | `BB_LTO=OFF bash build.sh` |
| `BB_PGO=generate` / `use` | Profile-Guided Optimization (generate profiling data during gameplay, then build optimized) | `BB_PGO=use bash build.sh` |
| `DLSS_SDK_ROOT=<path>` | Enables NVIDIA DLSS bridge using the NVIDIA DLSS SDK | `DLSS_SDK_ROOT=~/DLSS bash build.sh` |
| `--test` | Builds and runs the automated unit test suite (115+ tests) | `bash build.sh --test` |

---
- The old memory model removed, once the new one (the default on every GPU since 0.6) is
  confirmed on NVIDIA and Intel.
- Textures as objects created at load time (less guessing by address, uploads outside the
  frame).
- Shaders translated ahead of time, at install, not during play.
- More CPU parallelism in GPU command processing (split the draw-recording stage further),
  scaling to all hardware threads — most important for the Steam Deck.
- Async compute for the upscaler (the frame is GPU-bound at 4K).
- XeSS (super resolution) and XeFG frame generation through a Wine helper sharing Vulkan
  memory (a memory-bridge prototype is in `tools/bridge_helper`); inputs exposed so that OptiScaler-style mapping works.
- Frame generation (FSR 3.1 FG: in the code, hidden until its artifacts with upscaling are
  fixed), reactive and transparency masks for particles and fog.
- Fix the races in AMD's FSR 4.1.1 shaders at output widths that are not multiples of 64
  (e.g. 1600×900), as already done for the left-edge race in FSR 4 v07 at 1080p.
- Steam Deck validation of the AppImage; HDR output.
## 🎮 Launching the Game

### Method A: GTK4 Launcher with Full Gamepad Support (Recommended)
Launch the modern graphical launcher with Brazilian Portuguese support and complete controller navigation:
```bash
bash launcher/bb-launcher.sh
```
In the launcher, select your decrypted `CUSA03173` directory, customize graphics, configure upscaling (FSR 4 / FSR 3.1 / TAA), and press **Play** (or `Start` / `(Y)` on your gamepad).

### Method B: Direct Terminal Launch
To launch directly from the terminal without opening the launcher GUI:
```bash
BB_GAME_DIR="/caminho/para/seu/CUSA03173" bash run.sh
```

#### Useful Environment Variables for `run.sh`:
* `BB_GAME_DIR`: Path to the game folder containing `eboot.bin`.
* `BB_SAVE_LOG=1`: Saves detailed run logs, frametime graphs, and readback statistics in `logs/`.
* `BB_DATA_DIR`: Custom directory for storing saves, patches, and configurations (defaults to `.`).

---

## 🎮 Controls & Shortcuts

| Context | Input | Action |
| :--- | :--- | :--- |
| **In-Game Overlay** | `Insert` or `L3 + R3` | Open / Close the in-game settings overlay |
| **Memory Tools Suite**| Overlay (`Insert`) → *Memory Tools* | Open floating memory scanner, watchlist, hex editor & debugger |
| **Character Name** | In-game dialog | Interactive virtual keyboard (controller & physical keyboard) |
| **Launcher Navigation** | `D-Pad` / `Left Stick` | Move focus between launcher settings and categories |
| **Launcher Values** | `D-Pad Left/Right` | Adjust sliders, cycle combo options, and toggle switches |
| **Launcher Pages** | `LB` / `RB` (L1 / R1) | Switch launcher tabs |
| **Launcher Quick Play**| `Start` or `Y` | Immediate game launch / focus main action button |
| **Page Scrolling** | `Right Stick (RS)` | Smooth vertical scroll in launcher and menus |
| **Touchpad Gestures** | Left Touchpad / `Tab` | Open gestures menu (or retail Debug Menu if patch enabled) |
| **Touchpad Items** | Right Touchpad / `Backspace` | Open personal items menu |
| **Free Camera** | Hold `✕` + press `L3` | Cycle camera modes (requires free camera patch; keyboard: `Space` + `Z`) |
| **FromSoftware Debug Menu** | Left Touchpad / `Tab` | Requires patch enabled + font files (`DbgFont14h.ccm`/`.tpf`) from [Nexus #253](https://www.nexusmods.com/bloodborne/mods/253) in `dvdroot_ps4/font/` |

![Hunter Reverence Bow Gesture](prints/reverencia.GIF)
*(Performing gestures with the Plain Doll in the Hunter's Dream — open the gesture menu with `Tab` or Touchpad)*

---

## 📁 Repository Layout

| Path | Purpose |
| :--- | :--- |
| `src/` | Native loader (`probe.c`) and high-level emulation (HLE) runtime (`runtime_*.c`) |
| `gpu/` | Vulkan translation renderer (vendored shadPS4 video core + custom Bloodborne pipeline, FSR 4.1.1 replayer, Frame Generation, and ImGui overlay) |
| `launcher/` | Modern GTK4 / Libadwaita launcher with full controller navigation, i18n, and settings persistence |
| `prints/` | Screenshots showcasing custom fork enhancements, UI redesigns, and controls |
| `scripts/` | Image linker, eboot unpacker, libc/fios2 linkers, and XML patch compiler |
| `patches/` | Community performance and graphics patches (`Bloodborne.xml`) |
| `tools/` | Developer utilities: `fsr4cap`, automated benchmarks, asset downloaders, and test scripts |
| `tests/` | Comprehensive test suite (115+ automated unit tests for runtime, patches, and gamepad navigation) |
| `docs/` | Technical design documentation, measurements, and architectural roadmap |

---

## 🏆 Credits, Attribution & Licenses

![Plain Doll Clapping and Applause](prints/aplausos.GIF)

`bbport` is distributed under the **GNU General Public License v2.0 or later** ([LICENSE](LICENSE)). Third-party components, libraries, and patches retain their respective original licenses.

### Upstream Project & Port Architecture
- **Original Project Creator:** [deadinside28](https://github.com/deadinside28) ([deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc)) — Architect of bbport: native x86-64 execution, Wine-style PS4 HLE runtime (`src/runtime_*.c`), memory arena model, offline eboot/ELF conversion, parallel GPU decoding/recording pipeline, and custom motion vector generation.
- **Official Creator Discord:** [https://discord.gg/KYZRKk9CB](https://discord.gg/KYZRKk9CB) — Primary community server for upstream development and bug reports.

### Emulation & Graphics Translation
- **[shadPS4](https://github.com/shadps4-emu/shadPS4)** (GPL-2.0+): The core video renderer (`video_core`) and shader recompiler (`shader_recompiler`) that form the foundation of bbport's graphics translation engine.
- **[sirit](https://github.com/shadps4-emu/sirit)** (shadPS4 project): Lightweight SPIR-V runtime assembly generation library.
- **[shadNet / shadp2p](https://srv.shadps4.net)**: Online networking client and server infrastructure for Bloodborne messages, bloodstains, spectres, and cooperative matchmaking.
- **Khronos Group & Vulkan**: The Vulkan 1.3 low-level graphics API, validation layers, and SPIR-V tooling.
- **Wine & DXVK Projects**: The foundational concept and architectural inspiration for native Linux single-game translation runtimes.

### Upscaling, Reconstruction & Frame Generation
- **[AMD FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK)** (MIT): FidelityFX Super Resolution (FSR 3.1) algorithms, Robust Contrast Adaptive Sharpening (RCAS), subpixel jittering, and Frame Generation interpolation pipeline.
- **[FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan)** by FireBurn (MIT): Native Vulkan implementations of AMD FSR 3.1 and the FSR 4 v07 provider.
- **FSR 4.1.1 Replay Pipeline**:
  - **[dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv)** by Hans-Kristian Arntzen (MIT): Translates DirectX 12 DXIL passes into native Vulkan SPIR-V compute shaders for bit-exact execution in `fsr4cap`.
  - Research and model workflows inspired by the **OptiScaler** and **Goverlay** open-source communities.
- **[NVIDIA DLSS SDK](https://github.com/NVIDIA/DLSS)**: Deep Learning Super Sampling technology and NGX Linux runtime library.
- **[Supermedo's Windows Port](https://github.com/Supermedo/bloodborne_pc)** & **[IFreemz](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR)**: The original DLSS bridge concept, loader integration, and motion vector hooks.

### Input, Gamepad & Virtual Keyboard
- **[SDL3 (Simple DirectMedia Layer 3)](https://www.libsdl.org/)** (zlib): Low-latency controller input, unified gamepad mappings (Xbox 360 / One / Series, PlayStation DualShock 4 / DualSense, Nintendo Switch Pro, Steam Deck), hotplugging detection, and analog stick polling across both the game runtime and the GTK4 launcher.
- **Custom In-Game Virtual Keyboard**: Built specifically for the character creation contract screen with full controller navigation, button glyphs, and physical keyboard support.
- **GTK4 Gamepad Navigator**: Custom SDL3-powered navigation engine for the GTK4 / Libadwaita launcher, supporting D-Pad navigation, smooth analog scrolling, popover choice dropdowns, and modal dialogs.

### Audio, Typography & User Interface
- **[LibAtrac9](https://github.com/Thealexbarney/LibAtrac9)** by Thealexbarney (MIT): High-performance native ATRAC9 audio decoding for PlayStation 4 audio streams, sound effects, and orchestral music.
- **[Dear ImGui](https://github.com/ocornut/imgui)** by Omar Cornut (MIT): Lightweight in-game configuration overlay and real-time performance metrics HUD.
- **[GNOME / Libadwaita & GTK4](https://gitlab.gnome.org/GNOME/libadwaita)** (LGPL-2.1+): Modern, accessible desktop launcher interface with custom CSS theming, organized preference groups, and i18n support.
- **[DejaVu Fonts](https://dejavu-fonts.github.io/)**: Clear, high-legibility Unicode fonts used in the game overlay and debugging menus.

### Reverse Engineering & Memory Tools
- **[Zydis](https://github.com/zyantific/zydis)** (MIT): Fast and lightweight x86-64 disassembler library powering the in-game live instruction view and real-time disassembler.
- **Custom Asynchronous Memory Engine**: Non-blocking multithreaded memory scanner, lock-free signal-based page watchpoints (`mprotect` + `SIGSEGV`/`SIGTRAP` ring buffer), interactive live watchlist, and hex data inspector engineered directly into the Vulkan overlay.

### Game Patches, Modding & Community Research
- **Lance McDonald**: Pioneer of the Bloodborne 60 FPS patch, physics delta time decoupling research, frame pacing analysis, GoldHEN free camera patch, and game internals exploration.
- **Kyo**: Motion vector extraction hooks, simulation delta scaling, and graphics patches.
- **auser1337, illusion, emoose, and the Bloodborne modding community**: Resolution patches, game effects toggles (DoF, Chromatic Aberration), XML patch compilation, and modding tools (`patches/Bloodborne.xml`).
- **FromSoftware & Sony Interactive Entertainment**: The original creators and publishers of Bloodborne (CUSA03173 / CUSA00900 / CUSA03023).

### Packaging, Monitoring & System Tools
- **[MangoHud](https://github.com/flightlessmango/MangoHud)**: Performance monitoring, FPS, and frametime tracking overlay.
- **Nix / NixOS & AppImageKit**: Reproducible development environment (`shell.nix`) and standalone portable package creation for Linux desktops and the Steam Deck.
bbport is licensed under the **GNU GPL v2 or later** ([LICENSE](LICENSE)) — it contains code
from shadPS4 (GPL-2.0-or-later). Third-party components keep their licenses:
[shadPS4](https://github.com/shadps4-emu/shadPS4) (GPL-2.0+: the renderer and shader translator were derived from it and rewritten; the online module keeps its network and NP code),
[sirit](https://github.com/shadps4-emu/sirit), [half](https://half.sourceforge.net/),
[FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) by FireBurn (MIT; FSR 3.1 on Vulkan and the
FSR 4 v07 provider), AMD FidelityFX SDK (MIT), [LibAtrac9](https://github.com/Thealexbarney/LibAtrac9)
(MIT), [Dear ImGui](https://github.com/ocornut/imgui) (MIT), DejaVu fonts, BBSC seamless co-op
(GPL-2.0+, in the online module),
[dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv) (MIT, used to build the
FSR 4.1.1 assets). Game patches by Kyo, Lance McDonald, auser1337, illusion, emoose and other
community members (`patches/Bloodborne.xml`). The DLSS bridge (`gpu/dlss_bridge`, MIT) and
loader come from [Supermedo's Windows port](https://github.com/Supermedo/bloodborne_pc), adapted
from [IFreemz/shadPS4-Bloodborne-DLSS-FSR](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR).
AMD's FSR 4 DLLs and model data and NVIDIA's DLSS SDK and libraries are not distributed here.