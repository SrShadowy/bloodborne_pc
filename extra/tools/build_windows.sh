#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# [extra] Unified Windows native build script for bbport + GTK4 Launcher.
# Run inside MSYS2 CLANG64:
#   C:\msys64\usr\bin\bash.exe -l extra/tools/build_windows.sh
#   --DEV: fast dev build (LTO OFF, all cores, ccache if present); skips launcher/packaging/zip.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
export MSYSTEM=CLANG64
export PATH=/clang64/bin:/usr/bin:$PATH

echo "=== BBPort Windows Native Build ==="
echo "Root: $ROOT"
cd "$ROOT"

# [extra] --DEV: fast iteration build. Separate dir so it never clobbers the release build.
DEV_BUILD=0
for arg in "$@"; do
    case "$arg" in
        --DEV|--dev) DEV_BUILD=1 ;;
    esac
done
NINJA_JOBS="${NINJA_JOBS:-$(nproc 2>/dev/null || echo 2)}"

if [[ $DEV_BUILD -eq 1 ]]; then
    BUILD_DIR="$ROOT/out/win-dev"
    LTO=OFF
    BUILD_TYPE=Debug
    echo "--> MODE: DEV (LTO off, $BUILD_TYPE, -j$NINJA_JOBS, no launcher/zip)"
else
    BUILD_DIR="$ROOT/out/win-native"
    LTO=ON
    BUILD_TYPE=RelWithDebInfo
    echo "--> MODE: RELEASE (LTO on, $BUILD_TYPE, -j$NINJA_JOBS)"
fi

if command -v ccache >/dev/null 2>&1; then
    export CMAKE_C_COMPILER_LAUNCHER=ccache CMAKE_CXX_COMPILER_LAUNCHER=ccache
    echo "--> ccache: enabled"
fi

# 1. CMake Configure
# Invalidate cache if build was configured from a different path (e.g. drive B: vs C:)
WIN_SRC_DIR="$(cygpath -m "$ROOT/gpu" 2>/dev/null || echo "$ROOT/gpu")"
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] && ! grep -qi "CMAKE_HOME_DIRECTORY:INTERNAL=$WIN_SRC_DIR" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
    echo "--> Stale CMake cache detected ($BUILD_DIR was configured for a different path). Cleaning..."
    rm -rf "$BUILD_DIR"
fi

if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
    echo "--> Configuring CMake ($BUILD_TYPE, Ninja, BB_NET=ON, BB_LTO=$LTO)..."
    cmake -S gpu -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
        -DBB_NET=ON \
        -DBB_LTO=$LTO \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi

# 2. Build game executable and online module (bb-probe.exe, bbnet20.dll)
echo "--> Building bb-probe.exe and bbnet20.dll..."
ninja -j "$NINJA_JOBS" -C "$BUILD_DIR" bb-probe bbnet20

# 3. Build GPU capabilities tool
echo "--> Building bb-gpu-capabilities.exe..."
clang -std=c11 -O2 -Wall -Wextra "$ROOT/tools/gpu_capabilities.c" \
    -lvulkan-1 -lSDL3 -o "$BUILD_DIR/bb-gpu-capabilities.exe"

if [[ $DEV_BUILD -eq 1 ]]; then
    # [extra] Copy binaries into a minimal dist dir so the game runs without packaging.
    # Symlinks/junctions need admin on Windows; a plain copy of 3 files is instant.
    DIST_BIN="$ROOT/dist/BBPort-dev/bin"
    mkdir -p "$DIST_BIN"
    cp -f "$BUILD_DIR/bb-probe.exe" "$DIST_BIN/" 2>/dev/null || true
    cp -f "$BUILD_DIR/bbnet20.dll" "$DIST_BIN/" 2>/dev/null || true
    cp -f "$BUILD_DIR/bb-gpu-capabilities.exe" "$DIST_BIN/" 2>/dev/null || true
    # Collect MSYS2 runtime DLLs so bb-probe.exe runs outside the MSYS2 shell.
    python "$HERE/collect_dlls.py" "$DIST_BIN" "/clang64/bin" >/dev/null 2>&1 \
        && echo "--> Runtime DLLs collected into $DIST_BIN" || true
    echo "=== DEV build done ==="
    echo "Artifacts: $BUILD_DIR/bb-probe.exe"
    echo "Copied:    bb-probe.exe, bbnet20.dll, bb-gpu-capabilities.exe -> $DIST_BIN"
    echo "Skipped:   launcher (PyInstaller), packaging, zip."
    exit 0
