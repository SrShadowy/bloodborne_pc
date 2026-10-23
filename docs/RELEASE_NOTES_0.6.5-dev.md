# 🩸 Bloodborne PC Port (BBPort) — Pre-Release `v0.6.5-dev-24`

> ⚠️ **Build de Desenvolvimento & Testes Públicos (Dev Preview)**  
> Esta compilação destina-se a testes de compatibilidade, estabilidade do launcher nativo, novos recursos de controle e diagnósticos no Linux.

---

## 🚀 Destaques Principais

- ⚡ **AppImage Nativo Ultraleve (-80% de tamanho):** Substituição completa do antigo empacotador pesado pelo runtime nativo. O tamanho do AppImage caiu de **88 MB** para apenas **17.7 MB**, e o tempo de abertura foi reduzido para ~2 segundos com cache de bytecode XDG.
- 🖥️ **Compatibilidade Universal Wayland & X11:** Negociação dinâmica de backend gráfico sem poluir o sistema. Suporte de primeira classe para **KDE Plasma**, **GNOME**, **Hyprland/Sway** e **XFCE/MATE**, com fallback automático para X11/XWayland se o compositor Wayland não responder.
- 🎮 **Nova Ferramenta Visual de Teste de Controles:** Interface dedicada em tempo real na aba *Controles* com chips luminosos para todos os 15 botões, indicadores de eixos analógicos e gatilhos (L2/R2), detecção de modelo (PlayStation DualSense, Xbox, Switch, Deck) e suporte a hotplug.
- 🔍 **Subsistema de Diagnóstico & Observabilidade:** Novo gerador de relatórios técnicos redigidos (`--diagnostics`), logs rotativos seguros em `$XDG_STATE_HOME/bbport/logs/` e captura antecipada de crashes nativos via `faulthandler`.

---

## 📋 Registro Detalhado de Alterações (Changelog)

### 🎛️ Launcher & Interface (GTK 4 / Libadwaita)
- **Correção Crítica de Inicialização:** Resolvido o erro de ciclo de vida do GObject (`LauncherWindow is not initialized`) ao restaurar e posicionar a janela.
- **Tema & Acessibilidade:** Suporte ao `Adw.StyleManager` com valor padrão `"system"`, sincronizando automaticamente com o modo claro/escuro do desktop e respeitando configurações de alto contraste.
- **Geometria Responsiva:** Persistência segura de largura, altura e estado maximizado com *clamping* automático para telas menores (ex.: **Steam Deck** 1280x800 ou laptops 1366x768).
- **Configuração Atômica e Resiliente:** Gravação de `settings.json` com permissão estrita `0600`, gravação temporária com `fsync` e backup automático (`settings.json.bak`) para recuperação contra desligamentos abruptos.
- **Verificador de Atualizações Assíncrono:** Notificação sutil via Toast para novas releases do BBPort sem bloquear o carregamento e sem autoatualizações invasivas.
- **Localização Completa:** Interface disponível em **Português do Brasil**, **Inglês**, **Russo** e **Chinês Simplificado**.

### 🎮 Controles & Áudio (SDL3 / PipeWire / PulseAudio)
- **Isolamento de Falhas (Degradação Segura):** A ausência ou falha da biblioteca SDL3 não derruba o launcher; a interface abre normalmente com aviso amigável.
- **Mapeamentos Comunitários:** Carregamento automático de arquivos `gamecontrollerdb.txt` presentes na pasta do jogo ou do usuário.
- **Detecção de Steam Input:** Exibe alerta na interface quando o Steam Input está ativo para evitar sobreposição ou duplo mapeamento de botões.
- **Identificação do Servidor de Áudio:** Detecção e logging automático no boot para **PipeWire**, **PulseAudio** e **ALSA**.

### 🖥️ Gráficos, Vulkan & Hardware
- **Detecção PCI & Vulkan:** Identificação direta das GPUs via barramento PCI (`/sys/bus/pci/devices`) e dispositivos Vulkan físicos.
- **Priorização Inteligente em Híbridas:** Em notebooks com gráficos híbridos (APU + dGPU), prioriza a GPU dedicada por padrão.
- **Diagnóstico de Drivers:**
  - Alerta caso placas AMD estejam usando drivers proprietários alternativos (AMDGPU-PRO/AMDVLK) e recomenda o driver open-source **RADV (Mesa)**.
  - Verificação de versão do driver NVIDIA com alerta caso a versão seja inferior a 535 ou o módulo do kernel `nvidia` não esteja carregado.
  - Alerta claro se nenhum driver Vulkan ICD for encontrado no sistema.

### 🛡️ Preflight Checks & Execução do Jogo
- **Validação Pré-Lançamento:** Checagem prévia da presença do `eboot.bin` e permissões de escrita antes de chamar o processo.
- **Tratamento de Crashes e Sinais:** Captura clara de sinais de terminação (`SIGSEGV` código 11, `SIGABRT` código 6) com indicação direta da localização do log para depuração.
- **Preflight de Dependências (`[E-GTK-001]`):** Se o GTK 4 ou Libadwaita estiverem ausentes no host, o launcher exibe os comandos exatos de instalação para **Ubuntu/Debian**, **Arch/CachyOS** e **Fedora** sem tracebacks confusos.  
  *(Testado e validado em **Arch Linux / CachyOS (KDE Plasma Wayland)** e **Fedora 44**)*.
