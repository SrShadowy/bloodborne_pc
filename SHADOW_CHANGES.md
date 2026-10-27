# Shadow Changes — Registro Técnico de Desenvolvimento (Edição Compacta)

Registro técnico consolidado de melhorias arquiteturais, correções de bugs, mitigação de glitches visuais, internacionalização (i18n), integração online e estabilização de simulação do **Bloodborne PC Port (`BBPort`)**.

---

## 1. Resumo Executivo
- **Build & Shaders**: Compilação nativa no Linux (x86_64, Vulkan/Mesa, AMD/NVIDIA), suporte a FSR 3.1 / FSR 4.1.1, Grimório do Caçador em 3 partes e persistência de `VkPipelineCache`.
- **Gráficos & Glitches**: Eliminação de reflexos invertidos no céu, vertex explosions no FaceGen/panos, borrões de motion blur no céu e sincronização de 1D LUT de brilho no FSR 3.1.
- **Pacing, CPU & Modo Foto**: Afinidade de CPU para Ryzen Dual-CCD / Intel P/E-cores, Transparent Huge Pages (2 MiB), priorização de threads no CFS, sleep híbrido 50/50 e Modo Foto com congelamento total via `FD4TaskManager`.
- **Arquitetura `extra/` & Governança**: Modularização estrita em `extra/` com ganchos mínimos `// [extra]` no upstream.
- **Ferramentas & UX**: FPS HUD modular (CPU/GPU/RAM/VRAM), scanner de memória com watchlist, desassemblador x86-64 com símbolos e NOP patching, atalho F9 para liberação de mouse e Discord RPC oficial.
- **Online & Localização**: Suporte a pools BBSC 0.0.2 e The Hunter's Dream (bbhost/Discord) com cliente de relay POSIX Linux nativo, e localização trilíngue (PT-BR, EN, RU).

---

## 2. Histórico de Modificações Anteriores (Mudanças 2 a 90)

