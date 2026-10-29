#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 bbport contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Select Vulkan ICDs for the packaged port before starting GTK or the game.

Mesa comes with the default package (the minimal variant leaves it out: the system's
own drivers, found by the loader's default search). NVIDIA's userspace driver must come
from the host, matching its kernel module. Expose only NVIDIA libraries, rather than the
host's whole library directory (which would also replace the package's libc/GTK/SDL).
"""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

MANIFEST_DIRS = (
    Path("/etc/vulkan/icd.d"), Path("/usr/share/vulkan/icd.d"),
    Path("/usr/local/share/vulkan/icd.d"), Path("/run/opengl-driver/share/vulkan/icd.d"),
)
LIBRARY_DIRS = (
    Path("/usr/lib/x86_64-linux-gnu"), Path("/lib/x86_64-linux-gnu"),
    Path("/usr/lib64"), Path("/lib64"), Path("/usr/lib"), Path("/lib"),
    Path("/run/opengl-driver/lib"),
)
# The host's EGL vendor files (glvnd). The package's own glvnd looks only in its own store.
EGL_VENDOR_DIRS = (
    Path("/usr/share/glvnd/egl_vendor.d"), Path("/etc/glvnd/egl_vendor.d"),
    Path("/usr/local/share/glvnd/egl_vendor.d"),
)


def pc_model_gpu(drm_dir=None):
    return True


def amd_gpu(drm_dir=None):
    return pc_model_gpu(drm_dir)


def elf64(path):
    """Reject 32-bit ICDs in distributions that install both architectures."""
    try:
        with path.open("rb") as stream:
            header = stream.read(20)
        return header[:6] == b"\x7fELF\x02\x01" and header[18:20] == b"\x3e\x00"
    except OSError:
        return False


def host_nvidia(manifest_dirs, library_dirs):
    for directory in manifest_dirs:
        for manifest in sorted(directory.glob("*nvidia*.json")):
            try:
                data = json.loads(manifest.read_text(encoding='utf-8'))
                library = Path(data["ICD"]["library_path"])
            except (OSError, ValueError, KeyError, TypeError):
                continue
            if "nvidia" not in library.name.lower():
                continue
            if library.is_absolute():
                candidates = [library, *(d / library.name for d in library_dirs)]
            elif len(library.parts) > 1:
                candidates = [manifest.parent / library]
            else:
                candidates = [d / library for d in library_dirs]
            for candidate in candidates:
                if elf64(candidate):
                    return data, candidate.resolve()
    return None


# The C runtime a driver shares with the process that dlopens it: already loaded (libc, libstdc++,
# libgcc_s for this C++ port), or merged into glibc. Never linked from the host — a second libc or
# libstdc++ beside the package's own would break the process.
CORE_LIBS = frozenset({
    "libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1", "libresolv.so.2",
    "libutil.so.1", "libmvec.so.1", "libgcc_s.so.1", "libstdc++.so.6", "ld-linux-x86-64.so.2",
    "libnss_dns.so.2", "libnss_files.so.2", "libanl.so.1", "libnsl.so.2",
})


def elf_needed(path):
    """The DT_NEEDED sonames of a 64-bit ELF, from its dynamic section.

    Needed to link a host driver's exact dependency closure (RADV pulls in libLLVM, libelf, libdrm,
    ...) without exposing the host's whole library directory, whose libc/GTK would replace the
    package's own."""
    try:
        with path.open("rb") as stream:
            header = stream.read(64)
            if header[:6] != b"\x7fELF\x02\x01" or header[18:20] != b"\x3e\0":
                return []
            phoff = int.from_bytes(header[32:40], "little")
            phentsize = int.from_bytes(header[54:56], "little")
            phnum = int.from_bytes(header[56:58], "little")
            stream.seek(phoff)
            segments = stream.read(phentsize * phnum)
            dynamic, loads = None, []
            for i in range(phnum):
                seg = segments[i * phentsize:(i + 1) * phentsize]
                if len(seg) < 48:
                    return []
                ptype = int.from_bytes(seg[0:4], "little")
                if ptype == 2:  # PT_DYNAMIC
                    dynamic = (int.from_bytes(seg[8:16], "little"),
                               int.from_bytes(seg[32:40], "little"))
                elif ptype == 1:  # PT_LOAD, to turn DT_STRTAB's address into a file offset
                    loads.append((int.from_bytes(seg[16:24], "little"),
                                  int.from_bytes(seg[8:16], "little"),
                                  int.from_bytes(seg[32:40], "little")))
            if dynamic is None:
                return []
            stream.seek(dynamic[0])
            entries = stream.read(dynamic[1])
    except OSError:
        return []
    needed, strtab_va = [], None
    for i in range(0, len(entries) - 15, 16):
        tag = int.from_bytes(entries[i:i + 8], "little", signed=True)
        if tag == 0:  # DT_NULL
            break
        if tag == 1:  # DT_NEEDED
            needed.append(int.from_bytes(entries[i + 8:i + 16], "little"))
        elif tag == 5:  # DT_STRTAB
            strtab_va = int.from_bytes(entries[i + 8:i + 16], "little")
    if not needed or strtab_va is None:
        return []
    offset = next((off + strtab_va - vaddr for vaddr, off, size in loads
                   if vaddr <= strtab_va < vaddr + size), None)
    if offset is None:
        return []
    try:
        with path.open("rb") as stream:
            stream.seek(offset)
            table = stream.read(1 << 16)
    except OSError:
        return []
    names = []
    for off in needed:
        end = table.find(b"\0", off)
        if off < len(table) and end != -1:
            names.append(table[off:end].decode("utf-8", "replace"))
    return names


