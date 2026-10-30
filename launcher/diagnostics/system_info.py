# SPDX-License-Identifier: GPL-2.0-or-later
"""System environment, GPU, display backend and diagnostic reporting."""
from __future__ import annotations

import os
import platform
import sys
from pathlib import Path

import subprocess
import ctypes

from .logging_setup import get_log_dir, get_log_file


_UNSPECIFIED = object()


def get_distro_id() -> str:
    """Reads ID from /etc/os-release."""
    if sys.platform == "win32":
        return "windows"
    os_release = Path("/etc/os-release")
    if os_release.is_file():
        try:
            data = os_release.read_text(encoding="utf-8")
            for line in data.splitlines():
                if line.startswith("ID="):
                    return line.split("=", 1)[1].strip('"').lower()
        except OSError:
            pass
    return "linux"


def get_sdl3_install_command() -> str:
    """Returns the recommended terminal command to install SDL3 on the detected distro."""
    distro = get_distro_id()
    if distro == "windows":
        return "pacman -S mingw-w64-clang-x86_64-sdl3"
    elif distro in ("arch", "cachyos", "manjaro", "endeavouros", "artix"):
        return "sudo pacman -S sdl3"
    elif distro in ("ubuntu", "debian", "pop", "linuxmint", "elementary", "zorin"):
        return "sudo apt install libsdl3-0"
    elif distro in ("fedora", "rhel", "centos", "nobara"):
        return "sudo dnf install SDL3"
    elif distro in ("opensuse", "suse", "opensuse-tumbleweed", "opensuse-leap"):
        return "sudo zypper install libSDL3-0"
    elif distro in ("gentoo",):
        return "sudo emerge media-libs/libsdl3"
    elif distro in ("nixos",):
        return "nix-shell -p sdl3"
    return "Instale o pacote SDL3 da sua distribuição (ex: libsdl3 ou sdl3)."


def detect_audio_server() -> str:
    """Detects active Linux audio server (PipeWire, PulseAudio, ALSA)."""
    try:
        res = subprocess.run(["pactl", "info"], capture_output=True, text=True, timeout=1.0)
        if res.returncode == 0:
            for line in res.stdout.splitlines():
                if line.startswith("Server Name:"):
                    return line.split(":", 1)[1].strip()
    except (FileNotFoundError, subprocess.SubprocessError, PermissionError):
        pass

    runtime_dir = os.environ.get("XDG_RUNTIME_DIR")
    if runtime_dir:
        if (Path(runtime_dir) / "pipewire-0").exists():
            return "PipeWire (socket detected)"
        if (Path(runtime_dir) / "pulse" / "native").exists():
            return "PulseAudio (socket detected)"

    if Path("/proc/asound").is_dir():
        return "ALSA (direct kernel sound)"

    return "Unknown / None"


def detect_pci_gpus() -> list[tuple[str, str]]:
    """Detects graphics hardware directly on the PCI bus (/sys/bus/pci/devices)."""
    pci_dir = Path("/sys/bus/pci/devices")
    gpus = []
    if not pci_dir.is_dir():
        return gpus
    vendor_map = {
        "0x1002": "AMD",
        "0x10de": "NVIDIA",
        "0x8086": "Intel",
    }
    for dev in sorted(pci_dir.iterdir()):
        class_file = dev / "class"
        vendor_file = dev / "vendor"
        if class_file.is_file() and vendor_file.is_file():
            try:
                cls = int(class_file.read_text().strip(), 16)
                if (cls >> 16) == 0x03:
                    raw_vendor = vendor_file.read_text().strip().lower()
                    vendor_name = vendor_map.get(raw_vendor, raw_vendor)
                    gpus.append((dev.name, vendor_name))
            except Exception:
                pass
    return gpus


