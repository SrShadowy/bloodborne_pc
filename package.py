#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""
package.py - Automated build & packaging script for BBPort releases.

Compiles the native probe, GPU core, and tools (via build.sh),
organizes all required runtime files, scripts, patches, and launcher into a
clean, self-contained distribution layout, and compresses it into a .tar.gz
(or .tar.xz / .zip) ready to publish on GitHub Releases or distribute.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent
DEVELOPER = "SrShadowy"

def run_command(cmd, cwd=ROOT_DIR, check=True):
    print(f"\n[*] Running: {' '.join(cmd)}")
    res = subprocess.run(cmd, cwd=cwd)
    if check and res.returncode != 0:
        print(f"\n[!] Command failed with exit code {res.returncode}: {' '.join(cmd)}", file=sys.stderr)
        sys.exit(res.returncode)
    return res.returncode

def get_git_version():
    ver_file = ROOT_DIR / "VERSION"
    if ver_file.is_file():
        try:
            v = ver_file.read_text(encoding="utf-8").strip()
            if v:
                return v
        except OSError:
            pass
    try:
        res = subprocess.run(
            ["git", "describe", "--tags", "--always"],
            cwd=ROOT_DIR,
            capture_output=True,
            text=True,
            check=True
        )
        ver = res.stdout.strip()
        if ver.startswith("v"):
            ver = ver[1:]
        return ver
    except Exception:
        return "0.6.5-dev-24"

def get_ldd_dependencies(binary_path):
    """Parses ldd output to find dynamic library dependencies."""
    libs = {}
    try:
        res = subprocess.run(
            ["ldd", str(binary_path)],
            capture_output=True,
            text=True,
            check=True
        )
        for line in res.stdout.splitlines():
            m = re.match(r"\s*([^\s]+)\s*=>\s*([^\s]+)\s*\(0x", line)
            if m:
                libs[m.group(1)] = Path(m.group(2))
    except Exception as e:
        print(f"[!] Warning: failed to run ldd on {binary_path}: {e}")
    return libs

def should_bundle_library(name, path_str):
    """
    Decides whether a library should be bundled with the release.
    System/core libraries (glibc, X11, Vulkan loader, GPU driver libs) are excluded.
    Libraries from ~/.local or specific portable libraries (SDL3, miniz, zydis, fmt, xxhash) are bundled.
    """
    # Core system libraries that must ALWAYS come from the host OS
    system_prefixes = (
        "libc.so", "libm.so", "libpthread.so", "libdl.so", "librt.so",
        "ld-linux-x86-64.so", "libstdc++.so", "libgcc_s.so",
        "libGL.so", "libEGL.so", "libGLX.so", "libGLdispatch.so",
        "libvulkan.so", "libdrm.so", "libX11.so", "libxcb.so",
        "libresolv.so", "libnss", "libutil.so", "libsystemd.so"
    )
    if name in ("libbbgpu.so", "libbbnet.so", "libbbnet20.so"):
        return False

    for p in system_prefixes:
        if name.startswith(p):
            return False

    # Always bundle if installed in non-standard / home / local paths
    if ".local" in path_str or "shadowy" in path_str:
        return True

    # Bundle specific modern libraries that older distributions often lack
    target_keywords = (
        "sdl3", "miniz", "zydis", "zycore", "fmt", "xxhash",
        "avcodec", "avformat", "avutil", "swscale", "swresample",
        "miniupnpc", "protobuf", "absl", "utf8_validity",
    )
    name_lower = name.lower()
    for kw in target_keywords:
        if kw in name_lower:
            return True

    return False

