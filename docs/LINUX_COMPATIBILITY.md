# Guia de Compatibilidade, Diagnóstico e Dependências — Linux

Este documento detalha os requisitos, arquitetura de compatibilidade, mapeamento de dependências por distribuição e procedimentos de diagnóstico para o launcher do **Bloodborne PC (BBPort)** no Linux.

---

## 1. Requisitos de Sistema

| Componente | Requisito Mínimo | Recomendado | Finalidade |
| :--- | :--- | :--- | :--- |
| **Python** | 3.9+ | 3.12+ | Runtime do launcher e scripts de suporte |
| **Interface Gráfica** | GTK 4.6+ / Libadwaita 1.2+ | GTK 4.14+ / Libadwaita 1.5+ | Interface moderna nativa com suporte a temas |
| **Controles / Gamepad** | SDL 3.0+ (`libSDL3.so.0`) | SDL 3.2+ | Suporte a gamepads modernos, hotplug e testes visuais |
| **Gráficos / Vulkan** | Vulkan 1.3 | Vulkan 1.3 (RADV ou NVIDIA >= 550) | Backend de renderização do jogo e upscaling FSR |
| **Áudio** | ALSA | PipeWire / PulseAudio | Saída de som com baixa latência |

---

## 2. Dependências por Distribuição

O launcher utiliza a infraestrutura nativa do sistema operacional host para máxima performance e integração visual com o desktop.

### Debian / Ubuntu / Linux Mint / Pop!_OS
```bash
sudo apt update
sudo apt install -y \
    python3 \
    python3-gi \
    python3-gi-cairo \
    gir1.2-gtk-4.0 \
    gir1.2-adw-1 \
    libsdl3-0 \
    mesa-vulkan-drivers \
    vulkan-tools
```

### Arch Linux / CachyOS / Manjaro
```bash
sudo pacman -S --needed \
    python \
    python-gobject \
    gtk4 \
    libadwaita \
    sdl3 \
    vulkan-icd-loader \
    vulkan-tools
```
*Para drivers Vulkan específicos:*
- **AMD:** `vulkan-radeon` (driver RADV recomendado)
- **NVIDIA:** `nvidia-utils` (driver >= 550)
- **Intel:** `vulkan-intel`

### Fedora / RHEL / Nobara / Bazzite
```bash
sudo dnf install -y \
    python3 \
    python3-gobject \
    gtk4 \
    libadwaita \
    SDL3 \
    mesa-vulkan-drivers \
    vulkan-tools
```

### Nix / NixOS
O repositório inclui a derivação [packaging/default.nix](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/packaging/default.nix), que empacota automaticamente todas as bibliotecas necessárias, arquivos `.desktop` e ícones.

---

## 3. Sessões Gráficas e Desktops Suportados

O launcher negocia automaticamente com o display server ativo, sem forçar variáveis de ambiente globais:

| Ambiente | Sessão | Status | Observações |
| :--- | :--- | :--- | :--- |
| **KDE Plasma** | Wayland | Suportado (Nativo) | Detecção de DPI e esquema de cores automático |
| **KDE Plasma** | X11 | Suportado | Funciona nativamente ou com `--x11` |
| **GNOME** | Wayland | Suportado (Nativo) | Integração perfeita com Libadwaita |
| **GNOME** | X11 | Suportado | Negociação direta com Xorg |
| **Hyprland / Sway** | Wayland (wlroots) | Suportado | Suporte a Wayland nativo sem dependência de XWayland |
| **XFCE / MATE / Cinnamon** | X11 | Suportado | Fallback seguro para tema e ícones simbólicos Adwaita |
| **Headless / SSH** | Sem display | Protegido | Emite código `[E-DISP-001]` e instrui uso de `--play` |

---

## 4. Linha de Comando e Opções (CLI)

O launcher oferece opções de diagnóstico e controle via terminal:

| Opção | Descrição |
| :--- | :--- |
| `--help`, `-h` | Exibe a ajuda com todas as opções disponíveis e encerra. |
| `--version`, `-v` | Exibe a versão atual do BBPort Launcher e encerra. |
| `--diagnostics` | Imprime relatório técnico detalhado e redigido do sistema (GPU, Vulkan, áudio, display, logs). |
| `--debug`, `--verbose` | Ativa logging detalhado (`DEBUG`) no console e nos arquivos de log. |
| `--wayland` | Força a inicialização com o backend Wayland nativo. |
| `--x11` | Força a inicialização com o backend X11/XWayland. |
| `--play` | Inicia o jogo diretamente em modo headless (ideal para Steam Deck / Game Mode). |
| `--build-fsr411 <dll>` | Executa a compilação de shaders FSR 4.1.1 em segundo plano sem abrir janela. |

---

## 5. Sistema de Logs e Diagnóstico

### Localização dos Arquivos
Os logs seguem a especificação **XDG Base Directory**:
- **Pasta de logs:** `$XDG_STATE_HOME/bbport/logs` (ou `~/.local/state/bbport/logs`)
- **Arquivo principal:** `launcher.log`
- **Arquivo de crashes nativos:** `fatal-crash.log` (gerado pelo módulo `faulthandler` do Python)

### Rotação e Retenção
- O arquivo `launcher.log` rotaciona automaticamente ao atingir **2 MB**, mantendo até **4 backups** históricos (`launcher.log.1` a `launcher.log.4`).
- Nunca cresce indefinidamente nem ocupa espaço excessivo em disco.

### Ativação de Modo Debug
Para capturar logs mais profundos durante investigação de problemas:
```bash
LAUNCHER_DEBUG=1 ./Bloodborne-bbport-native-x86_64.AppImage
# ou via flag CLI:
./Bloodborne-bbport-native-x86_64.AppImage --debug
```

### Códigos de Erro Padronizados
- `[E-DISP-001]`: Nenhum display gráfico ativo detectado (`WAYLAND_DISPLAY` e `DISPLAY` ausentes).
- `[E-GTK-001]`: Falha na inicialização do GTK 4 ou Libadwaita (bibliotecas ou typelibs ausentes).
- `[E-GAME-001]`: Executável `eboot.bin` não encontrado no diretório do jogo especificado.
- `[W-SDL-001]`: Biblioteca SDL3 indisponível; a interface abre normalmente, mas o suporte a gamepad fica desativado.

---

## 6. Configurações e Persistência Segura

- **Diretório:** `$XDG_CONFIG_HOME/bbport-launcher` (ou `~/.config/bbport-launcher`)
- **Arquivo:** `settings.json`
- **Gravação Atômica:** Gravações utilizam arquivo temporário (`.settings.json.tmp.<pid>`), `fsync` e substituição atômica, além de manter um backup prévio (`settings.json.bak`) para proteção contra corrupção em desligamentos repentinos.
- **Permissões:** O arquivo é mantido com permissão estrita `0600` (leitura e escrita apenas pelo usuário).
