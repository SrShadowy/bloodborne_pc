#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Compila e gera o AppImage oficial do BBPort utilizando um container Docker com Nix.
# O resultado é idêntico ao build oficial para Linux e Steam Deck.
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_NAME="bbport-builder:nix"
VOLUME_NAME="bbport-nix-store"
HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

echo "======================================================================"
echo "    BBPort - Docker Nix AppImage Build (Oficial)"
echo "======================================================================"

echo -e "\n[*] [1/2] Construindo/Verificando imagem Docker: $IMAGE_NAME..."
docker build -t "$IMAGE_NAME" -f "$ROOT/packaging/Dockerfile.nix" "$ROOT"

# Se houver um CMakeCache gerado fora do ambiente Nix (ex: pelo host ou container Ubuntu), limpa-o
if [[ -f "$ROOT/out/gpu/CMakeCache.txt" ]] && ! grep -q "/nix/store" "$ROOT/out/gpu/CMakeCache.txt" 2>/dev/null; then
    echo "[*] Limpando build do GPU incompatível com Nix..."
    rm -rf "$ROOT/out/gpu"
fi

echo -e "\n[*] [2/2] Compilando e empacotando AppImage com Nix dentro do container..."
docker run --rm \
    -v "$VOLUME_NAME:/nix" \
    -v "$ROOT:/bbport" \
    -w /bbport \
    -e HOST_UID="$HOST_UID" \
    -e HOST_GID="$HOST_GID" \
    "$IMAGE_NAME" \
    bash -c '
        set -euo pipefail
        trap "chown -R \"$HOST_UID:$HOST_GID\" dist out packaging/runtime-paths.nix 2>/dev/null || true" EXIT
        git config --global --add safe.directory /bbport 2>/dev/null || true
        git config --global --add safe.directory /bbport/gpu/third_party/fsr-vulkan 2>/dev/null || true
        command -v sed >/dev/null || nix-env -iA nixpkgs.gnused
        command -v readelf >/dev/null || nix-env -iA nixpkgs.binutils

        echo ">>> [Passo 1/2] Compilando binarios do jogo no nix-shell..."
        nix-shell shell.nix --run "bash build.sh"

        echo ">>> [Passo 2/2] Empacotando AppImage com ralismark/nix-appimage..."
        bash packaging/appimage.sh

        echo ">>> Ajustando permissoes dos arquivos gerados..."
        chown -R "$HOST_UID:$HOST_GID" dist out packaging/runtime-paths.nix 2>/dev/null || true
    '

echo -e "\n======================================================================"
echo "    APPIMAGE GERADO COM SUCESSO!"
if [[ -f "$ROOT/dist/Bloodborne-bbport-x86_64.AppImage" ]]; then
    ls -lh "$ROOT/dist/Bloodborne-bbport-x86_64.AppImage"
fi
echo "======================================================================"
