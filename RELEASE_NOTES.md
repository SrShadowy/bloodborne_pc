# Bloodborne PC Port (BBPort) — Release Notes

An enhanced, highly stabilized build of the native Linux port of Bloodborne, featuring critical visual fixes, universal distro compatibility, and new embedded reverse-engineering tools.

---

### 🛡️ Stability & Critical Engine Fixes
- **Cutscene Deadlock Resolved**: Fixed thread concurrency and indexing deadlocks in `AvPlayer`, completely eliminating black screen freezes at the end of the opening cutscene and in-game transitions.
- **Player Death Screen Crash Fixed (Code 23)**: Resolved the fatal buffer synchronization assertion (`Out of order flip IRQ`) during "YOU DIED" screens and quick reloads.
- **Hunter's Dream Teleport Crash Fixed**: Resolved a segmentation fault (`TextureCache::TouchImage`) during lamp travel and map unloading.
- **GPU Hang Protection (RADV Error 23 / `VK_ERROR_DEVICE_LOST`)**: Added a GPU-timeline compute sanitizer (`sanitize_indirect.comp`) preventing driver timeouts caused by underflowed particle dispatch counters.

---

### 🎨 Visual Fidelity & Graphics
- **Upstream 0.5 Merge (30 FPS Shaders)**: Restored proper, authentic volumetric fog, lighting, and bloom rendering both at native 30 FPS and unlocked framerates.
- **Fixed Inverted Skybox Reflections**: Filtered draw calls using PS4 User Clip Planes, eliminating upside-down buildings and geometry artifacts leaking into the sky when planar reflections / SSR are active.
- **Eliminated Sky Scanlines & Banding**: Selectively disabled the problematic Motion Blur velomap pass in the sky while keeping all other post-processing effects (DoF, SSAO, SSR) fully active.
- **Main Menu Character Texture Fix**: The Hunter's model and dynamic facial blend shapes now render textured and shaded properly on the character selection screen.
- **FSR 4.1.1 (Native FP8) & FSR 3.1 Frame Generation**: Added dynamic FP8 acceleration detection on AMD RDNA 4 GPUs (RX 9070 XT), INT8 fallback, and fully synchronized dual-present frame generation.

---

### 🎮 Controls & Usability
- **In-Game Virtual Keyboard**: Create and name your character entirely using a **gamepad** (D-pad, analog stick, and quick shortcuts: `X` = Backspace, `Y` = Space, `Start` = Confirm) or mouse, with no physical keyboard required.
- **Smooth 360° Analog Mouse Look**: Mouse camera control with internal deadzone compensation, flick smoothing, and real-time sensitivity adjustment.
- **Full Trilingual Localization**: Complete native translations for **English**, **Portuguese (Brazil)**, and **Russian** across the in-game overlay and launcher.
- **Modernized GTK4 / Libadwaita Launcher**: Intuitive controls viewer, display settings, and automated shader management.

---

### 🔬 Embedded Reverse-Engineering Suite (<kbd>Insert</kbd> or <kbd>L3 + R3</kbd>)
- **Native Memory Scanner**: Cheat Engine-grade memory search for values, items, and player stats running directly within the Vulkan overlay.
- **Interactive Watchlist with Live Freezing**: Real-time 60 Hz memory freezing.
- **x86-64 Live Disassembler & 1-Click NOP Patching**: Inspect assembly code at runtime, track "Who Writes" to variables, and apply/undo NOP byte patches without restarting.
- **Hex Inspector**: 16-byte aligned memory inspector with multi-type data decoding.

---

### 📦 Universal Compatibility (Linux)
- **Built with glibc 2.34**: Runs out-of-the-box on Ubuntu 22.04/24.04, Debian 12, Fedora, Arch Linux, Linux Mint, and SteamOS (Steam Deck).
- **Self-Contained FFmpeg 6.1**: Bundles a minimal, dependency-free FFmpeg build for cutscenes without relying on host system packages (~18 MB total archive size).

---

### 🚀 How to Run
1. Extract the `.tar.gz` archive.
2. Place your game files (`eboot.bin`) into the folder.
3. Launch:
   - **With GUI Launcher**: `./start.sh`
   - **Directly to game**: `./start.sh --play` or `./run.sh`