def create_start_script(package_dir):
    """Creates the user-friendly start.sh entrypoint in the package root."""
    start_sh = package_dir / "start.sh"
    content = """#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# BBPort Launcher & Game Runner
# Usage:
#   ./start.sh          -> Abre o Launcher GTK4 (ou o jogo se o launcher não puder ser aberto)
#   ./start.sh --play   -> Inicia o jogo diretamente com as configurações salvas
#   ./start.sh --help   -> Mostra opções do runtime
set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export BB_PORT_DIR="$HERE"
export BB_PREBUILT=1

# Configura busca de bibliotecas compartilhadas empacotadas
if [[ -d "$HERE/lib" ]]; then
    export LD_LIBRARY_PATH="$HERE/lib:$HERE/bin/gpu:${LD_LIBRARY_PATH:-}"
else
    export LD_LIBRARY_PATH="$HERE/bin/gpu:${LD_LIBRARY_PATH:-}"
fi

# Shaders do FSR 4
if [[ -d "$HERE/fsr4_shaders" && -z "${BB_FSR4_SHADERS:-}" ]]; then
    export BB_FSR4_SHADERS="$HERE/fsr4_shaders"
fi

# Python bytecode cache for instant startup and fast cold launches
export PYTHONPYCACHEPREFIX="${XDG_CACHE_HOME:-$HOME/.cache}/bbport/pycache"

# Configure launcher backend: respect BB_LAUNCHER_BACKEND if set (x11 or wayland),
# otherwise default to modern OpenGL renderer and let GTK negotiate native Wayland/X11 automatically.
if [[ "${BB_LAUNCHER_BACKEND:-}" == "x11" ]]; then
    export GDK_BACKEND="x11"
    export GSK_RENDERER="${GSK_RENDERER:-gl}"
elif [[ "${BB_LAUNCHER_BACKEND:-}" == "wayland" ]]; then
    export GDK_BACKEND="wayland"
    export GSK_RENDERER="${GSK_RENDERER:-gl}"
else
    export GSK_RENDERER="${GSK_RENDERER:-gl}"
fi

# Modo direto (--play ou --game)
if [[ "${1:-}" == "--play" || "${1:-}" == "--game" ]]; then
    shift
    exec "$HERE/run.sh" "$@"
fi

# 1. Tenta abrir via Python nativo do sistema se GTK 4 e Libadwaita estiverem instalados (melhor integracao com a distro)
if python3 -c 'import gi; gi.require_version("Gtk", "4.0"); gi.require_version("Adw", "1")' 2>/dev/null; then
    exec python3 "$HERE/launcher/bbport_launcher.py" "$@"
# 2. Fallback: Tenta abrir o Launcher standalone empacotado
elif [[ -x "$HERE/launcher/bbport-launcher" ]]; then
    exec "$HERE/launcher/bbport-launcher" "$@"
elif [[ -x "$HERE/bin/bbport-launcher" ]]; then
    exec "$HERE/bin/bbport-launcher" "$@"
else
    echo "================================================================="
    echo "Aviso: Launcher standalone ou GTK 4/Libadwaita não encontrados."
    echo "Iniciando Bloodborne diretamente via run.sh..."
    echo "================================================================="
    exec "$HERE/run.sh" "$@"
fi
"""
    start_sh.write_text(content, encoding="utf-8")
    start_sh.chmod(0o755)

def create_readme(package_dir, version):
    """Creates README_RELEASE.md with execution instructions."""
    readme = package_dir / "README_RELEASE.md"
    content = f"""# Bloodborne PC Port (BBPort) - Release {version}

Versão compilada e pronta para execução em distribuições Linux (x86_64).

---

## 🎮 Como Jogar

1. **Coloque a pasta do jogo PS4 (`CUSA03173` contendo `eboot.bin`):**
   - Na mesma pasta deste pacote (ou ao lado dela), **OU**
   - Abra o inicializador e selecione a pasta da sua cópia do jogo.

2. **Inicie o jogo:**
   - **Com Launcher visual (GTK4 / Libadwaita):**
     ```bash
     ./start.sh
     ```
   - **Direto para o jogo (sem interface de launcher):**
     ```bash
     ./start.sh --play
     # ou
     ./run.sh
     ```

---

## ⚙️ Atalhos e Menus no Jogo
- **Menu de Configurações / UpScaler / Debugger:** Tecla `Insert` ou `L3 + R3` no controle.
- **Teclado Virtual:** Suporte a Gamepad e Teclado com navegação por D-pad / analógico.
- **Fechar:** `Alt + F4` ou `Esc` (no menu).

---

## 📦 Requisitos do Sistema
- **Sistema Operacional:** Linux x86_64 (glibc compatível).
- **GPU:** Suporte a **Vulkan 1.3** com drivers atualizados:
  - **AMD:** Mesa RADV (GCN 4 / Polaris, Vega, RDNA 1/2/3/4).
  - **NVIDIA:** Driver oficial proprietário (versão 535+ recomendada).
  - **Intel:** Mesa ANV (Intel Arc, Xe, Iris Xe).
- **Python 3:** Necessário para scripts de patches e carregamento.
- **Launcher:** Totalmente standalone! Inclui GTK 4, Libadwaita e interpretador embutidos (não requer instalação de pacotes do sistema).

---

*Gerado automaticamente pelo script package.py*
"""
    readme.write_text(content, encoding="utf-8")