- **2. Inverted Reflection Glitch**: Filtro em `vk_rasterizer` descartando planar reflections no céu (`PA_CL_UCP`) e toggle `puddle_reflections` no INI.
- **3. Vertex Explosions & FaceGen**: Padronização de `BB_READBACKS=1` (Relaxed) e `object_motion=0` evitando deformações de malha e deadlocks.
- **4. Internacionalização Modular (i18n)**: Módulo `extra/management/i18n` com enum `StringId`, macro `S(id)` e suporte dinâmico a EN, PT-BR e RU.
- **5. Startup Black Screen Freeze & AvPlayer**: Fix de double-indexing no `AvPlayer`, starvation EOF em `IsActive()` e `skip_intro=1` por padrão.
- **6. Verificação & Diagnósticos**: Testes automatizados contínuos (`build.sh --test`, `pytest`) e integração com launcher GTK4.
- **9. Motion Blur & Velomap no Céu**: Descarte de vetores de velocidade para o domo celeste ($Z \approx 1.0$) em `vk_rasterizer`, eliminando faixas.
- **10. Scanner de Memória In-Game**: Módulo `extra/core/debugger/mem_scanner` com watchlist, freeze em tempo real e persistência JSON.
- **11. Depurador x86-64 & NOP Patching**: Hooking em `mprotect`/`trap`, desassemblador Zydis/LLVM, inspeção de registradores e NOPs reversíveis.
- **12. Hex Inspector & Data Inspector**: Visualizador hexadecimal de 16 bytes por linha, edição direta e inspeção tipada contextual.
- **13. UI/UX, Font Safety & Toasts**: Sistema de notificações toast, fallbacks seguros UTF-8 contra asserts do ImGui e hierarquia docking.
- **14. RADV/AMD GPU Crash (Error 23)**: Clamping de buffers de indirect dispatch antes da submissão à GPU contra perdas de contexto.
- **15. Estabilização de Frametime & Logs**: Otimização no envio de comandos de apresentação e supressão de warnings repetitivos de shader.
- **16. Detecção Dinâmica de Precisão FSR 4.1.1**: Suporte automático a FP8 nativo em AMD RDNA 4 com fallback para FP8 emulado e INT8.
- **17. Teclado e Mouse (KBM) 360°**: Interceptação em `0x143cde0` adicionando delta do mouse aos ângulos de câmera sem emular stick analógico.
- **18. Launcher GTK4 / Libadwaita**: Modularização em `launcher/`, validação de presets de configuração e localização trilíngue.
- **19. FSR 3.1 Frame Generation**: Interpolação de quadros Vulkan via vetores de movimento e buffers de profundidade para 60/120/144 Hz.
- **20. Diretrizes de Arquitetura (`CODE_GUIDELINES.md`)**: Isolamento estrito entre upstream e código customizado em `extra/`.
- **21. Watchdog Timeout & FSR 3.1**: Fences de sincronização no `Presenter` e controle de frame pacing adaptativo contra travamentos do compositor.
- **22. Lamp Teleport Crash & TextureCache**: Referenciamento compartilhado e guarda de retenção de memória no `TextureCache` ao viajar entre lâmpadas.
- **23. Jitter & Tremulação de Imagem**: Desacoplamento da matriz histórica de projeção em `vk_rasterizer.cpp` e sanitização de jitter sub-pixel.
- **24. Estabilização do FPS Counter**: Trava de tamanho e alinhamento do contador in-game evitando oscilações visuais no overlay.
- **25. Detecção FP8 FSR 4.1.1**: Correção de flags de identificação de ponto flutuante de 8 bits em drivers Vulkan recentes.
- **26. Apresentação FSR Upscaled (`RasterScaling`)**: Correção do ciclo de vida das imagens redimensionadas no `Presenter::Present`.
- **27. GPU Indirect Dispatch Sanitizer (`sanitize_indirect.comp`)**: Higienização de argumentos de dispatch indireto via compute shader na GPU.
- **28. Sincronização de Transições de Tela & Morte**: Sincronização segura das transições de cena e tela cheia evitando asserts no swapchain.
- **29. Teclado Virtual Interativo (IME)**: Teclado na tela com suporte a gamepad e mouse para digitação de nomes e diálogos do PS4.
- **30. Debugger SIGTRAP (Exit 133)**: Tratador de sinal corrigido garantindo retorno limpo após execução com trap flags ativas.
- **31. Cache de Shaders & Pipelines**: Otimização de pipeline caches Vulkan em disco, acelerando o boot e mitigando stutters.
- **32. Mesclagem Upstream 0.51 & shadNet**: Atualização para base 0.51 e estruturação inicial dos protocolos de rede online.
- **33. Frame Generation em Menus**: Ajuste no pacer para suspender interpolação desnecessária durante navegação em menus estáticos.
- **34. Eliminação de Overlap no Menu**: Sliders com passos discretos (`Kind::Steps`) eliminando sobreposições na interface.
- **35. Ring Buffer de Cadência (120 FPS)**: Fila circular de 4 slots para entrega suave de 120 FPS interpolados a partir de 60 FPS reais.
- **36. Modularização do Launcher & Limpeza de Cache**: Separação em submódulos Python e botão para expurgo de caches corrompidos.
- **37. Indicador de Compilação de Shaders**: Notificação visual discreta no overlay in-game durante compilações Vulkan assíncronas.
- **38. Limite Nativo de FPS (`fps_limit`)**: Suporte a limitação configurável (30..480 FPS) via `bbport.ini` sem demandar patches manuais.
- **39. FSR 3.1 & Movimento de Objetos**: Estabilização dos vetores de movimento de malhas dinâmicas no passe de upscaling.
- **40. Empacotamento Standalone do Launcher**: Script de empacotamento PyInstaller autossuficiente para distros sem dependências GTK locais.
- **41. Modo Foto & Congelamento da Simulação**: Pausa do mundo via `FD4TaskManager`, supressão do HUD (`hide_hud`) e capturas em alta resolução.
- **42. Fix Crash 23 no Pós-Processamento Yebis**: Correção de assert (`sceKernelDebugRaiseException`) por amostras de blur fora da tela.
- **43. HUD / OSD Informativo Opcional**: Indicador leve no ImGui com taxas de quadros, frametimes e uso de memória.
- **44. Integração Nativa Discord RPC**: Módulo `extra/contact/rpc` exibindo status de jogo, área atual e nível do personagem.
- **45. Otimização de CPU & Render Thread**: Eliminação de spinlocks na sincronização CPU-GPU liberando ciclos para o thread principal.
- **46. Desacoplamento do Discord RPC**: Separação modular da leitura de dados da engine guest e serialização de presença.
- **47. Resolução de Nomes de Áreas**: Mapeamento dinâmico de mapas e localidades baseado em offsets do `CurrentMapList` (`0x553B148`).
- **48. Ponteiro de Tempo de Jogo**: Resolução segura do tempo acumulado em `PlayerGameData` (+0x690) contra estouros de 64 bits.
- **49. Detecção de NPCs e Fantasmas**: Identificação em tempo real de invocações e invasores via lista de entidades do `WorldChrMan`.
- **50. Contador de Mortes Dinâmico**: Persistência de mortes por slot de salvamento, auto-incremento na tela de morte e atalho F10.
- **51. Controles de Privacidade no Discord RPC**: Janela no overlay para ocultar seletivamente nível, área ou detalhes do caçador.
- **52. Prompts Dinâmicos de Botões**: Detecção automática de controles (PlayStation, Xbox, Nintendo Switch, KBM) com texturas adaptadas.
- **54. Arquitetura Modular `extra/` & Hooks**: Isolamento rigoroso do código em pastas de domínio com marcações `// [extra]` no upstream.
- **55. Desassemblador Modernizado & Símbolos**: Catálogo de mais de 45 símbolos nativos da engine, modos de tela cheia e visualizador de memória.
- **56. Suporte à Área de Transferência (Wayland/X11)**: Pipeline multi-camada `SetClipboardSafe` corrigindo cópia de registradores no Linux.
- **57. Correção de Crash no Depurador**: Eliminação de asserts do ImGui em `view_registers.cpp` e reorganização dos controles.
- **58. Multiplayer Online (bbhost / THD)**: Suporte a servidores BBSC e The Hunter's Dream com login via Discord no navegador.
- **59. Reorganização em `extra/hosts/`**: Centralização dos subsistemas de rede com hooks limpos via `extra/hooks.h`.
- **60. Sessão PSN Mockada & Logs**: Mock de tickets de autenticação PSN e matchmaking funcional sem conexão à PSN real.
- **61. Callbacks Assíncronos NP**: Isolamento do runtime de rede prevenindo deadlocks em notificações de estado online.
- **62. Keepalive no Game Server (20443)**: Pacotes periódicos na porta de sinalização evitando quedas repentinas de conexão.
- **63. Fix de Desconexão por Poças de Sangue**: Mock dos endpoints WebAPI de amigos/bloqueios impedindo queda ao ler espectros.
- **64. Empacotamento de `libbbnet.so`**: Resolução de dependências dinâmicas no `package.py` para geração de builds portáveis.
- **65. Resolução RPATH `$ORIGIN` (`libfmt`)**: Inclusão de `$ORIGIN` no binário eliminando conflitos de biblioteca C++ com o sistema operacional.
- **66. Correção de SIGILL no Launcher**: Shims de retrocompatibilidade para distros com bibliotecas GTK4/Adwaita legadas (Ubuntu 22.04).
- **69. Otimizações no Scanner de Memória**: Teto de matches para evitar saturação de RAM e cobertura estendida do heap guest.
- **70. Tecla de Caminhar & Fix de Sinos**: Suporte a tecla dedicada de caminhar suave no teclado e correção de timeouts de sinos online.
- **71. Fix SIGSEGV no Overlay Gráfico & PT-BR**: Realinhamento de efeitos gráficos (adição do Bloom) e guardas defensivas no ImGui.
- **72. Frame Pacing Simétrico 50/50 no Frame Gen**: Sleep híbrido de alta precisão (coarse sleep + spin-pause) eliminando ritmo em dente de serra.
- **73. Upstream Touch Ledger**: Formalização do catálogo de hooks upstream isolados sob marcações `// [extra]`.
- **75. Pulo de Cutscenes & AvPlayer**: Pulo de vídeos pré-renderizados e cenas in-engine com Enter/Esc via `BbVideoTrack` e `bbgpu_video_skip`.
- **76. Empacotamento Online Duplo**: Detecção e inclusão simultânea de `libbbnet.so` e `libbbnet20.so` em pacotes distribuíveis.
- **77. Discord Application ID BBPort**: Migração do Client ID genérico para a aplicação oficial registrada do BBPort (`1560890616811102219`).
- **78-83. Estabilidade, Saves BBSC & Leitura Segura**: `MakeWritable` em copy-sites, persistência AppImage e `extra_safe_read` contra SIGSEGV/SIGBUS.
- **84. Integração BBSC Linux (Teefpyre)**: Leitura direta de parâmetros via `SoloParamRepository` corrigindo queda de FPS em sessões co-op.
- **85-88. Backends de Vídeo & Deduplicação**: Seletor `BB_VIDEO_BACKEND=auto|x11|wayland`, correção Cairo/XWayland e saneamento do `bbport.ini`.
- **89. Touchpad PS4/PS5 & Toggle Giroscópio**: Detecção de clique esquerdo/direito no touchpad e switch para desativar gestos via giroscópio.
- **90. Pin do SDL3 (release-3.4.18)**: Build reproduzível Ubuntu/Nix corrigindo detecção do gamepad Flydigi Apex 5 via dock.

