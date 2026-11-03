#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# [extra] Recursively collects all transitive DLL dependencies for Windows binaries.
import os
import shutil
import subprocess
import sys
from pathlib import Path

def collect_dependencies(bin_dir, search_dir):
    bin_dir = Path(bin_dir)
    search_dir = Path(search_dir)
    
    system_dlls = {
        "advapi32.dll", "kernel32.dll", "shell32.dll", "user32.dll", "winmm.dll",
        "ws2_32.dll", "bcrypt.dll", "crypt32.dll", "d3d11.dll", "d3d12.dll",
        "dxgi.dll", "gdi32.dll", "imm32.dll", "ole32.dll", "oleaut32.dll",
        "setupapi.dll", "version.dll", "shlwapi.dll", "uxtheme.dll",
        "cfgmgr32.dll", "comdlg32.dll", "dinput8.dll", "dwmapi.dll", "mpr.dll",
        "netapi32.dll", "psapi.dll", "userenv.dll", "wldap32.dll", "dnsapi.dll",
        "iphlpapi.dll", "secur32.dll", "vulkan-1.dll", "dwrite.dll", "msimg32.dll",
        "rpcrt4.dll", "usp10.dll", "wsock32.dll", "bcryptprimitives.dll",
        "gdiplus.dll", "ncrypt.dll", "ntdll.dll"
    }
    
    objdump = shutil.which("llvm-objdump") or shutil.which("objdump") or "/clang64/bin/llvm-objdump.exe"
    
    def get_imported_dlls(filepath):
        try:
            out = subprocess.check_output(
                [str(objdump), "-p", str(filepath)],
                text=True, stderr=subprocess.DEVNULL
            )
        except Exception:
            return []
        dlls = []
        for line in out.splitlines():
            if "DLL Name:" in line:
                dlls.append(line.split("DLL Name:")[1].strip())
        return dlls

    queue = [f for f in bin_dir.glob("*.exe")] + [f for f in bin_dir.glob("*.dll")]
    seen = {f.name.lower() for f in queue}
    count = 0

    while queue:
        current = queue.pop(0)
        for dll in get_imported_dlls(current):
            dll_lower = dll.lower()
            if dll_lower.startswith("api-ms-win") or dll_lower.startswith("ext-ms-win"):
                continue
            if dll_lower in system_dlls or dll_lower in seen:
                continue
            seen.add(dll_lower)
            
            src = search_dir / dll
            if not src.is_file():
                for f in search_dir.glob("*.dll"):
                    if f.name.lower() == dll_lower:
                        src = f
                        break
            
            if src.is_file():
                dst = bin_dir / src.name
                if not dst.is_file():
                    shutil.copy2(src, dst)
                    count += 1
                queue.append(src)

    # Ensure vulkan-1.dll is NEVER in bin_dir
    (bin_dir / "vulkan-1.dll").unlink(missing_ok=True)
    print(f"Collected {count} transitive DLLs into {bin_dir}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: collect_dlls.py <bin_dir> <search_dir>")
        sys.exit(1)
    collect_dependencies(sys.argv[1], sys.argv[2])