def copy_tree_filtered(src_dir, dst_dir, ignore_patterns=(".pyc", "__pycache__", ".git")):
    """Recursively copies directory tree filtering out temporary and cache files."""
    dst_dir.mkdir(parents=True, exist_ok=True)
    for root, dirs, files in os.walk(src_dir):
        # Prune ignored directories
        dirs[:] = [d for d in dirs if not any(p in d for p in ignore_patterns)]
        rel = Path(root).relative_to(src_dir)
        target_sub = dst_dir / rel
        target_sub.mkdir(parents=True, exist_ok=True)
        for f in files:
            if any(f.endswith(p) or p in f for p in ignore_patterns):
                continue
            src_f = Path(root) / f
            dst_f = target_sub / f
            shutil.copy2(src_f, dst_f)

def deploy_launcher_wrapper(staging_dir):
    """
    Deploys the smart launcher wrapper at launcher/bbport-launcher.
    Executes bbport_launcher.py directly via Python with precompiled bytecode and XDG cache.
    """
    launcher_dir = staging_dir / "launcher"
    launcher_dir.mkdir(parents=True, exist_ok=True)
    wrapper_path = launcher_dir / "bbport-launcher"

    wrapper_content = """#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Bloodborne Port (BBPort) - Smart Launcher Wrapper
set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$HERE/.." && pwd)"

# Shared library search path for packaged runtime
if [[ -d "$ROOT/lib" ]]; then
    export LD_LIBRARY_PATH="$ROOT/lib:$ROOT/bin/gpu:${LD_LIBRARY_PATH:-}"
else
    export LD_LIBRARY_PATH="$ROOT/bin/gpu:${LD_LIBRARY_PATH:-}"
fi

export BB_PORT_DIR="$ROOT"
export BB_PREBUILT=1

if [[ -d "$ROOT/fsr4_shaders" && -z "${BB_FSR4_SHADERS:-}" ]]; then
    export BB_FSR4_SHADERS="$ROOT/fsr4_shaders"
fi

# Python bytecode cache for instant startup and fast cold launches
export PYTHONPYCACHEPREFIX="${XDG_CACHE_HOME:-$HOME/.cache}/bbport/pycache"

# Configure launcher backend: respect BB_LAUNCHER_BACKEND if set (x11 or wayland),
# otherwise inspect saved settings.json or CLI flags (--x11 / --wayland).
if [[ -z "${BB_LAUNCHER_BACKEND:-}" ]]; then
    _CFG_FILE="${XDG_CONFIG_HOME:-$HOME/.config}/bbport-launcher/settings.json"
    if [[ -f "$_CFG_FILE" ]]; then
        if grep -q '"video_backend"[[:space:]]*:[[:space:]]*"x11"' "$_CFG_FILE" 2>/dev/null; then
            BB_LAUNCHER_BACKEND="x11"
        elif grep -q '"video_backend"[[:space:]]*:[[:space:]]*"wayland"' "$_CFG_FILE" 2>/dev/null; then
            BB_LAUNCHER_BACKEND="wayland"
        fi
    fi
fi

for _arg in "$@"; do
    if [[ "$_arg" == "--x11" ]]; then
        BB_LAUNCHER_BACKEND="x11"
    elif [[ "$_arg" == "--wayland" ]]; then
        BB_LAUNCHER_BACKEND="wayland"
    fi
done

if [[ "${BB_LAUNCHER_BACKEND:-}" == "x11" ]]; then
    export GDK_BACKEND="x11"
    export GSK_RENDERER="${GSK_RENDERER:-gl}"
elif [[ "${BB_LAUNCHER_BACKEND:-}" == "wayland" ]]; then
    export GDK_BACKEND="wayland"
    export GSK_RENDERER="${GSK_RENDERER:-gl}"
else
    export GSK_RENDERER="${GSK_RENDERER:-gl}"
fi

exec python3 "$HERE/bbport_launcher.py" "$@"
"""
    wrapper_path.write_text(wrapper_content, encoding="utf-8")
    wrapper_path.chmod(0o755)
    print(f"[✓] Smart Launcher wrapper deployed to {wrapper_path}")

    # Convenience symlink in bin/
    bin_launcher = staging_dir / "bin" / "bbport-launcher"
    if bin_launcher.parent.exists() and not bin_launcher.exists():
        try:
            bin_launcher.symlink_to("../launcher/bbport-launcher")
        except OSError:
            pass