---

## 3. Modificações Recentes

### 91. Sincronização 1:1 de Brilho/Contraste (Display Pass 1D LUT) no Frame Generation
- **Problema**: Flicker severo ao alterar brilho/contraste, pois a calibração de imagem via 1D LUT de 3 camadas (`0x38d65b32`) era aplicada após o FSR 3.1.
- **Solução**: Compute pass `display_lut.comp` em `FrameGeneration::Interpolate`, aplicando a LUT diretamente no frame gerado in-flight com gancho mínimo em `raster_textures.cpp`.

### 92. Instrumentação e Descoberta de Shaders/Pipelines em Runtime
- Mapeamento deduplicado de shaders GCN e pipelines Vulkan registrando hashes, constantes de especialização e métricas de compilação.
- Exportação atômica do inventário para `shader_inventory.json` no encerramento (`Sync`), sem ponteiros efêmeros.

### 93. Grimório do Caçador: Arquivo Otimizado de 3 Partes & Driver VkPipelineCache Persistente
- **Formato Grimório**: Consolidação de milhares de arquivos soltos em 3 arquivos (`hunter_insight.idx`, `caryll_runes.pack`, `blood_echoes.meta`).
- **Driver Cache Vulkan**: Serialização de `pipeline_driver.cache` com validação de UUID de driver/hardware, reduzindo boot morno de ~0.39s para 0.03s (~13x mais rápido).
- **Console & Gestão**: Ring buffer com streaming ao vivo de compilações na aba de Shaders e pré-compilação headless via `--precompile-shaders`.