def check_gpu_diagnostics() -> list[str]:
    """Analyzes hardware PCI GPUs, Vulkan devices, and driver status.

    Returns a list of advisory/diagnostic messages (warnings/recommendations).
    """
    messages = []
    try:
        from bbport_devices import connected_gpus
        vk_gpus = connected_gpus()
    except Exception:
        vk_gpus = []

    pci_gpus = detect_pci_gpus()

    # 1. Vulkan availability
    if not vk_gpus:
        has_vulkan_icd = False
        for icd_path in [Path("/usr/share/vulkan/icd.d"), Path("/etc/vulkan/icd.d"), Path("/run/opengl-driver/share/vulkan/icd.d")]:
            if icd_path.is_dir() and any(icd_path.glob("*.json")):
                has_vulkan_icd = True
                break
        if not has_vulkan_icd:
            messages.append("Nenhum driver Vulkan ICD detectado no sistema. Instale os drivers Vulkan para sua GPU.")
        else:
            messages.append("Nenhuma GPU Vulkan compatível foi encontrada pelo carregador Vulkan.")

    # 2. Check NVIDIA specific issues
    has_nvidia_pci = any(vendor == "NVIDIA" for _, vendor in pci_gpus)
    has_nvidia_vk = any(str(vendor).lower() in ("0x10de", "nvidia") for _, vendor, _, _, _ in vk_gpus)

    if has_nvidia_pci:
        nvidia_ver_file = Path("/proc/driver/nvidia/version")
        if not nvidia_ver_file.is_file():
            messages.append("GPU NVIDIA detectada no barramento PCI, mas o módulo do kernel (nvidia) não está carregado.")
        else:
            try:
                content = nvidia_ver_file.read_text(encoding="utf-8")
                import re
                m = re.search(r"Module\s+(\d+\.\d+)", content)
                if m:
                    ver_float = float(m.group(1))
                    if ver_float < 535.0:
                        messages.append(f"Driver NVIDIA {m.group(1)} é antigo. Recomenda-se driver >= 550 para estabilidade Vulkan.")
            except Exception:
                pass
        if not has_nvidia_vk and nvidia_ver_file.is_file():
            messages.append("Driver NVIDIA carregado, mas a GPU NVIDIA não apareceu na lista de dispositivos Vulkan.")

    # 3. Check AMD specific issues
    for _, vendor, _, name, _ in vk_gpus:
        if str(vendor).lower() in ("0x1002", "amd"):
            name_lower = name.lower()
            if "amdgpu-pro" in name_lower or "amdvlk" in name_lower:
                messages.append(f"GPU AMD usando driver alternativo ({name}). O driver RADV (Mesa) é recomendado para maior estabilidade.")

    return messages


def get_runtime_backend(display=_UNSPECIFIED) -> str:
    """Returns the actual active GDK display backend (e.g. Wayland, X11)."""
    if display is _UNSPECIFIED:
        try:
            import gi
            gi.require_version("Gdk", "4.0")
            from gi.repository import Gdk
            display = Gdk.Display.get_default()
        except Exception:
            return "Unavailable (GDK not loaded)"

    if not display:
        return "None (No display connected)"

    cls_name = display.__class__.__name__
    if "Wayland" in cls_name:
        return "Wayland (native)"
    elif "X11" in cls_name:
        is_xwayland = bool(os.environ.get("WAYLAND_DISPLAY"))
        return "X11 (XWayland)" if is_xwayland else "X11 (native Xorg)"
    elif "Win32" in cls_name:
        return "Windows (Win32 GDK)"
    return cls_name