fi

# 4. Build Launcher (Bloodborne.exe via PyInstaller with GTK4/Adwaita)
echo "--> Building GTK4 Launcher (Bloodborne.exe)..."
LAUNCHER_OUT="$ROOT/out/launcher-win"
mkdir -p "$LAUNCHER_OUT"

WIN_ROOT="$(cygpath -m "$ROOT")"
WIN_HOOKS="$(cygpath -m "$HERE/pyi_hooks")"
WIN_GI_TYPELIBS="$(cygpath -m /clang64/lib/girepository-1.0)"
WIN_GLIB_SCHEMAS="$(cygpath -m /clang64/share/glib-2.0/schemas)"
WIN_ICONS="$(cygpath -m /clang64/share/icons)"

pyinstaller --noconfirm --windowed --name Bloodborne \
    --distpath "$LAUNCHER_OUT/dist" \
    --workpath "$LAUNCHER_OUT/build" \
    --specpath "$LAUNCHER_OUT" \
    --additional-hooks-dir "$WIN_HOOKS" \
    --paths "$WIN_ROOT/launcher" \
    --paths "$WIN_ROOT/scripts" \
    --collect-all gi \
    --add-data "$WIN_ROOT/launcher/settings_schema.json:." \
    --add-data "$WIN_ROOT/launcher/icons:icons" \
    --add-data "$WIN_ROOT/launcher/fonts:fonts" \
    --add-data "$WIN_ROOT/launcher/locales:locales" \
    --add-data "$WIN_ROOT/scripts:scripts" \
    --add-data "$WIN_GI_TYPELIBS:gi_typelibs" \
    --add-data "$WIN_GLIB_SCHEMAS:share/glib-2.0/schemas" \
    --add-data "$WIN_ICONS:share/icons" \
    --hidden-import gi.repository.Gtk \
    --hidden-import gi.repository.Adw \
    --hidden-import gi.repository.Gio \
    --hidden-import gi.repository.GLib \
    --hidden-import gi.repository.GObject \
    --hidden-import gi.repository.Gdk \
    --hidden-import gi.repository.Gsk \
    --hidden-import gi.repository.Pango \
    --hidden-import gi.repository.cairo \
    --hidden-import mods \
    --hidden-import game_check \
    --hidden-import online \
    --hidden-import trophy_list \
    --hidden-import patches \
    --hidden-import run \
    -i "$WIN_ROOT/packaging/bbport.ico" \
    "$WIN_ROOT/launcher/bbport_launcher.py"

# 5. Assemble distribution package in dist/
VER=$(cat "$ROOT/VERSION" 2>/dev/null || echo "0.7.4-8")
DIST_NAME="BBPort-${VER}-win-x86_64"
STAGE="$ROOT/dist/${DIST_NAME}"
ZIP_FILE="$ROOT/dist/${DIST_NAME}.zip"

echo "--> Assembling distribution package at $STAGE..."
rm -rf "$STAGE" "$ZIP_FILE"
mkdir -p "$STAGE/bin" "$STAGE/online" "$STAGE/out" "$STAGE/user" "$STAGE/mods" "$STAGE/patches" "$STAGE/logs" "$STAGE/scripts" "$STAGE/tools"