### 94. Tela de Splash Cinematográfica de Pré-compilação com Progresso em Tempo Real
- Janela SDL3 16:9 independente com arte de fundo, barra de progresso dourada e contador `[ X / 801 ]` em tempo real na primeira execução.
- Orquestração em `run.sh` disparando a tela apenas quando `pipeline_driver.cache` estiver frio no perfil do usuário.

### 95. Shader Discovery Engine — Fase 1 & 2 (Catálogo Estático e Database)
- Extrator autônomo (`extract_static_shaders.py`) descompactando DCX e indexando 4.376 shaders únicos de 64 bits em 0.17s.
- Catálogo em memória C++ (`bbport_static_catalog`) e linha de status na aba de cache do launcher com métricas de cobertura.

### 96. Shader Discovery Engine — Fases 3 a 10 (Contexto, Fila Paralela, Gestão do Grimório)
- Fila de compilação assíncrona desacoplada da thread de renderização e utilitário autônomo `scripts/grimoire_tool.py` (pack/unpack/verify).
- Rotina de auto-restauração `RestoreMasterCacheIfEmpty` mesclando o catálogo mestre de 921+ pipelines diretamente para a pasta do jogador.

### 97. Afinidade de CPU & Agendamento para Ryzen Dual-CCD e Intel P/E-Cores
- **Problema**: Migração caótica de threads entre chiplets (latência L3 saltando de 15ns para 80ns no Infinity Fabric) e uso de 100% de CPU em spinlocks.
- **Solução**: Módulo C11 `bbport_cpu_affinity.h` fixando threads críticas no cluster primário (CCD0 / P-cores) e injeção de instrução x86 `pause` em `thread_yield()`.
- **Resultado**: Estabilização expressiva de frametimes, redução de temperatura e fim de engasgos em processadores híbridos.