def host_system_icds(manifest_dirs, library_dirs):
    """The host's non-NVIDIA ICDs (RADV/ANV in the minimal package), resolved to absolute 64-bit
    libraries, as (manifest name, manifest data, driver path) — the minimal variant's own drivers."""
    found = []
    for directory in manifest_dirs:
        for manifest in sorted(directory.glob("*.json")):
            if "nvidia" in manifest.name.lower():
                continue
            try:
                data = json.loads(manifest.read_text(encoding='utf-8'))
                library = Path(data["ICD"]["library_path"])
            except (OSError, ValueError, KeyError, TypeError):
                continue
            if library.is_absolute():
                candidates = [library, *(d / library.name for d in library_dirs)]
            elif len(library.parts) > 1:
                candidates = [manifest.parent / library]
            else:
                candidates = [d / library for d in library_dirs]
            for candidate in candidates:
                if elf64(candidate):
                    found.append((manifest.name, data, candidate.resolve()))
                    break
    return found


def system_driver_closure(driver, library_dirs):
    """The driver and its DT_NEEDED dependencies, each resolvable, core runtime (CORE_LIBS) apart.

    The dependencies are linked into one cache and found there through LD_LIBRARY_PATH; a library
    already loaded under its soname (libc, libstdc++) is reused by the linker, never linked twice."""
    queue, seen, resolved = [driver], set(), []
    while queue:
        current = queue.pop(0)
        if current.name in seen:
            continue
        seen.add(current.name)
        resolved.append(current)
        for soname in elf_needed(current):
            if soname in CORE_LIBS or soname in seen:
                continue
            for directory in dict.fromkeys((current.parent, *library_dirs)):
                candidate = directory / soname
                if elf64(candidate):
                    queue.append(candidate.resolve())
                    break
    return resolved