def build_and_package(args):
    print("=" * 70)
    print("      BBPort - Automated Build & Release Packager")
    print("=" * 70)

    version = args.name_version or get_git_version()
    print(f"[*] Detected Version: {version}")

    # 1. Build step
    if args.build:
        print("\n[+] Step 1: Building binaries with build.sh...")
        run_command(["bash", "build.sh"])
    else:
        print("\n[+] Step 1: Skipping build (--no-build specified).")

    # 2. Verify binary existence
    bin_probe = ROOT_DIR / "out" / "bb-probe"
    bin_caps = ROOT_DIR / "out" / "bb-gpu-capabilities"
    lib_gpu = ROOT_DIR / "out" / "gpu" / "libbbgpu.so"
    lib_net = ROOT_DIR / "out" / "gpu" / "libbbnet.so"
    lib_net20 = ROOT_DIR / "out" / "gpu" / "libbbnet20.so"
    has_online = lib_net.exists() and lib_net.stat().st_size > 0
    has_online20 = lib_net20.exists() and lib_net20.stat().st_size > 0

    if has_online20:
        print(f"[+] Online module (v0.20) detected: {lib_net20}")
    if has_online:
        print(f"[+] Online module (legacy) detected: {lib_net}")
    if not has_online and not has_online20:
        print("[!] Notice: Online modules (out/gpu/libbbnet*.so) not found; packaging offline-only.")

    missing = [str(p) for p in [bin_probe, bin_caps, lib_gpu] if not p.exists() or p.stat().st_size == 0]
    if missing:
        print(f"\n[!] Error: Expected build output is missing or empty:\n    " + "\n    ".join(missing), file=sys.stderr)
        print("[!] Please run without --no-build or inspect out/gpu-build.log.", file=sys.stderr)
        sys.exit(1)

    # 3. Setup staging folder
    pkg_name = f"BBPort-{version}-linux-x86_64"
    staging_dir = ROOT_DIR / "out" / "package_staging" / pkg_name

    if staging_dir.exists():
        if args.clean or True:
            shutil.rmtree(staging_dir)
    staging_dir.mkdir(parents=True, exist_ok=True)

    print(f"\n[+] Step 2: Populating package structure in {staging_dir}...")

    # bin/ directory
    pkg_bin = staging_dir / "bin"
    pkg_bin_gpu = pkg_bin / "gpu"
    pkg_bin_gpu.mkdir(parents=True, exist_ok=True)

    shutil.copy2(bin_probe, pkg_bin / "bb-probe")
    (pkg_bin / "bb-probe").chmod(0o755)

    shutil.copy2(bin_caps, pkg_bin / "bb-gpu-capabilities")
    (pkg_bin / "bb-gpu-capabilities").chmod(0o755)

    shutil.copy2(lib_gpu, pkg_bin_gpu / "libbbgpu.so")
    (pkg_bin_gpu / "libbbgpu.so").chmod(0o755)

    if has_online:
        shutil.copy2(lib_net, pkg_bin_gpu / "libbbnet.so")
        (pkg_bin_gpu / "libbbnet.so").chmod(0o755)
        print(f"[✓] Deployed online module: {pkg_bin_gpu / 'libbbnet.so'}")

    if has_online20:
        shutil.copy2(lib_net20, pkg_bin_gpu / "libbbnet20.so")
        (pkg_bin_gpu / "libbbnet20.so").chmod(0o755)
        print(f"[✓] Deployed online module (v0.20): {pkg_bin_gpu / 'libbbnet20.so'}")

    if args.strip:
        print("[*] Stripping debug symbols (--strip-debug)...")
        try:
            subprocess.run(["strip", "--strip-debug", str(pkg_bin_gpu / "libbbgpu.so")], check=True)
            subprocess.run(["strip", "--strip-debug", str(pkg_bin / "bb-gpu-capabilities")], check=True)
            if has_online:
                subprocess.run(["strip", "--strip-debug", str(pkg_bin_gpu / "libbbnet.so")], check=True)
            if has_online20:
                subprocess.run(["strip", "--strip-debug", str(pkg_bin_gpu / "libbbnet20.so")], check=True)
        except Exception as e:
            print(f"[!] Warning: strip failed: {e}")

    # lib/ directory for bundled dependencies
    if args.bundle_libs:
        print("[*] Collecting shared library dependencies for bundling...")
        pkg_lib = staging_dir / "lib"
        pkg_lib.mkdir(parents=True, exist_ok=True)

        found_deps = {}
        queue = [bin_probe, bin_caps, lib_gpu]
        if has_online:
            queue.append(lib_net)
        if has_online20:
            queue.append(lib_net20)
        processed = set()
        while queue:
            target = queue.pop(0)
            try:
                target_real = target.resolve()
            except Exception:
                target_real = target
            if target_real in processed:
                continue
            processed.add(target_real)
            deps = get_ldd_dependencies(target)
            for name, path in deps.items():
                if name not in found_deps:
                    found_deps[name] = path
                    if should_bundle_library(name, str(path)) and path.exists():
                        queue.append(path)

        bundled_count = 0
        for name, path in sorted(found_deps.items()):
            if should_bundle_library(name, str(path)):
                if path.exists():
                    target_file = pkg_lib / path.name
                    # Copy actual file and maintain symlink if any
                    real_file = path.resolve()
                    shutil.copy2(real_file, pkg_lib / real_file.name)
                    if real_file.name != path.name:
                        symlink_dest = pkg_lib / path.name
                        if not symlink_dest.exists():
                            symlink_dest.symlink_to(real_file.name)
                    print(f"    -> Bundled: {name} ({path})")
                    bundled_count += 1

        print(f"[*] Bundled {bundled_count} shared libraries into lib/")

        # Convenience symlinks so any relative $ORIGIN rpath ($ORIGIN/lib, $ORIGIN/../lib, $ORIGIN/../../lib) resolves to lib/
        try:
            bin_lib_link = pkg_bin / "lib"
            if not bin_lib_link.exists():
                bin_lib_link.symlink_to("../lib")
            bin_gpu_lib_link = pkg_bin_gpu / "lib"
            if not bin_gpu_lib_link.exists():
                bin_gpu_lib_link.symlink_to("../../lib")
        except OSError:
            pass

    # Root execution scripts & configuration
    pkg_run = staging_dir / "run.sh"
    run_sh_src = (ROOT_DIR / "run.sh").read_text(encoding="utf-8")
    # Ensure BB_PREBUILT=1 defaults to on if running from package root
    if "export BB_PREBUILT=" not in run_sh_src:
        run_sh_src = run_sh_src.replace("set -euo pipefail\n", "set -euo pipefail\nexport BB_PREBUILT=${BB_PREBUILT:-1}\n", 1)
    pkg_run.write_text(run_sh_src, encoding="utf-8")
    pkg_run.chmod(0o755)

    create_start_script(staging_dir)
    create_readme(staging_dir, version)
    (staging_dir / "VERSION").write_text(version + "\n", encoding="utf-8")

    if (ROOT_DIR / "patches.json").exists():
        shutil.copy2(ROOT_DIR / "patches.json", staging_dir / "patches.json")

    # Directory trees
    print("[*] Copying launcher...")
    copy_tree_filtered(ROOT_DIR / "launcher", staging_dir / "launcher")
    (staging_dir / "launcher" / "VERSION").write_text(version + "\n", encoding="utf-8")
    # Make launcher scripts executable
    if (staging_dir / "launcher" / "bb-launcher.sh").exists():
        (staging_dir / "launcher" / "bb-launcher.sh").chmod(0o755)

    # Precompile Python bytecode in staging for fast cold launches
    try:
        import compileall
        compileall.compile_dir(staging_dir / "launcher", quiet=1)
    except Exception:
        pass

    deploy_launcher_wrapper(staging_dir)

    print("[*] Copying scripts...")
    copy_tree_filtered(ROOT_DIR / "scripts", staging_dir / "scripts")

    print("[*] Copying patches...")
    copy_tree_filtered(ROOT_DIR / "patches", staging_dir / "patches")

    if args.bundle_shaders and (ROOT_DIR / "fsr4_shaders").exists():
        print("[*] Copying fsr4_shaders...")
        copy_tree_filtered(ROOT_DIR / "fsr4_shaders", staging_dir / "fsr4_shaders")

    print("[*] Copying tools...")
    copy_tree_filtered(ROOT_DIR / "tools", staging_dir / "tools")
    if (staging_dir / "tools" / "fetch_fsr4_assets.sh").exists():
        (staging_dir / "tools" / "fetch_fsr4_assets.sh").chmod(0o755)

    # Empty user and mods directory placeholders
    (staging_dir / "mods").mkdir(exist_ok=True)
    (staging_dir / "mods" / ".gitkeep").touch()
    (staging_dir / "user").mkdir(exist_ok=True)
    (staging_dir / "user" / ".gitkeep").touch()

    # 4. Create compressed tarball
    out_dist_dir = Path(args.output_dir).resolve()
    out_dist_dir.mkdir(parents=True, exist_ok=True)

    archive_ext = ".tar.gz"
    if args.format == "tar.xz" or args.format == "xztar":
        archive_ext = ".tar.xz"
        tar_mode = "w:xz"
    else:
        archive_ext = ".tar.gz"
        tar_mode = "w:gz"

    archive_path = out_dist_dir / f"{pkg_name}{archive_ext}"
    print(f"\n[+] Step 3: Compressing into {archive_path.name}...")

    with tarfile.open(archive_path, tar_mode) as tar:
        tar.add(staging_dir, arcname=pkg_name)

    # 5. Compute SHA256 checksum
    hasher = hashlib.sha256()
    with open(archive_path, "rb") as f:
        while chunk := f.read(65536):
            hasher.update(chunk)
    sha256_hash = hasher.hexdigest()

    checksum_file = out_dist_dir / f"{archive_path.name}.sha256"
    checksum_file.write_text(f"{sha256_hash}  {archive_path.name}\n", encoding="utf-8")

    archive_size_mb = archive_path.stat().st_size / (1024 * 1024)

    print("\n" + "=" * 70)
    print("             RELEASE PACKAGE CREATED SUCCESSFULLY!")
    # 6. Optional: Create Native AppImage if requested
    if getattr(args, "appimage", False):
        build_native_appimage(staging_dir, out_dist_dir, pkg_name, version)