### 98. Transparent Huge Pages (MADV_HUGEPAGE) & Mapeamento de Prioridade de Threads
- **Páginas de 2 MiB**: Chamada `madvise(..., MADV_HUGEPAGE)` no pool de memória física direta do jogo, reduzindo a tabela de páginas em 512x e eliminando faltas de TLB.
- **Níveis de Cortesia Linux**: Mapeamento das prioridades nativas para o CFS via `setpriority()` (Main/Render em `nice -5`, Audio em `nice -4`, Background em `nice +4`).
- **Configurabilidade**: Ativo por padrão com controle opcional via `BB_CPU_PRIORITY=1|0`.

### 99. Auditoria de Métricas de Shaders e Classificação Unificada
- Benchmark explícito (`--warm` vs `--cold`) separando tempos de I/O, inicialização Vulkan e compilação real do driver.
- Classificação dos shaders em 4 categorias (`CONFIRMED_MATCH`, `STATIC_ONLY`, `RUNTIME_ONLY`, `STALE_REJECTED`) e bypass lock-free em gameplay.

### 100. Limpeza de Módulos Obsoletos (Purga de Stubs do World Warper e Arsenal Harvester)
- Exclusão definitiva de 4 arquivos stubs em `extra/core/debugger/` (-424 linhas inoperantes) superados pelo Grimório consolidado.
- Limpeza de testes sintéticos mantendo 100% de aprovação na suíte de testes de shaders.

### 101. Empacotamento do Reconstrutor de Shaders em AppImage e Tarball
- Inclusão do binário `pipeline-reconstruct-test` e do Grimório mestre na staging de pacotes do `package.py` com RPATH relativo.
- Suporte a `$APPDIR` e `$BB_PORT_DIR` em `RestoreMasterCacheIfEmpty`, permitindo pré-compilação em pacotes portáveis.

### 102. Resolução da Arte de Fundo da Splash no AppImage
- Inclusão canônica de `splash_bloodborne.png` no empacotamento sem duplicações e resolução de caminhos via `$APPDIR`.
- Apresentação em letterbox 16:9 (`SDL_SetRenderLogicalPresentation`) corrigindo escala em telas HiDPI e Steam Deck.

### 103. Saneamento do Cache para o Cliente e Auto-Purga de Chaves Stale
- **Purga Automática**: `ForEachBlobPath` remove chaves obsoletas/danificadas com conflito de permutação, eliminando mais de 350 warnings no boot.
- **Distribuição Limpa**: Exclusão de JSONs de telemetria/desenvolvedor dos pacotes de usuário, consolidando o Grimório em 623 pipelines limpas a 66.6 µs por pipeline.