def expose_system_drivers(env, manifest_dirs, library_dirs, bundled):
    """The minimal package: the host's own AMD/Intel drivers, loadable inside the AppImage.

    The loader finds the host's ICD manifests (/usr/share/vulkan/icd.d), but their library_path is
    often a bare name (Debian/Ubuntu) resolved through the host's /usr/lib/x86_64-linux-gnu, which
    the AppImage's linker does not search: vkCreateInstance then reports no compatible driver. Link
    the driver and its dependencies into a cache by absolute path (as for the host's NVIDIA driver)
    instead of the host's whole directory, whose libc/GTK would shadow the package's."""
    icds = host_system_icds(manifest_dirs, library_dirs)
    if not icds:
        return None
    data_dir = Path(env.get("BB_DATA_DIR") or
                    str(Path(env.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "bbport"))
    # One cache for the set of installed drivers: their versions and mtimes decide its name.
    fingerprint = ":".join(f"{driver}:{driver.stat().st_mtime_ns}" for _, _, driver in icds)
    key = hashlib.sha256(fingerprint.encode()).hexdigest()[:16]
    cache = data_dir / "vulkan" / "system" / key
    libraries = cache / "lib"
    libraries.mkdir(parents=True, exist_ok=True)
    for _, _, driver in icds:
        for source in system_driver_closure(driver, library_dirs):
            link = libraries / source.name
            try:
                link.symlink_to(source.resolve())
            except FileExistsError:
                pass
    manifests = []
    for name, data, driver in icds:
        data["ICD"]["library_path"] = str(libraries / driver.name)
        manifest = cache / name
        temporary = cache / f"{name}.{os.getpid()}.tmp"
        temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        temporary.replace(manifest)
        manifests.append(str(manifest))
    env["VK_DRIVER_FILES"] = ":".join(manifests) + (":" + bundled if bundled else "")
    env["LD_LIBRARY_PATH"] = str(libraries) + (
        ":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    return "Vulkan: host system driver(s) " + ", ".join(
        d.name for _, _, d in icds) + " linked into the cache (no bundled Mesa)"


def configure(env, manifest_dirs=MANIFEST_DIRS, library_dirs=LIBRARY_DIRS,
              egl_dirs=EGL_VENDOR_DIRS):
    # Steam's Game Mode starts non-Steam shortcuts in pressure-vessel, which sets these to its
    # own proxy ICDs and layers: not the user's choice, and with them the bundled loader finds
    # no VK_KHR_surface (#172). Dropped, so the bundled or host drivers are found as usual.
    if any("/pressure-vessel/" in env.get(name, "")
           for name in ("VK_DRIVER_FILES", "VK_ICD_FILENAMES", "VK_IMPLICIT_LAYER_PATH")):
        for name in ("VK_DRIVER_FILES", "VK_ICD_FILENAMES", "VK_IMPLICIT_LAYER_PATH",
                     "VK_ADD_LAYER_PATH", "VK_LAYER_PATH", "VK_LOADER_LAYERS_ENABLE"):
            env.pop(name, None)
        # Its library aliases would shadow the drivers' own dependencies too.
        kept = [d for d in env.get("LD_LIBRARY_PATH", "").split(":")
                if d and "/pressure-vessel/" not in d]
        if kept:
            env["LD_LIBRARY_PATH"] = ":".join(kept)
        else:
            env.pop("LD_LIBRARY_PATH", None)
    # Both overrides belong to the user; VK_DRIVER_FILES takes precedence over
    # VK_ICD_FILENAMES. In particular don't replace an explicit Lavapipe setup.
    if env.get("VK_DRIVER_FILES") or env.get("VK_ICD_FILENAMES"):
        return "Vulkan: using explicit driver override"
    bundled = env.get("BB_BUNDLED_VK_DRIVER_FILES", "")
    if env.get("VK_ADD_DRIVER_FILES"):
        bundled = env["VK_ADD_DRIVER_FILES"] + (":" + bundled if bundled else "")
    extra = env.get("BB_NVIDIA_LIB_DIR")
    if extra:
        library_dirs = (Path(extra), *library_dirs)
    found = host_nvidia(manifest_dirs, library_dirs)
    if not found:
        if bundled:
            env["VK_DRIVER_FILES"] = bundled
            return "Vulkan: bundled AMD/Intel drivers; no accessible NVIDIA ICD found"
        # The minimal package: the host's own AMD/Intel drivers. The loader's default search finds
        # their manifests, but a bare library_path (Debian/Ubuntu) resolves through the host's
        # /usr/lib/x86_64-linux-gnu, which the AppImage's linker does not search. Link the drivers
        # and their dependencies into a cache instead (as for the host's NVIDIA driver).
        return (expose_system_drivers(env, manifest_dirs, library_dirs, bundled)
                or "Vulkan: system drivers (no bundled Mesa); no accessible NVIDIA ICD found")

    data, driver = found
    # The driver version/path and mtime give each installed driver its own small
    # cache. exec preserves process/signal behavior; the cache survives launch.
    key = hashlib.sha256(f"{driver}:{driver.stat().st_mtime_ns}".encode()).hexdigest()[:16]
    data_dir = Path(env.get("BB_DATA_DIR") or
                    str(Path(env.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))) / "bbport"))
    cache = data_dir / "vulkan" / "nvidia" / key
    libraries = cache / "lib"
    libraries.mkdir(parents=True, exist_ok=True)
    # NVIDIA dlopens versioned shader/compiler libraries at runtime, so expose
    # the driver's siblings as well as its ICD. Never link generic host libs.
    sources = [driver]
    # libcuda.so.1 is the driver's too: with DLSS loaded, NGX has the driver load it, and without it
    # vkCreateDevice fails (#135). The ICD's own directory first, then the host's library
    # directories: Debian puts the ICD in nvidia/current/ but libnvidia-glsi/-tls/-glcore one
    # level up (#205). The first file of each name wins.
    for directory in dict.fromkeys((driver.parent, *library_dirs)):
        for pattern in ("libnvidia-*.so*", "libGLX_nvidia.so*", "libEGL_nvidia.so*",
                        "libcuda.so*"):
            sources.extend(sorted(directory.glob(pattern)))
    names = set()
    for source in sources:
        if source.name in names or not elf64(source):
            continue
        names.add(source.name)
        link = libraries / source.name
        try:
            link.symlink_to(source.resolve())
        except FileExistsError:
            pass
    # Keep the manifest's API/architecture metadata, replace its library path.
    data["ICD"]["library_path"] = str(libraries / driver.name)
    manifest = cache / "nvidia_icd.json"
    temporary = cache / f"nvidia_icd.{os.getpid()}.tmp"
    temporary.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    temporary.replace(manifest)
    env["VK_DRIVER_FILES"] = str(manifest) + (":" + bundled if bundled else "")
    env["LD_LIBRARY_PATH"] = str(libraries) + (
        ":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")
    # #107: NVIDIA's ICD gave no vkCreateInstance on some hosts (a GTX 1650 laptop on CachyOS)
    # until glvnd saw the host's EGL vendor files, NVIDIA's first; the package's glvnd only looks
    # in its own store. The user's own list, or BB_NVIDIA_EGL=0, keeps it as it is.
    if not env.get("__EGL_VENDOR_LIBRARY_FILENAMES") and env.get("BB_NVIDIA_EGL") != "0":
        vendors = [p for d in egl_dirs for p in sorted(d.glob("*.json"))]
        vendors.sort(key=lambda p: "nvidia" not in p.name.lower())
        if any("nvidia" in p.name.lower() for p in vendors):
            env["__EGL_VENDOR_LIBRARY_FILENAMES"] = ":".join(str(p) for p in vendors)
    # #181/#219/#229: on X11 (Xwayland, PRIME) NVIDIA's ICD presents through EGL and its X11
    # platform (libnvidia-egl-xcb/-xlib), which needs a GBM device. The package's libgbm only
    # looks in /run/opengl-driver/lib/gbm, so off NixOS it has no nvidia-drm backend, the X11
    # platform fails, and the driver calls a null callback (eglcore +0xbb1819) at the first
    # surface query. Only NVIDIA's backend is exposed: the host's dri_gbm.so is a host-Mesa build.
    if not env.get("GBM_BACKENDS_PATH"):
        backend = next((d / "gbm" / "nvidia-drm_gbm.so" for d in dict.fromkeys(
            (driver.parent, *library_dirs)) if elf64(d / "gbm" / "nvidia-drm_gbm.so")), None)
        if backend:
            gbm = cache / "gbm"
            gbm.mkdir(exist_ok=True)
            try:
                (gbm / backend.name).symlink_to(backend.resolve())
            except FileExistsError:
                pass
            env["GBM_BACKENDS_PATH"] = f"{gbm}:/run/opengl-driver/lib/gbm"
    return f"Vulkan: host NVIDIA driver {driver}; bundled AMD/Intel also available"