def build_native_appimage(staging_dir, out_dist_dir, pkg_name, version):
    print("\n" + "=" * 70)
    print("      BUILDING NATIVE APPIMAGE (HIGH COMPRESSION)")
    print("=" * 70)

    # 1. Prepare AppDir metadata
    apprun_src = ROOT_DIR / "packaging" / "AppRun"
    desktop_src = ROOT_DIR / "packaging" / "bbport.desktop"
    icon_src = ROOT_DIR / "packaging" / "bbport.png"

    if apprun_src.exists():
        shutil.copy2(apprun_src, staging_dir / "AppRun")
        (staging_dir / "AppRun").chmod(0o755)

    if desktop_src.exists():
        shutil.copy2(desktop_src, staging_dir / "bbport.desktop")
        # Version (number only) and developer into the embedded .desktop; launchers like Steam
        # read them instead of showing "Unknown".
        try:
            desktop_file = staging_dir / "bbport.desktop"
            text = desktop_file.read_text(encoding="utf-8")
            lines = [line for line in text.splitlines()
                     if not (line.startswith("Version=") or line.startswith("Developer=") or line.startswith("X-AppImage-Version="))]
            # FreeDesktop spec: 'Version' is the specification version (1.0).
            # Application version belongs in X-AppImage-Version.
            lines.insert(1, "Version=1.0")
            lines.insert(2, f"X-AppImage-Version={version}")
            lines.insert(3, f"X-Developer={DEVELOPER}")
            desktop_file.write_text("\n".join(lines) + "\n", encoding="utf-8")
        except OSError as error:
            print(f"[!] Warning: could not write Version into .desktop: {error}", file=sys.stderr)

    if icon_src.exists():
        shutil.copy2(icon_src, staging_dir / "bbport.png")
        shutil.copy2(icon_src, staging_dir / ".DirIcon")

    # 2. Locate or download appimagetool
    appimagetool = shutil.which("appimagetool")
    if not appimagetool:
        local_tool = ROOT_DIR / "out" / "tools" / "appimagetool"
        if not local_tool.exists():
            local_tool.parent.mkdir(parents=True, exist_ok=True)
            print("[*] Downloading portable appimagetool...")
            import urllib.request
            url = "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
            urllib.request.urlretrieve(url, local_tool)
            local_tool.chmod(0o755)
        appimagetool = str(local_tool)

    appimage_name = "Bloodborne-bbport-native-x86_64.AppImage"
    appimage_path = out_dist_dir / appimage_name

    env = os.environ.copy()
    env["ARCH"] = "x86_64"
    env["APPIMAGE_EXTRACT_AND_RUN"] = "1"

    print(f"[*] Packaging AppImage into {appimage_name}...")
    cmd = [
        appimagetool,
        "--no-appstream",
        str(staging_dir),
        str(appimage_path)
    ]
    # In containers/environments without FUSE, self-extract appimagetool
    if appimagetool.endswith(".AppImage") or "appimagetool" in appimagetool:
        cmd.insert(1, "--appimage-extract-and-run")
    res = subprocess.run(cmd, env=env)
    if res.returncode != 0:
        print(f"[!] Error: appimagetool failed with code {res.returncode}", file=sys.stderr)
        return None

    hasher = hashlib.sha256()
    with open(appimage_path, "rb") as f:
        while chunk := f.read(65536):
            hasher.update(chunk)
    appimage_sha = hasher.hexdigest()
    (out_dist_dir / f"{appimage_name}.sha256").write_text(f"{appimage_sha}  {appimage_name}\n", encoding="utf-8")

    size_mb = appimage_path.stat().st_size / (1024 * 1024)
    print("\n" + "=" * 70)
    print("        NATIVE APPIMAGE CREATED SUCCESSFULLY!")
    print("=" * 70)
    print(f"🚀 AppImage: {appimage_path}")
    print(f"📊 Size:     {size_mb:.2f} MB")
    print(f"🔑 SHA256:   {appimage_sha}")
    return appimage_path

