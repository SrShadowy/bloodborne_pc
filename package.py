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

def run_command(cmd, cwd=ROOT_DIR, check=True):
    print(f"\n[*] Running: {' '.join(cmd)}")
    res = subprocess.run(cmd, cwd=cwd)
    if check and res.returncode != 0:
        print(f"\n[!] Command failed with exit code {res.returncode}: {' '.join(cmd)}", file=sys.stderr)
        sys.exit(res.returncode)
    return res.returncode

def get_git_version():
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
        return "0.6"

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

def ensure_pyinstaller():
    """Locates pyinstaller or bootstraps a virtualenv with pyinstaller if missing."""
    for candidate in [
        shutil.which("pyinstaller"),
        ROOT_DIR / "out" / ".venv" / "bin" / "pyinstaller",
        Path("/tmp/test_venv/bin/pyinstaller"),
    ]:
        if candidate and Path(candidate).is_file():
            return Path(candidate)

    venv_dir = ROOT_DIR / "out" / ".venv"
    venv_pyi = venv_dir / "bin" / "pyinstaller"
    if venv_pyi.is_file():
        return venv_pyi

    venv_dir.parent.mkdir(parents=True, exist_ok=True)
    try:
        print("[*] PyInstaller not found. Bootstrapping out/.venv with system-site-packages...")
        subprocess.run([sys.executable, "-m", "venv", "--system-site-packages", str(venv_dir)], check=True)
        subprocess.run([str(venv_dir / "bin" / "pip"), "install", "pyinstaller"], check=True)
        return venv_pyi
    except Exception as e:
        print(f"[*] Note: Virtualenv creation failed ({e}). Attempting direct pip install...")
        try:
            subprocess.run([sys.executable, "-m", "pip", "install", "--no-cache-dir", "pyinstaller"], check=True)
            pyi = shutil.which("pyinstaller")
            if pyi:
                return Path(pyi)
        except Exception:
            pass
        raise

def deploy_launcher_wrapper(staging_dir, cached_binary=None):
    """
    Deploys the smart launcher wrapper at launcher/bbport-launcher.
    If a standalone binary is available, deploys it as launcher/bbport-launcher.bin.
    """
    launcher_dir = staging_dir / "launcher"
    launcher_dir.mkdir(parents=True, exist_ok=True)
    wrapper_path = launcher_dir / "bbport-launcher"
    binary_path = launcher_dir / "bbport-launcher.bin"

    if cached_binary and cached_binary.is_file():
        shutil.copy2(cached_binary, binary_path)
        binary_path.chmod(0o755)
        print(f"[✓] Standalone Launcher binary deployed to {binary_path}")

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

# 1. Native system Python with GTK 4 and Libadwaita (best desktop integration, native theme and 0 ABI conflicts)
if python3 -c 'import gi; gi.require_version("Gtk", "4.0"); gi.require_version("Adw", "1")' 2>/dev/null; then
    exec python3 "$HERE/bbport_launcher.py" "$@"
fi

# 2. Standalone compiled executable (bundled fallback)
if [[ -x "$HERE/bbport-launcher.bin" ]]; then
    exec "$HERE/bbport-launcher.bin" "$@"
fi

# 3. Direct execution fallback
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