def nvidia_report(env, tool, run=subprocess.run):
    """--vulkan-info with the host NVIDIA driver: what the packaged loader got, for issue reports
    (#107: the driver loaded but gave no vkCreateInstance). The linked libraries, the kernel
    module, the device nodes, hybrid-graphics variables, and the dynamic linker's view of one
    vulkaninfo run: NVIDIA libraries it searched for and never loaded."""
    manifest = next((Path(p) for p in env.get("VK_DRIVER_FILES", "").split(":")
                     if "/vulkan/nvidia/" in p), None)
    if manifest is None:
        return []
    lines = [f"NVIDIA libraries linked in {manifest.parent / 'lib'}:"]
    try:
        links = sorted((manifest.parent / "lib").iterdir())
    except OSError as error:
        links = []
        lines.append(f"  cannot list: {error}")
    for link in links:
        target = os.readlink(link) if link.is_symlink() else "(file)"
        lines.append(f"  {link.name} -> {target}{'' if link.exists() else '  MISSING'}")
    try:
        module = Path("/proc/driver/nvidia/version").read_text(encoding="utf-8").splitlines()[0]
    except (OSError, IndexError):
        module = "not loaded (no /proc/driver/nvidia/version)"
    lines.append(f"Kernel module: {module}")
    nodes = sorted(Path("/dev").glob("nvidia*"))
    lines.append("Device nodes: " + (", ".join(
        f"{n.name}{'' if os.access(n, os.R_OK | os.W_OK) else ' (no access)'}" for n in nodes)
        or "none"))
    for key in ("__NV_PRIME_RENDER_OFFLOAD", "__VK_LAYER_NV_optimus", "__GLX_VENDOR_LIBRARY_NAME",
                "DRI_PRIME", "VK_LOADER_LAYERS_DISABLE"):
        if env.get(key):
            lines.append(f"{key}={env[key]}")
    try:
        result = run([tool, "--summary"], env={**env, "LD_DEBUG": "libs"},
                     stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, timeout=60)
        trace = result.stderr
    except (OSError, subprocess.SubprocessError) as error:
        lines.append(f"Dynamic linker trace: {tool} did not run ({error})")
        return lines
    searched, loaded = [], set()
    for line in trace.splitlines():
        if (at := line.find("find library=")) != -1:
            name = line[at + len("find library="):].split(" ", 1)[0]
            if name not in searched:
                searched.append(name)
        elif (at := line.find("calling init: ")) != -1:
            loaded.add(Path(line[at + len("calling init: "):].strip()).name)
    wanted = [n for n in searched if "nvidia" in n.lower() or "GLdispatch" in n]
    missing = [n for n in wanted if n not in loaded]
    lines.append(f"Dynamic linker: {len(wanted)} NVIDIA/glvnd libraries searched, "
                 + (f"not loaded: {', '.join(missing)}" if missing else "all loaded"))
    return lines


