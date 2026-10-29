# SPDX-FileCopyrightText: Copyright 2026 bbport contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Repository paths for the Python tests (run: python3 -m unittest discover -s tests)."""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT / 'scripts') not in sys.path:
    sys.path.insert(0, str(ROOT / 'scripts'))

# Ensure native test binaries can load bundled dependencies (e.g. libfmt, libminiz, libZydis)
staging_libs = sorted((ROOT / "out" / "package_staging").glob("*/lib"))
if staging_libs:
    lib_dir = str(staging_libs[-1])
    cur_ld = os.environ.get("LD_LIBRARY_PATH", "")
    if lib_dir not in cur_ld:
        os.environ["LD_LIBRARY_PATH"] = f"{lib_dir}:{cur_ld}" if cur_ld else lib_dir