- **Proteção Headless (`[E-DISP-001]`):** Se executado em terminal sem display gráfico, instrui o uso de `--play` ou `--diagnostics`.

### 📦 Empacotamento, CLI & Distribuição
- **Opções CLI Adicionadas:**
  - `--diagnostics`: Exibe relatório completo de hardware, display, Vulkan, áudio e dependências.
  - `--debug` / `--verbose`: Ativa modo de log detalhado (`LAUNCHER_DEBUG=1`).
  - `--wayland` / `--x11`: Força a escolha do backend de exibição.
  - `--play`: Execução direta sem interface gráfica (ideal para Steam Deck / Big Picture).
  - `--version`, `-v` e `--help`, `-h`: Informações de versão e uso.
- **Pacote Nix:** Corrigida instalação de arquivos `.desktop` e ícones no derivation `packaging/default.nix`.
- **Documentação:** Criado o guia completo de dependências por família de distribuição em [`docs/LINUX_COMPATIBILITY.md`](docs/LINUX_COMPATIBILITY.md).

---

## 📥 Como Executar

### Opção 1: Via AppImage Nativo (Recomendado)
```bash
# 1. Torne o arquivo executável:
chmod +x Bloodborne-bbport-native-x86_64.AppImage

# 2. Execute o launcher:
./Bloodborne-bbport-native-x86_64.AppImage

# Ou inicie o jogo direto no Steam Deck (Game Mode):
./Bloodborne-bbport-native-x86_64.AppImage --play
```

### Opção 2: Via Pacote Tarball (.tar.gz)
```bash
tar -xzf BBPort-0.6.5-dev-24-linux-x86_64.tar.gz
cd BBPort-0.6.5-dev-24-linux-x86_64
./start.sh
```

---

## ⚖️ Qual versão baixar? Entenda as diferenças

Para atender aos diferentes perfis de usuários e ambientes Linux, o BBPort é disponibilizado em três formatos principais:

| Característica | 🚀 AppImage Nativo (Recomendado) | 📦 Arquivo Tarball (`.tar.gz`) | 🐘 AppImage Grande (Nix) |
| :--- | :--- | :--- | :--- |
| **Tamanho do Download** | **~17 MB - 18 MB** (Ultraleve) | **~18 MB** | **~400 MB a 1 GB+** (Pesado) |
| **Formato de Entrega** | Executável único portátil (`.AppImage`) | Pasta compactada extraível | Executável único portátil (`.AppImage`) |
| **Dependências de Sistema** | Usa GTK4/Python do sistema host | Usa GTK4/Python do sistema host | **Autossuficiente** (Traz tudo embutido) |
| **Tempo de Abertura** | Quase instantâneo (~2s) | Instantâneo | Mais lento (descompacta store Nix) |
| **Integração Visual** | Perfeita (temas KDE/GNOME nativos) | Perfeita (temas do sistema) | Pode divergir do tema do sistema |
| **Ideal para...** | Maioria dos usuários (Arch, Fedora, Ubuntu) | Steam Deck (Non-Steam Game) e customização | Distros antigas/mínimas sem GTK4 |

### Detalhamento dos Formatos:

1. **🚀 AppImage Nativo (`Bloodborne-bbport-native-x86_64.AppImage`):**
   - **O que é:** O melhor equilíbrio entre portabilidade e leveza. Empacota os binários do jogo, bibliotecas essenciais portáteis (como SDL3, xxhash, fmt) e shaders em um arquivo único de clique duplo.
   - **Vantagem:** Download minúsculo (17 MB), abre em 2 segundos e se integra de forma nativa aos temas escuro/claro e ao compositor Wayland da sua distro.
   - **Requisito:** Requer que o sistema tenha Python 3 e GTK 4 instalados (já presentes por padrão na grande maioria das distros modernas). Caso falte algo, o launcher informa o comando exato para instalar.

2. **📦 Tarball Portátil (`BBPort-*-linux-x86_64.tar.gz`):**
   - **O que é:** O pacote clássico em formato de pasta extraível contendo todos os binários, ferramentas e scripts de execução (`start.sh`, `run.sh`).
   - **Vantagem:** Perfeito para adicionar como *"Jogo não-Steam"* no SteamOS (Steam Deck), permitindo apontar diretamente para os scripts ou inspecionar/modificar arquivos de configuração localmente.

3. **🐘 AppImage Grande via Nix (`Bloodborne-bbport-nix-x86_64.AppImage`):**
   - **O que é:** Um pacote completamente hermético gerado pelo ecossistema Nix. Traz dentro de si uma árvore inteira do `/nix/store` com seu próprio Python, GTK 4, Libadwaita, GObject Introspection e todas as bibliotecas C/C++.
   - **Vantagem:** Funciona em absolutamente qualquer distribuição Linux (mesmo distros muito antigas ou minimalistas corporativas) sem requerer a instalação de nenhum pacote no sistema.
   - **Desvantagem:** Download consideravelmente mais pesado e inicialização mais lenta.

---

## 🐛 Como Reportar Problemas
Se encontrar qualquer problema durante os testes, anexe o relatório técnico gerado por:
```bash
./Bloodborne-bbport-native-x86_64.AppImage --diagnostics
```
Ou envie o arquivo de log persistente localizado em:
`~/.local/state/bbport/logs/launcher.log`