def build_standalone_launcher(staging_dir, force_rebuild=False):
    """
    Builds a standalone executable for bbport_launcher with embedded GTK4, Libadwaita,
    PyGObject, and Python runtime using PyInstaller.
    """
    cached_binary = ROOT_DIR / "out" / "bbport-launcher"

    need_rebuild = force_rebuild or not cached_binary.is_file()
    if not need_rebuild:
        cached_mtime = cached_binary.stat().st_mtime
        for check_dir in [ROOT_DIR / "launcher", ROOT_DIR / "scripts", ROOT_DIR / "tools"]:
            if need_rebuild:
                break
            for root, _, files in os.walk(check_dir):
                for f in files:
                    if f.endswith(".py"):
                        if (Path(root) / f).stat().st_mtime > cached_mtime:
                            need_rebuild = True
                            break
                if need_rebuild:
                    break

    if not need_rebuild and cached_binary.is_file():
        print(f"\\n[+] Reusing existing compiled launcher binary: {cached_binary}")
        deploy_launcher_wrapper(staging_dir, cached_binary)
        return

    print("\\n[+] Building standalone GTK4/Libadwaita Launcher executable...")
    try:
        pyi_path = ensure_pyinstaller()
        print(f"[*] Using PyInstaller: {pyi_path}")
    except Exception as e:
        print(f"[!] Warning: PyInstaller is unavailable in this environment: {e}")
        deploy_launcher_wrapper(staging_dir, cached_binary if cached_binary.is_file() else None)
        return

    launcher_src_dir = ROOT_DIR / "launcher"
    dist_dir = ROOT_DIR / "out" / "pyi_dist"
    work_dir = ROOT_DIR / "out" / "pyi_work"
    pyi_cmd = [
        str(pyi_path),
        "--noconfirm",
        "--onefile",
        "--clean",
        "--name", "bbport-launcher",
        "--paths", str(ROOT_DIR / "launcher"),
        "--paths", str(ROOT_DIR / "scripts"),
        "--paths", str(ROOT_DIR / "tools"),
        "--collect-all", "gi",
        "--hidden-import", "gi",
        "--hidden-import", "gi.repository.Gtk",
        "--hidden-import", "gi.repository.Adw",
        "--hidden-import", "gi.repository.Gio",
        "--hidden-import", "gi.repository.GLib",
        "--hidden-import", "gi.repository.Gdk",
        "--hidden-import", "gi.repository.Pango",
        "--distpath", str(dist_dir),
        "--workpath", str(work_dir),
        "--specpath", str(work_dir),
        str(launcher_src_dir / "bbport_launcher.py"),
    ]
    try:
        ret = run_command(pyi_cmd, check=False)
        if ret != 0:
            raise RuntimeError(f"PyInstaller exited with code {ret}")
        built_file = dist_dir / "bbport-launcher"
        if not built_file.is_file():
            raise RuntimeError(f"PyInstaller failed to create {built_file}")
        shutil.copy2(built_file, cached_binary)
        cached_binary.chmod(0o755)
    except Exception as e:
        print(f"[!] Warning: Failed to compile standalone launcher with PyInstaller: {e}")

    deploy_launcher_wrapper(staging_dir, cached_binary if cached_binary.is_file() else None)

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

    if (ROOT_DIR / "patches.json").exists():
        shutil.copy2(ROOT_DIR / "patches.json", staging_dir / "patches.json")

    # Directory trees
    print("[*] Copying launcher...")
    copy_tree_filtered(ROOT_DIR / "launcher", staging_dir / "launcher")
    # Make launcher scripts executable
    if (staging_dir / "launcher" / "bb-launcher.sh").exists():
        (staging_dir / "launcher" / "bb-launcher.sh").chmod(0o755)

    if args.standalone_launcher:
        build_standalone_launcher(staging_dir, force_rebuild=args.rebuild_launcher)
    else:
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
    print("=" * 70)
    print(f"📦 Archive:  {archive_path}")
    print(f"📊 Size:     {archive_size_mb:.2f} MB")
    print(f"🔑 SHA256:   {sha256_hash}")
    print(f"📄 Checksum: {checksum_file}")
    print("=" * 70)
    print("\nPronto para publicação! Você pode fazer upload deste .tar.gz diretamente no GitHub Releases.")

def main():
    parser = argparse.ArgumentParser(description="Build and package BBPort release tarball.")
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
    parser.add_argument("--no-standalone-launcher", dest="standalone_launcher", action="store_false", default=True,
                        help="Do not compile a standalone PyInstaller launcher with bundled GTK4/Libadwaita")
    parser.add_argument("--rebuild-launcher", action="store_true", default=False,
                        help="Force rebuilding standalone launcher executable even if cached binary is present")
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
        sys.exit(subprocess.run(["bash", str(docker_script)]).returncode)

    build_and_package(args)

if __name__ == "__main__":
    main()