### 80. Atualização BBSC 0.0.2 (Seamless Co-op) e Relay P2P em Linux
- **Relay POSIX Linux**: Implementação nativa em Linux do cliente TCP de relay (porta 20444) sem depender da camada Winsock.
- **Dual-Send & Fallback BBR1**: Envio simultâneo inicial (UDP 3478 + direto), fallback para pseudo-endpoints `169.254.x.x` e fila assíncrona de retry de ativação.
- **Sincronização de Pacotes**: Despacho de batimentos `0x23` (vida/morte) e `0x24` (escala) eliminando timeouts de 30s e warnings de rede.

### 104. FPS HUD Modular (CPU/GPU/RAM/VRAM), Presets e Resolução Dinâmica Auto
- **Telemetria Direta**: Leituras sem overhead de CPU/GPU/RAM/VRAM via `/proc/stat`, sysfs DRM (AMD/NVIDIA/Intel) e `/proc/meminfo`.
- **HUD Customizável**: Presets (Padrão, Compacto, Detalhado), opacidade, escala e indicação de resolução interna vs apresentada.
- **Resolução Dinâmica Auto**: Padrão de `live_resolution` ajustado para `-1` (`auto`), ativando otimizações inteligentes conforme a GPU.

### 105. Filtragem de Níveis de Log e Refinamento UX no Launcher
- Seletor de 5 níveis de severidade (`minimal`, `error`, `warning`, `verbose`, `none`) com filtragem de streams em tempo real.
- Tooltips contextuais nas opções gráficas do inicializador Libadwaita e suíte de testes unitários automatizada.

### 106. Toggle de Fallback do Relay P2P (BBR1) no Launcher
- Opção visual na aba online do inicializador para desativar o relay UDP e forçar conexões P2P puras durante testes de rede.
- Persistência em `settings.json`/`bbport.ini` e exportação automática de `SHADPS4_P2P_RELAY=0`.

### 107. Liberação e Captura do Mouse com Atalho F9 & Auto-Release em Menus
- **Auto-Release em Menus**: Desativação automática do confinamento do cursor (`SDL_SetWindowMouseGrab`) ao abrir qualquer menu, inventário ou overlay.
- **Atalho F9**: Liberação ou captura manual sob demanda no meio da gameplay com banner toast de confirmação.
- **Respeito ao Switch**: Se `mouse_capture` estiver desativado na configuração, o cursor nunca é confinado pela janela.

### 108. Automação de Versão Incremental (`x.x.d-b`) e Proteção de Versão do AppImage
- **Formato Diário**: Script `scripts/bump_version.py` gerenciando versão Incremento coordenado em `build.sh` e `package.py` com prevenção de bump duplo via `BB_SKIP_VERSION_INC`.

### 109. Gerenciamento Inteligente de VRAM e Módulo `extra/memoryDev`
- **Módulo `extra/memoryDev`**: Criação de camada arquitetural isolada com suporte a predefinições de gerenciamento de memória e VRAM (Alta, Média, Baixa).
- **Perfis de VRAM**:
  - *Alta (Padrão)*: 100% fiel ao comportamento atual para placas com 12+ GB de VRAM.
  - *Média (6–8 GB)*: Despejo moderado para RAM e ciclo de Garbage Collection de texturas balanceado (10s).
  - *Baixa (4 GB / APU)*: Despejo agressivo de texturas inativas de volta para a RAM do sistema (GC em 5s), teto de VRAM em 3.2 GB e prioridade de retenção mínima para evitar crashes de out-of-device-memory.
- **Integração no Launcher**: Seção dedicada na aba *Desenvolvimento* com persistência no `settings.json`, exportação de `BB_MEMORY_PRESET` e suporte trilíngue completo (PT-BR, EN, RU).
- **Telemetria de VRAM do Jogo no HUD**: Correção da leitura de VRAM no overlay in-game para inspecionar `/proc/self/fdinfo` e os alocadores Vulkan nativos do processo. Exibição da VRAM exclusiva do jogo com o total da GPU entre parênteses (`VRAM: X.XX GB (Tot: Y.YY)`), eliminando confusão com a memória alocada por outros aplicativos do sistema operacional.

