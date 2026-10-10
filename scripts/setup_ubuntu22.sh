#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Instala todas as dependências necessárias para compilar o BBPort no Ubuntu 22.04 LTS
set -euo pipefail

echo "======================================================================"
echo "    BBPort - Instalador de Dependências para Ubuntu 22.04 LTS"
echo "======================================================================"

SUDO=""
if [[ $EUID -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "[!] Este script precisa de privilégios root para instalar pacotes." >&2
        exit 1
    fi
fi

# 1. Repositórios e dependências básicas
echo -e "\n[*] [1/6] Instalando pacotes básicos e repositório de toolchains..."
$SUDO apt-get update
$SUDO apt-get install -y software-properties-common ca-certificates curl wget git pkg-config python3 python3-pip ninja-build

$SUDO add-apt-repository -y ppa:ubuntu-toolchain-r/test
$SUDO apt-get update

# 2. GCC 13 e bibliotecas de desenvolvimento do sistema
echo -e "\n[*] [2/6] Instalando GCC 13 (C++23) e bibliotecas de desenvolvimento..."
$SUDO apt-get install -y gcc-13 g++-13 \
    libboost-all-dev libxxhash-dev \
    libavcodec-dev libavformat-dev libswscale-dev libswresample-dev libavutil-dev \
    libx11-dev libxcb1-dev libwayland-dev libxext-dev \
    libzstd-dev libgl1-mesa-dev libvulkan-dev glslang-tools

# Configura gcc-13 e g++-13 como padrão do sistema
$SUDO update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 --slave /usr/bin/g++ g++ /usr/bin/g++-13
$SUDO update-alternatives --set gcc /usr/bin/gcc-13

# 3. CMake moderno (>= 3.24)
echo -e "\n[*] [3/6] Instalando CMake moderno..."
pip3 install --no-cache-dir cmake

# 4. SDL3 (compilação a partir da fonte oficial)
echo -e "\n[*] [4/6] Compilando e instalando SDL3..."
rm -rf /tmp/sdl3
git clone --depth 1 https://github.com/libsdl-org/SDL.git -b main /tmp/sdl3
cmake -S /tmp/sdl3 -B /tmp/sdl3/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build /tmp/sdl3/build -j"$(nproc)"
$SUDO cmake --build /tmp/sdl3/build --target install
rm -rf /tmp/sdl3

# 5. fmt 10, Zydis e miniz
echo -e "\n[*] [5/6] Compilando e instalando fmt, Zydis e miniz..."
rm -rf /tmp/fmt /tmp/zydis /tmp/miniz

git clone --depth 1 --branch 10.2.1 https://github.com/fmtlib/fmt.git /tmp/fmt
cmake -S /tmp/fmt -B /tmp/fmt/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DFMT_TEST=OFF
cmake --build /tmp/fmt/build -j"$(nproc)"
$SUDO cmake --build /tmp/fmt/build --target install
rm -rf /tmp/fmt

git clone --depth 1 --recursive https://github.com/zyantific/zydis.git /tmp/zydis
cmake -S /tmp/zydis -B /tmp/zydis/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DZYDIS_BUILD_SHARED_LIB=ON
cmake --build /tmp/zydis/build -j"$(nproc)"
$SUDO cmake --build /tmp/zydis/build --target install
rm -rf /tmp/zydis

git clone --depth 1 --branch 3.0.2 https://github.com/richgel999/miniz.git /tmp/miniz
cmake -S /tmp/miniz -B /tmp/miniz/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local -DBUILD_SHARED_LIBS=ON
cmake --build /tmp/miniz/build -j"$(nproc)"
$SUDO cmake --build /tmp/miniz/build --target install
rm -rf /tmp/miniz

# 6. Bibliotecas header-only: magic_enum, tsl-robin-map, VulkanMemoryAllocator
echo -e "\n[*] [6/6] Instalando magic_enum, tsl-robin-map e VulkanMemoryAllocator..."
rm -rf /tmp/magic_enum /tmp/robin_map /tmp/vma

git clone --depth 1 --branch v0.9.5 https://github.com/Neargye/magic_enum.git /tmp/magic_enum
cmake -S /tmp/magic_enum -B /tmp/magic_enum/build -DCMAKE_INSTALL_PREFIX=/usr/local
$SUDO cmake --build /tmp/magic_enum/build --target install
rm -rf /tmp/magic_enum

git clone --depth 1 --branch v1.3.0 https://github.com/Tessil/robin-map.git /tmp/robin_map
cmake -S /tmp/robin_map -B /tmp/robin_map/build -DCMAKE_INSTALL_PREFIX=/usr/local
$SUDO cmake --build /tmp/robin_map/build --target install
rm -rf /tmp/robin_map

git clone --depth 1 --branch v3.1.0 https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git /tmp/vma
cmake -S /tmp/vma -B /tmp/vma/build -DCMAKE_INSTALL_PREFIX=/usr/local
$SUDO cmake --build /tmp/vma/build --target install
rm -rf /tmp/vma

$SUDO ldconfig

echo -e "\n======================================================================"
echo "    TODAS AS DEPENDÊNCIAS FORAM INSTALADAS COM SUCESSO!"
echo "    Agora você pode rodar: python3 package.py"
echo "======================================================================"