def main():
    if len(sys.argv) < 2:
        print("Usage: bbport_vulkan.py <program> [args...]", file=sys.stderr)
        return 1
    try:
        print(configure(os.environ), file=sys.stderr, flush=True)
    except OSError as error:
        # Startup diagnostics should identify driver/cache failures rather than
        # falling through to an unrelated Vulkan assertion.
        print(f"bbport: cannot prepare NVIDIA driver: {error}", file=sys.stderr)
        return 1
    if "--vulkan-info" in sys.argv[1:]:
        tool = os.environ.get("BB_VULKANINFO", "vulkaninfo")
        os.environ.setdefault("VK_LOADER_DEBUG", "error,warn,driver")
        # Everything on stdout, with the summary: one file for an issue report (#107: the report
        # went to stderr and was missing from saved output).
        for key in ("VK_DRIVER_FILES", "VK_ICD_FILENAMES", "LD_LIBRARY_PATH",
                    "__EGL_VENDOR_LIBRARY_FILENAMES", "GBM_BACKENDS_PATH"):
            print(f"{key}={os.environ.get(key, '')}", flush=True)
        for line in nvidia_report(os.environ, tool):
            print(line, flush=True)
        os.execvpe(tool, [tool, "--summary"], os.environ)
    os.execvpe(sys.argv[1], sys.argv[1:], os.environ)


if __name__ == "__main__":
    sys.exit(main())