### 110. Auto Mip-Clamp, VRAM Limit de 2GB e Otimizações de 30 FPS para APU
- **Variável `bb_vram_limit`**: Suporte configurável via `bbport.ini` ou variáveis de ambiente `BB_VRAM_LIMIT` / `BB_VRAM_LIMIT_MB` com parsing flexível de sufixos (`2GB`, `2G`, `2048MB`, `2048`, `1536MB`).
- **Ajuste Fino de Orçamento em `UpdateMemoryBudget()` e `GarbageCollectImages()`**:
  - `trigger_gc_memory` travado em **~1000 MB** (`budget / 2` ou metade do limite definido), iniciando a reciclagem e descarte antecipado de texturas ociosas para a memória RAM.
  - `pressure_gc_memory` em **~1300 MB** (`budget * 65 / 100`).
  - `critical_gc_memory` travado em **~1600 MB** (`budget * 80 / 100`), impedindo estouros de memória em placas de 2 GB / 4 GB.
- **Auto Mip-Clamp com Preservação de UI e Player**:
  - Salto de Mip 0 (`baseMipLevel = 1`, `levelCount = levels - 1`) na criação da `ImageView` para texturas pesadas do mundo e cenário, reduzindo em ~75% o tráfego de leitura do barramento DDR5.
  - **Preservação de UI**: Texturas de nível único (`levels <= 1`), ícones de botões (`prompt_icon`, `prompt_atlas`), formatos não-comprimidos (fontes, menus), render targets e chamadas durante a fase de UI (`InUiPhase()`) mantêm o Mip 0 nativo intacto.
  - **Preservação do Player / Caçador**: Detecção em tempo de execução de arquivos `chr/c0000` (corpo do caçador) e `parts/` (armas, vestimentas) em `runtime_file.c`, bem como malhas vinculadas com buffers de ossos (`BufferRole::Skeleton`/`SmallSkeleton`). As texturas do caçador permanecem em resolução total.
- **Perfil APU / 2GB no Launcher e run.sh**:
  - Nova opção dedicada `"APU / 2 ГБ (Встроенная графика / 30 FPS)"` / `"APU / 2 GB (Gráficos Integrados / 30 FPS)"`.
  - Configura automaticamente: `BB_VRAM_LIMIT_MB=2048`, `BB_AUTO_MIP_CLAMP=1`, reciclagem de GC em 4s/2s.

### 111. Diagnóstico e Resolução do Gargalo de 11 FPS na APU Radeon 610M (2 CUs)
- **Diagnóstico do Gargalo (Screenshot do Usuário)**:
  - O uso de memória estava perfeito (**489 MB de VRAM** e **527 MB de RAM** com Auto Mip-Clamp ativo e Caçador/UI nítidos).
  - A APU (AMD Radeon 610M / Raphael) possui apenas **2 Compute Units** (128 ALUs / shaders).
  - O framerate caiu para 11 FPS porque os shaders estavam 100% sobrecarregados por:
    1. **FSR 3.1 Frame Generation ativo (`frame_generation=1`)**: A interpolação e cálculo de optical flow gastavam mais de 20 ms de computação por quadro em apenas 2 CUs, cortando o framerate base pela metade.
    2. **Screen-Space Reflections ativo (`effect_ssr=1` e patch no `Bloodborne.xml`)**: O raymarching de poças e superfícies molhadas em Yharnam não existia no jogo original de PS4 e destruía o desempenho da APU.
    3. **Resolução de Renderização Interna em 720p (preset 1 - Quality)**: Shading de 921.600 pixels por quadro era excessivo para 2 CUs.
    4. **Efeitos Pesados de Pós-Processamento**: SSAO, Depth of Field, Motion Blur, Bloom (escala 1.40), distância de sombras em 1.09 e LOD de modelos em 0 (nível máximo de polígonos distantes).
