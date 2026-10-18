# SPDX-License-Identifier: GPL-2.0-or-later
# Dockerfile para compilar o AppImage oficial do BBPort usando Nix
FROM nixos/nix:latest

# Configuração do Nix para execução otimizada dentro do container
RUN mkdir -p /etc/nix && \
    echo "experimental-features = nix-command flakes" >> /etc/nix/nix.conf && \
    echo "filter-syscalls = false" >> /etc/nix/nix.conf && \
    echo "sandbox = false" >> /etc/nix/nix.conf

# Utilitários necessários para os scripts de build e packaging
RUN nix-env -iA nixpkgs.binutils nixpkgs.gawk nixpkgs.bash nixpkgs.coreutils nixpkgs.gnused

WORKDIR /bbport
CMD ["bash", "-c", "nix-shell shell.nix --run 'bash build.sh' && bash packaging/appimage.sh"]
