#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Compila e empacota o BBPort dentro de um container Ubuntu 22.04 LTS (glibc 2.35)
# Isso garante compatibilidade máxima com quase todas as distribuições Linux modernas.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_NAME="bbport-builder:ubuntu22"

echo "======================================================================"
echo "    BBPort - Docker Build (Ubuntu 22.04 LTS / glibc 2.35)"
echo "======================================================================"

echo -e "\n[*] [1/2] Construindo/Verificando imagem Docker: $IMAGE_NAME..."
docker build -t "$IMAGE_NAME" -f "$ROOT/packaging/Dockerfile.ubuntu22" "$ROOT"

# Se houver arquivos do CMake criados por root ou fora do container Ubuntu, limpa-os
if [[ -d "$ROOT/out/gpu/CMakeFiles" && ! -w "$ROOT/out/gpu/CMakeFiles" ]]; then
    docker run --rm -v "$ROOT:/bbport" "$IMAGE_NAME" rm -rf /bbport/out/gpu
    mkdir -p "$ROOT/out/gpu"
fi
if [[ -f "$ROOT/out/gpu/CMakeCache.txt" ]]; then
    if grep -q "/nix" "$ROOT/out/gpu/CMakeCache.txt" 2>/dev/null || ! grep -q "gcc-13" "$ROOT/out/gpu/CMakeCache.txt" 2>/dev/null; then
        rm -rf "$ROOT/out/gpu"
        mkdir -p "$ROOT/out/gpu"
    fi
fi

echo -e "\n[*] [2/2] Compilando e gerando release dentro do container..."
docker run --rm \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e APPIMAGE_EXTRACT_AND_RUN=1 \
    -e BB_SKIP_VERSION_INC="${BB_SKIP_VERSION_INC:-}" \
    -v "$ROOT:/bbport" \
    -w /bbport \
    "$IMAGE_NAME" \
    bash -c "git config --global --add safe.directory /bbport 2>/dev/null || true; bash build.sh && python3 package.py --no-build \"\$@\"" -- "$@"

echo -e "\n======================================================================"
echo "    BUILD CONCLUÍDO COM SUCESSO!"
echo "    Os binários compatíveis e o pacote .tar.gz estão em: dist/"
echo "======================================================================"