def main():
    parser = argparse.ArgumentParser(description="Build and package BBPort release tarball.")
    parser.add_argument("--appimage", action="store_true", default=False,
                        help="Generate a standalone native AppImage alongside the release tarball")
    parser.add_argument("--no-build", dest="build", action="store_false", default=True,
                        help="Skip compiling binaries with build.sh (use existing out/)")
    parser.add_argument("--output-dir", "-o", default="dist",
                        help="Output directory for the packaged archive (default: dist/)")
    parser.add_argument("--format", choices=["gztar", "tar.gz", "xztar", "tar.xz"], default="gztar",
                        help="Archive compression format (default: gztar)")
    parser.add_argument("--name-version", default=None,
                        help="Custom version string for package naming (default: from git)")
    parser.add_argument("--no-bundle-libs", dest="bundle_libs", action="store_false", default=True,
                        help="Do not bundle third-party dynamic libraries (SDL3, miniz, zydis, etc.)")
    parser.add_argument("--no-bundle-shaders", dest="bundle_shaders", action="store_false", default=True,
                        help="Do not bundle fsr4_shaders/ folder")
    parser.add_argument("--no-strip", dest="strip", action="store_false", default=True,
                        help="Do not strip debug symbols from libbbgpu.so")
    parser.add_argument("--no-standalone-launcher", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--rebuild-launcher", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--docker", action="store_true", default=False,
                        help="Build inside an Ubuntu 22.04 Docker container for maximum Linux distribution compatibility (glibc 2.35)")
    parser.add_argument("--clean", action="store_true", default=True,
                        help="Clean staging directory before packaging")

    args = parser.parse_args()
    if args.docker:
        docker_script = ROOT_DIR / "packaging" / "docker-build.sh"
        if not docker_script.exists():
            print("[!] Error: packaging/docker-build.sh not found", file=sys.stderr)
            sys.exit(1)
        passthrough_args = [arg for arg in sys.argv[1:] if arg != "--docker"]
        sys.exit(subprocess.run(["bash", str(docker_script)] + passthrough_args).returncode)

    build_and_package(args)

if __name__ == "__main__":
    main()