cp "$LAUNCHER_OUT/dist/Bloodborne/Bloodborne.exe" "$STAGE/"
cp -r "$LAUNCHER_OUT/dist/Bloodborne/_internal" "$STAGE/"
cp "$BUILD_DIR/bb-probe.exe" "$STAGE/bin/"
cp "$BUILD_DIR/bb-gpu-capabilities.exe" "$STAGE/bin/"
[[ -f "$BUILD_DIR/bbnet20.dll" ]] && cp "$BUILD_DIR/bbnet20.dll" "$STAGE/online/" 2>/dev/null || true
[[ -f "$BUILD_DIR/bbnet20.dll" ]] && cp "$BUILD_DIR/bbnet20.dll" "$STAGE/bin/" 2>/dev/null || true

# Copy runtime DLLs recursively (never vulkan-1.dll in bin)
echo "--> Collecting all transitive DLL dependencies for bin/..."
python "$HERE/collect_dlls.py" "$STAGE/bin" "/clang64/bin"

# Fonts
mkdir -p "$STAGE/bin/fonts"
if [[ -d "$ROOT/gpu/third_party/fonts" ]]; then
    cp "$ROOT/gpu/third_party/fonts/"*.ttf "$STAGE/bin/fonts/" 2>/dev/null || true
fi

# Scripts, patches, tools, shaders
cp "$ROOT/scripts/"*.py "$STAGE/scripts/" 2>/dev/null || true
if [[ -d "$ROOT/scripts/bbsc_gameserver" ]]; then
    mkdir -p "$STAGE/scripts/bbsc_gameserver"
    cp "$ROOT/scripts/bbsc_gameserver/"*.py "$ROOT/scripts/bbsc_gameserver/"*.xml "$ROOT/scripts/bbsc_gameserver/"*.txt \
       "$ROOT/scripts/bbsc_gameserver/LICENSE"* "$ROOT/scripts/bbsc_gameserver/README.md" "$STAGE/scripts/bbsc_gameserver/" 2>/dev/null || true
fi
[[ -d "$ROOT/patches" ]] && cp -r "$ROOT/patches/"* "$STAGE/patches/" 2>/dev/null || true
[[ -d "$ROOT/tools" ]] && cp -r "$ROOT/tools/"* "$STAGE/tools/" 2>/dev/null || true
[[ -d "$ROOT/fsr4_shaders" ]] && cp -r "$ROOT/fsr4_shaders" "$STAGE/"
[[ -f "$ROOT/catalog.json" ]] && cp "$ROOT/catalog.json" "$STAGE/"
[[ -f "$ROOT/shader_recipes.bin" ]] && cp "$ROOT/shader_recipes.bin" "$STAGE/"
[[ -f "$ROOT/image_shapes.txt" ]] && cp "$ROOT/image_shapes.txt" "$STAGE/"
[[ -f "$ROOT/bbport.ini" ]] && cp "$ROOT/bbport.ini" "$STAGE/"
cp "$ROOT/run.py" "$STAGE/"
cp "$ROOT/packaging/windows/README-Windows.txt" "$STAGE/"
[[ -f "$ROOT/LICENSE" ]] && cp "$ROOT/LICENSE" "$STAGE/"
[[ -f "$ROOT/NOTICE" ]] && cp "$ROOT/NOTICE" "$STAGE/"
[[ -f "$ROOT/VERSION" ]] && cp "$ROOT/VERSION" "$STAGE/"
cp "$ROOT/packaging/bbport.ico" "$STAGE/"

cat << 'EOF' > "$STAGE/start.cmd"
@echo off
setlocal
cd /d "%~dp0"
if "%GSK_RENDERER%"=="" if not "%WINEUSERNAME%"=="" set GSK_RENDERER=cairo
start "" "%~dp0Bloodborne.exe" %*
EOF

# 6. Compress to ZIP
echo "--> Compressing to $ZIP_FILE..."
cd "$ROOT/dist"
zip -r -q -9 "${ZIP_FILE}" "${DIST_NAME}"

echo "=== Build finished successfully! ==="
echo "Package: $STAGE"
echo "Zip:     $ZIP_FILE ($(du -sh "$ZIP_FILE" | cut -f1))"