- **Ações e Correções Implementadas**:
  - **Desativação Total de Frame Generation na APU**: `frame_generation=0` (interpolação requer base alta e não deve rodar em iGPUs de 2 CUs).
  - **Desativação de SSR e SSAO**: `effect_ssr=0` no ini, patch de SSR desativado em `Bloodborne.xml` e `effect_ssao=0`.
  - **Preset FSR Performance (preset 3 - 540p interno)**: Redução de 44% no número de pixels renderizados (518.400 pixels), liberando os 128 shaders para alcançar 30 FPS estáveis com upscaling para 1080p.
  - **Otimizações Geométricas e de Iluminação**: `model_lod=2` (menor malha distante), `shadow_distance=0.40` e `light_grid_opt=1` (redução de draw calls de grid de luz).
  - **Trava de 30 FPS**: `fps_limit=30` e `BB_FPS=30` para consistência de frametime e ritmo de quadros.
  - **Automação no Launcher e run.sh**: Quando `memory_preset=apu` é selecionado, `run.sh` e `bbport_config.py` exportam automaticamente essas configurações otimizadas.

### 112. Predefinição Gráfica Interativa no Launcher (Estilo Jogos de PC)
- **Grupo de Predefinição Gráfica e de Memória na Aba Gráficos**: Novo grupo de controle no topo da aba Gráficos do Launcher (sincronizado bidirecionalmente com a aba Desenvolvimento).
- **Ajuste Visual dos Controles em Tempo Real**: Selecionar uma predefinição altera instantaneamente todas as opções visíveis na interface do Launcher:
  - *Alta (8+ GB / Recomendada)*: FSR Quality (1), LOD Normal (0), SSAO/DoF/Blur/Bloom ativos, FPS sem limite (uncap / 0), Preupload padrão (sem sobrecarga de 3 GB extra para caber perfeitamente em 8 GB).
  - *Média (6 GB / Equilibrada)*: FSR Balanced (2), LOD Normal (0), SSAO/DoF/Blur/Bloom ativos, trava em 60 FPS, Preupload padrão, liberação balanceada de VRAM (GC a 5000 MB).
  - *Baixa*: FSR Performance (3), LOD Lower (1), efeitos pesados desligados, trava em 60 FPS, Preupload desligado (0).
  - *APU / 2 GB*: FSR Performance 540p (3), Frame Generation desligado, LOD Mínimo (2), SSAO/DoF/Blur/Bloom/SSR desligados, trava em 30 FPS, Preupload 0 e render pipeline híbrido.
- **Persistência Imediata**: Gravação sincronizada em `settings.json` e `bbport.ini` com banner toast de confirmação da predefinição aplicada.

### 113. Controles de Ajustes de Desempenho para APU e GPUs Fracas na Aba Desenvolvimento
- **Novo Grupo na Aba Desenvolvimento**: `"Ajustes de Desempenho para APU e GPUs de Entrada"` (`apu_tuning`).
- **Controles Adicionados**:
  1. **Sombras de Luzes Dinâmicas (`effect_dynamic_shadows`)**: Permite desligar passes pesados de sombra projetados por tochas e lanternas, economizando dezenas de draw calls (sincronizado com a aba Gráficos e com o patch em `Bloodborne.xml`).
  2. **Distância das Sombras (`shadow_distance`)**: SpinRow com alcance de 0.25 a 3.00 (padrão 0.25 no perfil APU), reduzindo a área de cálculo de sombras ao redor do jogador.
  3. **Opacidade da Névoa Volumétrica (`fog_opacity`)**: SpinRow com alcance de 0.00 a 2.00 (padrão 0.30 no perfil APU), diminuindo intensamente o custo do raymarching da névoa no ar.
  4. **Otimização da Grade de Luzes (`light_grid_opt`)**: SwitchRow para particionamento eficiente do grid de iluminação.
  5. **Escala de Resolução Interna Rápida (`preset`)**: Seletor rápido direto na aba Desenvolvimento permitindo alternar facilmente para Ultra Desempenho (360p / x3.0), Performance (540p / x2.0), Balanced, Quality e Native (sincronizado com o seletor da aba Gráficos).
- **Traduções Globais**: Chaves adicionadas aos dicionários do `bbport_i18n.py` (PT-BR, PT, EN, ZH-CN).


