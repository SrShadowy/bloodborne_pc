#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Gera o AppImage Nativo ultraleve do BBPort (~60-80 MB) em poucos segundos.
#
# Uso:
#   bash packaging/appimage-native.sh [--no-build] [--docker]
#
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "======================================================================"
echo "    BBPort - Gerador de AppImage Nativo Ultraleve"
echo "======================================================================"

python3 package.py --appimage "$@"
