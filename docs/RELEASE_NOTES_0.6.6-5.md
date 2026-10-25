# 🩸 Bloodborne PC Port (BBPort) — Pre-Release `v0.6.6-5`

> ⚠️ **Build de Desenvolvimento & Testes Públicos (Dev Preview — v0.6.6-5)**  
> Esta compilação consolida o **Grimório do Caçador (Hunter's Grimoire)** com pré-compilação de shaders offline, suporte completo e resiliente ao **BBSC Relay em Linux**, novo **FPS HUD com telemetria de hardware (CPU/GPU/RAM/VRAM)**, **filtragem de níveis de log**, **resolução dinâmica inteligente (`live_resolution=auto`)** e refinamentos de usabilidade e interface.

---

## 🚀 Destaques Principais

- 🗡️ **Grimório do Caçador & Tela de Splash Cinematográfica:** Reconstrução offline de pipelines Vulkan via `pipeline-reconstruct-test`, eliminando completamente os travamentos de compilação (*shader compilation stutter*) durante o gameplay. Inclui uma tela de splash cinematográfica em SDL3 com barra de progresso visual em tempo real e arte oficial Full HD em letterbox 16:9 (`packaging/splash_bloodborne.png`), acionável via launcher ou pela flag CLI `--precompile-shaders`.
- 🌐 **BBSC Seamless Co-op & Relay Nativo em Linux (POSIX):** Cliente de relay TCP/UDP totalmente portado para Linux (porta 20444) sem qualquer dependência do Windows Winsock. Adicionado mecanismo de fallback automático para o relay BBR1 (`169.254.x.x:3173`) e fila de retry de ativação assíncrona, eliminando a queda por timeout de 30 segundos em redes com CGNAT. Sincronização dos batimentos de vida/morte (`0x23` `BbscLife` a cada 500 ms) e escala (`0x24` `BbscScale`), silenciando warnings repetitivos no terminal.
- 📊 **Novo FPS HUD Modular com Telemetria de Hardware (CPU, GPU, RAM, VRAM):** Telemetria em tempo real nativa no Linux (`bbport_perf_metrics`). Controles customizáveis no overlay (<kbd>Insert</kbd> ou <kbd>L3+R3</kbd>), incluindo presets (*Padrão*, *Compacto*, *Detalhado*), gráfico de frametime, escala de fonte e exibição da resolução interna renderizada vs resolução final de tela com upscaler (`%dx%d < %dx%d`).
- 🔇 **Controle de Níveis de Log (Logging Levels) no Launcher e Runner:** Novo seletor de verbosidade no launcher (`Minimal`, `Error`, `Warning`, `Verbose`, `None`), suprimindo mensagens redundantes em tempo de execução, aliviando o uso de I/O em disco e mantendo o terminal limpo.
- 🎯 **Resolução Dinâmica Padrão em Auto (`live_resolution=-1`):** A configuração de resolução dinâmica agora vem por padrão em `"auto"` (`-1`), ativando aceleração otimizada automaticamente de acordo com as capacidades da GPU (dGPUs modernas usam o caminho acelerado, enquanto Steam Deck e GPUs integradas mantêm máxima estabilidade).
- 🧹 **Purga Automática de Cache de Pipelines & Limpeza de Pacotes:** Auto-pruning de chaves corrompidas ou obsoletas (`ForEachBlobPath`), resultando em 623 pipelines mestre 100% verificadas, 0 warnings de colisão de permutação no boot e latência média de pré-carregamento de apenas 66.6 µs por pipeline.
- 🎛️ **Refinamento de UX com Tooltips em Configurações:** Dicas contextuais interativas em cada `Adw.ComboRow` detalhando cada parâmetro gráfico e de sistema no launcher GTK4 / Libadwaita.
- ⚡ **AppImage Nativo Ultraleve (-80% de tamanho):** Mantido o runtime nativo ultraleve (~17 MB base / ~44 MB completo com caches e splash Full HD), abrindo em ~2 segundos com cache de bytecode XDG.
- 💡 **Diagnóstico e Estabilização do Flickering (Display Pass):** Filtro de brilho desacoplado na etapa de apresentação, estabilizando a iluminação com upscalers temporais (FSR/TAA).
- 🎮 **Nova Ferramenta Visual de Teste de Controles:** Interface dedicada em tempo real na aba *Controles* com chips luminosos para todos os 15 botões, indicadores de eixos analógicos e gatilhos (L2/R2), detecção de modelo (PlayStation DualSense, Xbox, Switch, Deck) e suporte a hotplug.

---

## 📋 Registro Detalhado de Alterações (Changelog)

### 🗡️ Shaders, Cache & Grimório do Caçador
- **Pré-compilador Offline (`pipeline-reconstruct-test`):** Integrado ao `build.sh` e empacotado no AppImage/Tarball via `package.py` com RPATH relativo. Permite reconstruir e aquecer todo o acervo de pipelines gráficas antes do boot da emulação.
- **Tela de Splash Cinematográfica (`extra/ui/splash/bbport_shader_splash.cpp`):** Interface visual em SDL3 renderizando barra de progresso em tempo real sobre a arte de Bloodborne. Resolução proporcional e independente de DPI via `SDL_SetRenderLogicalPresentation` em letterbox 16:9, com busca canônica da imagem através de `$APPDIR`, `$BB_PORT_DIR` ou caminho relativo.
- **Purga Automática de Chaves Stale (`gpu/render/blob_store.cpp`):** Função `ForEachBlobPath` com exclusão automática (`std::filesystem::remove`) de pipelines corrompidas ou em conflito de permutação, evitando poluição de logs e falhas de pré-carregamento.
- **Proteção de Cache do Usuário:** Isolamento de dumps de telemetria e inventário (`shader_inventory.json`, `shader_diagnostic_report.json`), gerados apenas quando explicitamente requisitados via variáveis de ambiente (`BB_SHADER_DISCOVERY_DUMP=1`, `BB_SHADER_REPORT=1`).
- **Trigger no Launcher & CLI:** Opção de disparar a pré-compilação diretamente pela interface do launcher ou via linha de comando com `--precompile-shaders` / `./run.sh --precompile-shaders`.

### 🌐 Multiplayer Online & BBSC (Seamless Co-op)
- **Cliente BBSC Relay Nativo em Linux (`gpu/bbnet/` e `gpu/bbnet20/`):** Implementação completa em C++ usando POSIX sockets (`bbsc_relay_client.cpp`), conectando à porta 20444 do servidor de relay com reconexão periódica, gerenciamento assíncrono de threads e tratamento resiliente de erros.
- **Mecanismo de Fallback para Relay UDP (BBR1):** Em `np_matching2_mm.cpp`, fallback automático para alocação de pseudo-endpoints (`169.254.x.x:3173`) roteados pelo relay BBR1 quando o broker shadNet expira ou o par ainda não registrou IP público direto.
- **Fila de Retry de Ativação:** Fila assíncrona (`PendingActivation retry`) em `np_signaling_state.cpp`, eliminando o erro de *timeout* aos 30 segundos ("peer endpoint unresolved; connection timeout").
- **Pacotes 0x23 (`BbscLife`) e 0x24 (`BbscScale`):** Despacho dos batimentos cardíacos de vida/morte (500 ms) e escala dos jogadores em `np_signaling_helpers.cpp` e `signaling_handler.cpp`, direcionando-os a `OnBbscLifePacket` e `OnBbscScalePacket`. Corrige o warning recorrente `unexpected SHAD packet type=35 size=14`.
- **Supressão de Ruído de Rede:** Ajuste de logs de chamadas assíncronas normais bem-sucedidas (`result = 0x0` em `sceNpWaitAsync`, `sceNpPollAsync`, `sceNpAuthWaitAsync`, `sceNpAuthPollAsync`) e `sceNetEpollControl` de `WARNING` para `DEBUG`.
- **Title Menu Scaleform Hook:** Injeção de linhas extras no menu principal para controle e opções integradas do BBSC Seamless Co-op (`bbport_title_menu.{h,cpp}`).
- **Toggle de Fallback do Relay P2P (BBR1) no Launcher:** Adicionada opção configurável na aba de rede/online permitindo desativar o relay UDP de fallback (`p2p_relay=0` / `SHADPS4_P2P_RELAY=0`) para testes e depuração de conexões P2P puras sem interferência de servidores intermediários.

### 📊 HUD de Performance, Métricas de Hardware & Resolução
- **Métricas de Hardware em Tempo Real (`extra/management/hud/bbport_perf_metrics.cpp`):** Leitura de estatísticas de CPU (total e por núcleo via `/proc/stat`), uso de GPU (sysfs DRM/hwmon AMD/NVIDIA/Intel), consumo de RAM (`/proc/meminfo`) e VRAM.
- **Configuração Completa do FPS HUD (`extra/management/hud/bbport_fps_hud.cpp`):**
  - Toggles individuais para FPS, Frametime, CPU, GPU, RAM, VRAM e gráfico de frametime.
  - Seleção de layout e presets visuais: *Padrão*, *Compacto* e *Detalhado*.
  - Configuração de posição nos 4 cantos da tela, escala de fonte (`ImGui::SetWindowFontScale`) e opacidade de fundo.
  - Exibição dinâmica da resolução de renderização interna versus resolução de saída com upscaling (`1920x1080 < 1280x720`).
- **Painel de Controle no Overlay In-Game:** Nova seção no overlay (<kbd>Insert</kbd> ou <kbd>L3+R3</kbd>) em `extra/management/sections/bbport_overlay_sections.cpp` permitindo ligar e desligar métricas e presets dinamicamente durante o jogo.
- **Resolução Dinâmica Auto:** Padrão de `live_resolution` configurado para `"auto"` (`-1`), adaptando-se automaticamente à GPU do usuário.

### 🎛️ Launcher, Configuração & Logging
- **Níveis de Log Configuráveis:** Nova opção de verbosidade (`Minimal`, `Error`, `Warning`, `Verbose`, `None`) no launcher (`launcher/bbport_config.py`, `launcher/bbport_launcher.py`), com filtragem ativa no `bbport_game_runner.py` para reduzir I/O e ruído no terminal.
- **Tooltips Explicativos em `ComboRow`:** Suporte a dicas contextuais em cada opção de configuração para facilitar o entendimento de parâmetros gráficos e de sistema.
- **Bateria de Testes Automatizados:** Adicionados testes cobrindo o gerenciamento de logs, configurações do HUD e componentes da UI em `tests/test_launcher_dev.py` e `tests/test_launcher_log.py`.
- **Geometria Responsiva & Configuração Atômica:** Persistência segura de largura, altura e estado maximizado com *clamping* automático para telas menores (Steam Deck), gravação atômica de `settings.json` com `fsync` e backup `.bak`.
- **Localização Completa:** Interface disponível em Português do Brasil, Inglês, Russo e Chinês Simplificado.

### 🖥️ Gráficos, Vulkan & Pipeline de Renderização
- **Descoberta e Isolamento do Flickering:** Isolado o comportamento que causava variações abruptas de intensidade luminosa entre frames devido ao filtro de brilho injetado tardiamente durante o **Display Pass**, permitindo a sincronização 1:1 de brilho e contraste com FSR/TAA.
- **Detecção PCI & Vulkan:** Identificação direta das GPUs via barramento PCI (`/sys/bus/pci/devices`) e dispositivos Vulkan físicos.
- **Priorização Inteligente em Híbridas:** Em notebooks com gráficos híbridos (APU + dGPU), prioriza a GPU dedicada por padrão.
- **Diagnóstico de Drivers:**
  - Alerta caso placas AMD estejam usando drivers proprietários alternativos (AMDGPU-PRO/AMDVLK) e recomenda o driver open-source **RADV (Mesa)**.
  - Verificação de versão do driver NVIDIA com alerta caso a versão seja inferior a 535 ou o módulo do kernel `nvidia` não esteja carregado.
  - Alerta claro se nenhum driver Vulkan ICD for encontrado no sistema.

### 🎮 Controles & Áudio (SDL3 / PipeWire / PulseAudio)
- **Isolamento de Falhas (Degradação Segura):** A ausência ou falha da biblioteca SDL3 não derruba o launcher; a interface abre normalmente com aviso amigável.
- **Mapeamentos Comunitários:** Carregamento automático de arquivos `gamecontrollerdb.txt` presentes na pasta do jogo ou do usuário.
- **Detecção de Steam Input:** Exibe alerta na interface quando o Steam Input está ativo para evitar sobreposição ou duplo mapeamento de botões.
- **Identificação do Servidor de Áudio:** Detecção e logging automático no boot para **PipeWire**, **PulseAudio** e **ALSA**.

### 🛡️ Preflight Checks & Execução do Jogo
- **Validação Pré-Lançamento:** Checagem prévia da presença do `eboot.bin` e permissões de escrita antes de chamar o processo.
- **Tratamento de Crashes e Sinais:** Captura clara de sinais de terminação (`SIGSEGV` código 11, `SIGABRT` código 6) com indicação direta da localização do log para depuração.
- **Preflight de Dependências (`[E-GTK-001]`):** Se o GTK 4 ou Libadwaita estiverem ausentes no host, o launcher exibe os comandos exatos de instalação para **Ubuntu/Debian**, **Arch/CachyOS** e **Fedora** sem tracebacks confusos.
- **Proteção Headless (`[E-DISP-001]`):** Se executado em terminal sem display gráfico, instrui o uso de `--play` ou `--diagnostics`.

### 📦 Empacotamento, CLI & Distribuição
- **Opções CLI Adicionadas:**
  - `--precompile-shaders`: Executa a rotina de pré-compilação do Grimório com a tela de splash SDL3 e sai.
  - `--diagnostics`: Exibe relatório completo de hardware, display, Vulkan, áudio e dependências.
  - `--debug` / `--verbose`: Ativa modo de log detalhado (`LAUNCHER_DEBUG=1`).
  - `--wayland` / `--x11`: Força a escolha do backend de exibição.
  - `--play`: Execução direta sem interface gráfica (ideal para Steam Deck / Big Picture).
  - `--version`, `-v` e `--help`, `-h`: Informações de versão e uso.

---

## 📥 Como Executar

### Opção 1: Via AppImage Nativo (Recomendado)
```bash
# 1. Torne o arquivo executável:
chmod +x Bloodborne-bbport-native-x86_64.AppImage

# 2. Executar o launcher com a interface gráfica:
./Bloodborne-bbport-native-x86_64.AppImage

# Ou pré-compilar os shaders com a tela de splash:
./Bloodborne-bbport-native-x86_64.AppImage --precompile-shaders

# Ou iniciar o jogo direto no Steam Deck (Game Mode):
./Bloodborne-bbport-native-x86_64.AppImage --play
```

### Opção 2: Via Pacote Tarball (.tar.gz)
```bash
tar -xzf BBPort-0.6.6-5-linux-x86_64.tar.gz
cd BBPort-0.6.6-5-linux-x86_64
./start.sh
```

---

## ⚖️ Qual versão baixar? Entenda as diferenças

Para atender aos diferentes perfis de usuários e ambientes Linux, o BBPort é disponibilizado em formatos adaptados:

| Característica | 🚀 AppImage Nativo (Recomendado) | 📦 Arquivo Tarball (`.tar.gz`) | 🐘 AppImage Grande (Nix) |
| :--- | :--- | :--- | :--- |
| **Tamanho do Download** | **~44 MB** (Com shaders/splash) | **~44 MB** | **~400 MB a 1 GB+** (Pesado) |
| **Formato de Entrega** | Executável único portátil (`.AppImage`) | Pasta compactada extraível | Executável único portátil (`.AppImage`) |
| **Dependências de Sistema** | Usa GTK4/Python do sistema host | Usa GTK4/Python do sistema host | **Autossuficiente** (Traz tudo embutido) |
| **Tempo de Abertura** | Quase instantâneo (~2s) | Instantâneo | Mais lento (descompacta store Nix) |
| **Integração Visual** | Perfeita (temas KDE/GNOME nativos) | Perfeita (temas do sistema) | Pode divergir do tema do sistema |
| **Ideal para...** | Maioria dos usuários (Arch, Fedora, Ubuntu) | Steam Deck (Non-Steam Game) e customização | Distros antigas/mínimas sem GTK4 |

---

## 🐛 Como Reportar Problemas
Se encontrar qualquer problema durante os testes, anexe o relatório técnico gerado por:
```bash
./Bloodborne-bbport-native-x86_64.AppImage --diagnostics
```
Ou envie o arquivo de log persistente localizado em:
`~/.local/state/bbport/logs/launcher.log`