def collect_diagnostics_report() -> str:
    """Gathers an anonymized, structured diagnostics report."""
    lines = []
    lines.append("======================================================================")
    lines.append("                  BBPort Launcher Diagnostic Report                   ")
    lines.append("======================================================================")

    # 1. System & Python
    lines.append("\n[System & Runtime]")
    lines.append(f"  OS Platform:     {platform.platform()}")
    lines.append(f"  Architecture:    {platform.machine()}")
    lines.append(f"  Python Version:  {sys.version.split()[0]} ({sys.executable})")
    lines.append(f"  Working Dir:     {os.getcwd()}")

    # 2. Desktop & Display
    lines.append("\n[Desktop & Display Session]")
    lines.append(f"  XDG_SESSION_TYPE:    {os.environ.get('XDG_SESSION_TYPE', 'unset')}")
    lines.append(f"  XDG_CURRENT_DESKTOP: {os.environ.get('XDG_CURRENT_DESKTOP', 'unset')}")
    lines.append(f"  XDG_SESSION_DESKTOP: {os.environ.get('XDG_SESSION_DESKTOP', 'unset')}")
    lines.append(f"  DESKTOP_SESSION:     {os.environ.get('DESKTOP_SESSION', 'unset')}")
    lines.append(f"  WAYLAND_DISPLAY:     {os.environ.get('WAYLAND_DISPLAY', 'unset')}")
    lines.append(f"  DISPLAY:             {os.environ.get('DISPLAY', 'unset')}")
    lines.append(f"  GDK_BACKEND (env):   {os.environ.get('GDK_BACKEND', 'default (automatic)')}")
    lines.append(f"  GSK_RENDERER (env):  {os.environ.get('GSK_RENDERER', 'default')}")
    lines.append(f"  Active GDK Backend:  {get_runtime_backend()}")

    # 3. Audio Subsystem
    lines.append("\n[Audio Subsytem]")
    lines.append(f"  Audio Server:        {detect_audio_server()}")

    # 4. GUI Toolkits
    lines.append("\n[GUI Toolkits]")
    try:
        import gi
        gi.require_version("Gtk", "4.0")
        gi.require_version("Adw", "1")
        from gi.repository import Gtk, Adw, GLib
        lines.append(f"  GTK4 Version:        {Gtk.get_major_version()}.{Gtk.get_minor_version()}.{Gtk.get_micro_version()}")
        adw_ver = f"{getattr(Adw, 'get_major_version', lambda: '?')()}.{getattr(Adw, 'get_minor_version', lambda: '?')()}.{getattr(Adw, 'get_micro_version', lambda: '?')()}"
        lines.append(f"  Libadwaita Version:  {adw_ver}")
        lines.append(f"  GLib Version:        {GLib.MAJOR_VERSION}.{GLib.MINOR_VERSION}.{GLib.MICRO_VERSION}")
    except Exception as e:
        lines.append(f"  GTK/Libadwaita:      Error querying versions ({e})")

    # 5. Storage & Paths
    lines.append("\n[Storage & Paths]")
    data_dir = os.environ.get("BB_DATA_DIR")
    if not data_dir:
        data_dir = str(Path(os.environ.get("XDG_DATA_HOME") or (Path.home() / ".local" / "share")) / "bbport")
    lines.append(f"  BB_DATA_DIR:         {data_dir} (exists: {Path(data_dir).is_dir()})")
    log_dir = get_log_dir()
    lines.append(f"  Persistent Logs:     {log_dir or 'Unavailable'} (writable: {Path(log_dir).is_dir() if log_dir else False})")
    log_file = get_log_file()
    lines.append(f"  Log File:            {log_file or 'None'}")

    # 6. Controller Subsytem (SDL3)
    lines.append("\n[Gamepad Subsytem]")
    try:
        from bbport_gamepad import _load_sdl3, _SDL3, get_gamepad_type_label
        sdl = _SDL3 or _load_sdl3()
        if sdl:
            lines.append("  SDL3 Library:        Loaded successfully")
            count = ctypes.c_int(0)
            pads = sdl.SDL_GetGamepads(ctypes.byref(count)) if hasattr(sdl, "SDL_GetGamepads") else None
            lines.append(f"  Connected Gamepads:  {count.value}")
            if count.value > 0 and pads:
                for idx in range(count.value):
                    handle = sdl.SDL_OpenGamepad(pads[idx])
                    if handle:
                        name = sdl.SDL_GetGamepadName(handle)
                        name_str = name.decode("utf-8", "replace") if name else "Gamepad"
                        t_label = get_gamepad_type_label(handle)
                        lines.append(f"    Pad #{idx}:          {name_str} (Type: {t_label})")
        else:
            lines.append("  SDL3 Library:        Not available (Gamepad navigation disabled)")
            lines.append(f"  Install Instruction: {get_sdl3_install_command()}")
    except Exception as e:
        lines.append(f"  SDL3 Library:        Failed to load ({e})")

    # 7. GPUs & Hardware
    lines.append("\n[Graphics & GPUs]")
    pci_devs = detect_pci_gpus()
    if pci_devs:
        pci_str = ", ".join(f"{vendor} ({dev_id})" for dev_id, vendor in pci_devs)
        lines.append(f"  PCI Hardware:        {pci_str}")
    try:
        from bbport_devices import connected_gpus
        gpus = connected_gpus()
        if gpus:
            for item in gpus:
                if isinstance(item, (list, tuple)) and len(item) >= 4:
                    idx, vendor, gtype, name = item[0], item[1], item[2], item[3]
                    lines.append(f"  GPU #{idx}:            {name} ({gtype}, vendor={vendor})")
                elif isinstance(item, dict):
                    lines.append(f"  GPU #{item.get('index', '?')}:            {item.get('name', 'Unknown')} ({item.get('type', 'Unknown')})")
                else:
                    lines.append(f"  GPU:                 {item}")
        else:
            lines.append("  Vulkan GPUs:         None returned by Vulkan loader")
    except Exception as e:
        lines.append(f"  GPU Detection:       Failed to query devices ({e})")

    advisories = check_gpu_diagnostics()
    if advisories:
        lines.append("\n[GPU & Driver Advisories]")
        for adv in advisories:
            lines.append(f"  * {adv}")

    lines.append("\n======================================================================")
    lines.append("                  End of Report (Ready to share)                      ")
    lines.append("======================================================================")
    return "\n".join(lines)
