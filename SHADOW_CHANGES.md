# Shadow Changes — Registro Técnico de Desenvolvimento (Edição Compacta)

Registro técnico consolidado de melhorias arquiteturais, correções de bugs, mitigação de glitches visuais, internacionalização (i18n), integração online e estabilização de simulação do **Bloodborne PC Port (`BBPort`)**.

---

## 1. Resumo Executivo
- **Build & Shaders**: Compilação nativa no Linux (x86_64, Vulkan/Mesa, AMD/NVIDIA), suporte a FSR 3.1 / FSR 4.1.1, Grimório do Caçador em 3 partes e persistência de `VkPipelineCache`.
- **Gráficos & Glitches**: Eliminação de reflexos invertidos no céu, vertex explosions no FaceGen/panos, borrões de motion blur no céu e sincronização de 1D LUT de brilho no FSR 3.1.
- **Pacing, CPU & Modo Foto**: Afinidade de CPU para Ryzen Dual-CCD / Intel P/E-cores, Transparent Huge Pages (2 MiB), priorização de threads no CFS, espaçamento uniforme dos quadros gerados com `Common::SleepUntil` + spin de 200 µs e Modo Foto com congelamento total via `FD4TaskManager`.
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
- **33. Frame Generation em Menus**: Ajuste no pacer de apresentação (removido depois, ver 122) para suspender interpolação desnecessária durante navegação em menus estáticos — **histórico**: o caminho atual não tem essa suspensão.
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
- **72. Frame Pacing no Frame Gen**: Era sleep híbrido de alta precisão (coarse sleep + spin-pause) dentro do `PresentPacer` — **histórico**: esse módulo foi removido em 122. O espaçamento atual mora em `Presenter::Present` (ver 122).
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

### 114. F0 — Persistência Real do VkPipelineCache do Driver e Grimório `.bmeta`
- **Causa raiz medida**: `LoadDriverCache()`/`SaveDriverCache()` existiam em `extra/` mas nenhum caminho do jogo os chamava — todo boot recompilava ~550 pipelines. O `pipeline-reconstruct-test` gravava um cache de driver vazio (36 bytes, só header) e o gate `! -f` do `run.sh` fechava o splash de pré-compilação para sempre.
- **Runtime persistente (`extra/core/shader_precompile/bbport_vk_pipeline_cache.{h,cpp}`)**:
  - `Create(device, physical)`: semeia o `VkPipelineCache` com o blob salvo (validado por vendor/device/UUID); se o driver rejeitar o blob (corrupção), recria vazio e segue — corromper o arquivo nunca quebra o boot; aborta apenas se o create vazio falhar (paridade com o `ASSERT_MSG` antigo).
  - `PipelineCreateGuard` (RAII): lock compartilhado + contador de creates em cada `vkCreate*Pipelines` (compilações paralelas preservadas). O flush pega o lock exclusivo (`std::shared_mutex`): `vkGetPipelineCacheData` exige external sync contra creates concorrentes — selo hermético válido em qualquer driver, presente rodar em thread separada ou não.
  - `FlushTick()` (1× por frame em `Presenter::Present`): salva no primeiro "lull" (≥2 s sem create novo, nunca no meio do storm de boot) com backoff ≥5 s (também após falha de disco); não salva com `BB_SHADER_COLD_RUN`. O jogo termina via `_exit` (sem destrutores), então o flush periódico é o único mecanismo de persistência — perde no máximo os compiles desde o último flush (recompilados no boot seguinte, apenas mais lentos).
- **Grimório `blood_echoes.meta` → `blood_echoes.bmeta`**: tira o blob de ~400 KB do glob `*.meta` do `BlobStore::ForEachBlob(ShaderMeta)` (lido/parseado todo boot). `MigrateArchiveNames()` renomeia in-place no boot (chamado do `Create`, antes de qualquer scan de preload); `RestoreMasterCacheIfEmpty` mapeia o nome antigo do mestre para o novo e aceita `.bmeta` na lista de extensões; `PackDirectory` remove o legado após o pack; `scripts/grimoire_tool.py` atualizado.
- **Hooks upstream**: `pipeline_cache.cpp` (ctor: `Create` no lugar do create vazio, região de 2 linhas + include), `pipeline.cpp` (2 guards de 1 linha + include), `presenter.cpp` (`FlushTick()`, 1 linha), `run.sh` (gate por tamanho: cache <4 KiB = frio, o splash volta a rodar até o jogo gravar o blob real).
- **Ferramenta**: `extra/tools/audit.sh` (novo) — auditoria rápida do AGENTS.md (marcadores `[extra]`, limites de linhas em `extra/`, no máx. 1 `extra/hooks.h` por arquivo). As violações que ele reporta (bbport_strings.cpp 729 linhas, overlay 1086, etc.) são dívida pré-existente, não deste change.
- **Correção de build do alvo de teste (exceção documentada)**: `tests/test_gpu_runtime_stubs.cpp` ganhou 3 stubs — `runtime_low_map`, `runtime_memory_commit`, `runtime_savedata_active_slot` — imports que o upstream 0.7 adicionou (`gpu/shim/bbport_map_near.h`, `gpu/games/bloodborne/hooks_heap.cpp`, `gpu/games/bloodborne_kit_restore.cpp`, commit `0fc2921`) sem atualizar os stubs, quebrando o link de `pipeline-reconstruct-test` (quebra pré-existente do alvo, não do F0; todo caller já trata o retorno de falha: `BbMapNear::Map` → nullptr, `HeapGrowCommitAll`/`HeapGrowAhead` → skip, `ActiveSave` → false). Região de 5 linhas (> 3) marcada `// [extra]`; documentada aqui como exceção do orçamento.
- **Limitações conhecidas**: `BlobStore::Clear()` (rebuild de versão) apaga `pipeline_driver.cache` e o Grimório — aceito: o flush regrava no boot seguinte; `Sync()` do reconstruct-test mantém o save direto sem lock (offline, sem concorrência).

### 115. F3 — Métricas de Criação de Pipeline (hit-rate + tempo via `VK_*_pipeline_creation_feedback`)
- **Motivo**: o F0 provou não-regressão, mas nada instrumentava ganho: nenhum número de tempo em `vkCreate*Pipelines` nem de quantos pipelines saíram do blob. (A/B já era possível — `BB_SHADER_COLD_RUN=1` desliga load/save/FlushTick isoladamente, único leitor no runtime — mas sem métrica não havia o que comparar.)
- **Novo `extra/core/shader_precompile/bbport_vk_pipeline_stats.{h,cpp}`** (um assunto por arquivo): contadores atômicos de sessão (`creates/hits/feedback_valid/compile_ns/longest_ns`); `HookDeviceExtension(has, enable)` habilita `VK_KHR|EXT_pipeline_creation_feedback` no `Instance::CreateDevice` **só com `BB_PIPELINE_STATS=1`** — sem a var o device fica byte-idêntico ao de antes (zero risco default; headers do build antecedem a promoção KHR, o nome KHR vai como literal, o EXT via macro); `AttachForCreate` encadeia a feedback struct no `pNext` do create-info (rendering info/subgroup ficam encadeados abaixo, nunca clobbered) e liga o ponteiro `pPipelineCreationFeedback` na struct de dados que o guard possui (os headers do build expõem o layout promovido: o relatório do driver é uma struct separada atrás desse ponteiro); é no-op desarmado (pNext sem a extensão habilitada seria VUID violation); `RecordCreate` usa a duração reportada pelo driver (`eValid`) + o flag `eApplicationPipelineCacheHit` (hit no blob semeado), com wall-time como fallback.
- **`PipelineCreateGuard` ganha overloads** (`create_guard{pipeline_ci}` nos 2 sites): as mesmas 2 linhas do F0 agora cronometram o escopo (chrono no ctor/dtor, custo ~ns contra compilações de ms) e encadeiam o feedback; dtor registra tempo + hit.
- **`FlushTick`**: além do save, 1 linha de métricas por lull (`session: creates=N hits=H valid=V compile_time=T longest=L ms`); o braço cold (`BB_SHADER_COLD_RUN`) imprime também (sem salvar) — esse par é a medição A/B; anti-spam: `s_stats_line_creates` (uma linha por lote novo, nunca por frame).
- **Hooks upstream**: `gpu/vulkan/instance.cpp` (include + 1 linha `HookDeviceExtension` — único ponto que habilita a extensão), `gpu/render/pipeline.cpp` (as 2 linhas de guard ganham o argumento + `const` removido das 2 declarações locais do `pipeline_ci`: objetos stack-local que o upstream nunca modifica, e a mutabilidade é o que permite encadear o feedback no `pNext` — 4 linhas marcadas no total). `bb_env_vars.def` + `docs/ENVIRONMENT.md`: entrada `BB_PIPELINE_STATS`.
- **Teste**: report ganha "Pipeline Creation Stats" + self-test offline da contabilidade (flags fake → contadores reais: valid+hit com duração do driver não pode cair no wall-time; chaining de `pNext` verificado) — os números reais de hit-rate só existem in-game (o teste não drena compilações).
- **Protocolo A/B in-game**: mesma build, dois boots com `BB_PIPELINE_STATS=1`: braço frio `BB_SHADER_COLD_RUN=1` vs braço warm (blob ≥4 KiB) — comparar `compile_time` e `hits/creates`; para isolar o F0 da cache em disco do Mesa, apagar `~/.cache/mesa_shader_cache*` nos dois braços.

### 116. Gates de QA Executáveis (`qa_runner.sh`) e Ratchet de Tamanho no `audit.sh`
- **Causa raiz**: o §2 do AGENTS.md mandava rodar `extra/tools/qa_runner.sh` após cada mudança, mas o script nunca existiu — a bateria era refeita à mão a cada sessão. E o `audit.sh` saía 1 **para sempre** por 14 arquivos de dívida pré-existente (>300 linhas, ex.: `bbport_overlay_sections.cpp` com 1086), saturando o exit code: um gate que nunca passa não detecta violação nova. Quebrar os 9 arquivos >400 à força só pelo limite seria churn de risco contra ganho estético — Ponytail proíbe.
- **`extra/tools/qa_runner.sh` (novo)**: 3 estágios — [1] `audit.sh`; [2] build **no host** (`bash build.sh --no-bump && ninja -C out/gpu pipeline-reconstruct-test`, com o mesmo fallback `nix-shell` da L133 do `run.sh` quando faltam pacotes); [3] smoke real: 2 corridas warm do reconstruct-test num sandbox descartável (`$BB_QA_DIR`, default `/tmp/opencode/qa_sandbox`) conferindo exit 0, self-test do creation feedback (`accounting=OK pNext-chaining=OK`) e round-trip byte-idêntico do blob de driver (a rede de regressão do F0), + 1 corrida armada (`BB_PIPELINE_STATS=1`; "[feedback armed]" é driver-dependente, só informativo). Build falho ⇒ smoke vira SKIP (rodar o binário stale daria PASS falso). PASS/SKIP/FAIL por estágio, exit 1 se algo falhar. Validação visual de frame segue manual (precisa do eboot).
- **Ratchet com baseline embutido no `audit.sh`**: a lista de dívida (14 violadores com tamanho atual) vive **dentro do próprio script** — `extra/tools/` não é versionado e o arquivo auxiliar original (`audit_baseline.txt`) **sumiu duas vezes** entre sessões (causa não identificada; como a lista só muda por edição deliberada, embutir elimina a classe do problema). Arquivo **novo** >400 linhas ou violador que **cresceu** = FAIL; dívida estável ou encolhendo = `DEBT` informativo com exit 0. O limite de tamanho do §0 vira regra executável contra erosão sem refatoração forçada; quem pagar dívida atualiza o bloco `baseline_debt` com o one-liner do header do `audit.sh`.
- **Verificado**: exit 0 com a dívida conhecida (14 linhas DEBT); FAIL em arquivo novo de 401 linhas (temporário, removido); FAIL em crescimento além do baseline (entrada temporária 389→390, revertida); `qa_runner.sh` end-to-end com PASS nos 3 estágios nesta máquina (RADV RX 9070 XT).
- **Build no host, sem docker — causa raiz do flip-flop**: o `run.sh` roda `bash build.sh` **no host a cada lançamento** (L391) → `cmake -S gpu -B out/gpu` com `$PWD` do host → qualquer `out/gpu` configurado no container é reescrito com paths absolutos `/home/shadowy/...`, e o ninja do container quebra na próxima re-regeneração de globs (`mkdir(/home/shadowy): Permission denied` + edges "missing"). Como o gate convive com os lançamentos do usuário, o estágio 2 builda no mesmo lugar em que o jogo builda; o docker (`bbport-builder:ubuntu22`) segue existindo para build limpo manual, mas saiu do gate.

### 117. Tag `[Shaders]` no Início das Linhas de Log do Subsistema de Shaders
- **Motivo**: as linhas de compilação/cache de shaders já se identificavam por componente (`VkPipelineCache:`, `ShaderDiscovery:`, `BbShaderPack:`, `BbShaderSplash:`), mas faltava uma tag única no início para filtrar o subsistema inteiro com um grep só (pedido de UX: encontrar as linhas de shaders no meio do log geral).
- **Mudança**: os 14 printfs de runtime de shaders em 4 arquivos de `extra/` (`bbport_vk_pipeline_cache.cpp` ×6, `bbport_shader_discovery.cpp` ×2, `bbport_shader_pack.cpp` ×2, `bbport_shader_splash.cpp` ×4) passam a começar com `[Shaders] `; o prefixo de componente continua logo depois da tag (lossless — `grep "VkPipelineCache"` segue funcionando). Filtro único: `grep "\[Shaders\]" <log>`. Todas as strings citadas nas seções 114/115 ganham esse prefixo no início; o restante fica idêntico.
- **Não tocado**: o relatório do `pipeline-reconstruct-test` (harness — o `qa_runner.sh` depende do formato exato da linha de self-test) e os echos do launcher `run.sh` (escopo launcher/back-end, não shaders).
- **Sufixo anti-ambiguidade**: a linha `session:` do `FlushTick` ganha ` [feedback off: hits/valid need BB_PIPELINE_STATS=1]` quando a extensão não está armada — no log real de gameplay, `hits=0 valid=0` numa corrida sem a var foi lido como "cache não recarrega": sem a var esses campos são estruturalmente zero (fallback de wall-time), não medição.

### 118. Limpeza da Aba Shaders do ImGui: Eixo de Análise Estática Removido (Módulo `bbport_static_catalog` Deletado)
- **Causa raiz**: a aba Shaders (dentro do Memory Workspace) carregava um eixo inteiro de "análise estática vs runtime" (sub-aba Static Catalog com cobertura/bundles, classificação `[MATCH]`/`[RUN]` no Inventory, relatório JSON de diagnóstico) que dependia de `catalog.json` gerado por `tools/shader_catalog.py` — ferramenta que **não existe mais no repo**. O `catalog.json` virou um fóssil desatualizável e toda a análise derivada dele era peso morto na UI.
- **Mudança** (tudo em `extra/`, zero hooks upstream tocados): deletados `bbport_static_catalog.{h,cpp}` (~305 linhas); `bbport_shader_discovery.cpp` perde o `AutoLoad("dist/user/cache")` do init e o `RecordRuntimeShader` por shader novo (o catálogo era o único consumidor — busca grep não achou nenhum outro em `gpu/` ou `extra/`); `tab_shaders_views.{h,cpp}` perde `RenderStaticCatalog` inteira, o estado `s_class_filter`, a linha de radios All/Match/Runtime e a coluna Class do Inventory (tabela 7→6 colunas), o bloco de export do relatório JSON em Diagnostics (restam os 3 contadores de rebuild e o botão de export de `shader_recipes.bin`, vivo via `pipeline_import.cpp`); `tab_shaders.cpp` perde a sub-aba "Static Catalog" e o include. Rótulo defasado "Shader Recipes Pack (Upstream 0.7)" → "Shader Recipes Pack".
- **Resultado**: `tab_shaders_views.cpp` 305→244 linhas (**sai da lista de dívida**; ratchet do `audit.sh` atualizado para 13 entradas), `tab_shaders.cpp` 132→125, `bbport_shader_discovery.cpp` 291→288. Comportamento visível: 5 sub-abas → 4 (Overview, Live Event Log, Inventory, Diagnostics & Export).
- **Não tocado**: o arquivo `catalog.json` na raiz (dado, não código — pode ser insumo de ferramentas locais); os eixos vivos (Overview com grimório/driver-cache F0/recipes, Live Events via hooks de `pipeline_store.cpp`, Inventory, export de recipes).
- **Caçada de referências pós-build**: o grep inicial cobriu só `extra/`+`gpu/` e o build derrubou dois pontos em `tests/`: `test_pipeline_reconstruct.cpp` (include + bloco de relatório `BB_SHADER_REPORT`/`--report` removidos — o DumpInventory de shutdown segue como caminho de export) e `test_shader_discovery.cpp` (include + seção 6 de teste do catálogo removida; seções vivas de discovery e `BbCompileQueue` intactas — verificado compilando e linkando `shader-discovery-test`).


### 119. Retorno do Toggle do Relay P2P (BBR1) na Página Online Ativa
- **Causa raiz**: a página ativa `launcher/bbport_launcher_online.py` (0.7.x) perdeu o `p2p_relay_row` da 0.6.7. O suporte continua inteiro em `libbbnet20.so` (`vendor/shadps4/core/net/p2p_relay.cpp`, lê `SHADPS4_P2P_RELAY`) e o `run.sh` ainda traduz `p2p_relay=0` do `bbport.ini`, mas nenhuma linha da UI ativa gravava o valor — o `hasattr(self, "p2p_relay_row")` em `bbport_game_runner.py` escondia a opção.
- **Mudança (launcher apenas, sem rebuild do módulo)**: `p2p_relay_row` restaurado no grupo Conexão; `store()` grava em `settings.json` e `bbport.ini`; `bbport_launcher_common.game_environment()` exporta `BB_P2P_RELAY=1`/`SHADPS4_P2P_RELAY=0`; `DEFAULTS`/`INI_DEFAULTS` ganham a chave (ligada por padrão).
- **Semântica preservada**: ligado deixa o default do módulo (todo peer conhecido pelo relay); desligado força P2P direto por UDP (`SHADPS4_P2P_RELAY=0`), como na 0.6.7.
- **Teste**: `test_p2p_relay_in_active_launcher_env` cobre `DEFAULTS`/`INI_DEFAULTS` e o env da página ativa nos dois estados.
- **Compatibilidade 19/20**: `libbbnet20.so` já fala shadNet v1, BBSC e BBR1 idênticos à geração 19; o que difere é o enquadramento do transporte P2P do SceNet (0.19: `[src vport][dst vport][payload]`; 0.20: marcador `0xFF` + kind + vports + digest/crypto). Um codec legado com detecção por peer dentro do próprio `libbbnet20.so` é viável, mas ficou fora desta mudança por exigir validação com um peer Windows 0.19.
### 120. Retorno da Verificação de Login shadNet, do Discord Client ID e do Fallback de Conta THD
- **Causa raiz**: a página Online ativa da 0.7.x perdeu três recursos que ainda têm consumidor real no módulo/C++: o botão "Проверить вход" (`bbport_shadnet.check_login`), a linha `discord_client_id` (lida por `gpu/shim/bbport_settings.cpp` e usada em `extra/contact/rpc/bbport_discord_rpc.cpp`) e o fallback de conta THD via `account.json` (`bbport_hosts.load_account`).
- **Mudança (launcher apenas)**: `on_shadnet_login`/`set_login_result` e a linha de login portados da `bbport_online.py` para a `OnlinePage` ativa; linha "Discord Client ID" restaurada gravando em `settings.json` e `bbport.ini` (`DEFAULTS`/`INI_DEFAULTS` ganham `discord_client_id` = `1560890616811102219`); `game_environment()` cai para `account.json` quando `thd_online_id`/`thd_token` estão vazios (valores explícitos continuam vencendo).
- **Não reintroduzido**: `BB_HOST_*`, `BB_ONLINE_BACKEND`, `BB_NET_GEN` e o seletor Gen 19 continuam fora — não há consumidor atual (o codec legado 19/20 ainda não existe), então seriam opções falsas.
- **README corrigido**: removida a afirmação de que `libbbnet.so` (Gen 19) ainda é distribuído via `BB_NET_GEN=19`; documentados relay, verificação de login e Client ID.
- **Testes**: `test_discord_client_id_setting`, `test_thd_account_fallback` e `test_online_page_rows` (15 testes na suíte, OK).

### 121. Codec P2P Gen 19 (shadp2p 0.6.x) Dentro do `libbbnet20.so`, com Checkbox Explícito no Launcher
- **Causa raiz**: o `libbbnet20.so` fala shadNet v1, BBSC e o relay BBR1 como a geração 19, mas o transporte P2P do SceNet mudou de enquadramento. A 0.19 usa `[src vport 2][dst vport 2][payload]` (vports big endian; signaling `0xFFFF/0xFFFF`, UDP apenas), enquanto a 0.20 usa `0xFF` + flags/kind + vports + digest/crypto. Sem codec legado, um peer 0.6.x/Windows descarta ou entrega no vport errado cada pacote P2P — nenhum `.so` extra resolve isso sem duplicar o módulo inteiro.
- **Novo `extra/net/bbport_p2p_legacy.{h,cpp}`**: `Extra::Net::Legacy19Codec : Core::Net::P2P::Codec` implementa somente o wire 0.19 — `Decode` lê 4 bytes BE, `dst == 0xFFFF` vira `Signaling`, o resto `Datagram`; `EncodeDatagram`/`EncodeSignaling` escrevem os vports em BE; `EncodeStream` devolve vazio e `StreamPseudoHeader` devolve zeros (0.19 não tem P2P TCP). `Protection` é ignorado (0.19 não tem digest/crypto/assinatura) e o `Keyring` do codec 0.20 não é usado.
- **Seleção explícita e custo zero no default**: `Extra::Net::MakeP2PCodec()` (inline no header) lê `BB_NET_GEN` uma única vez na criação do transporte; somente o valor exato `"19"` constrói `Legacy19Codec`. Não definido ou `"20"` constrói o `FramingCodec` de hoje, no mesmo caminho de código — o modo 0.20 continua byte-idêntico e o codec legado nem é instanciado.
- **Hook upstream mínimo**: `net_p2p.cpp` ganha 1 include e a linha do `FramingCodec` passa a chamar a factory — 2 linhas com `// [extra]`, nenhum outro arquivo upstream tocado.
- **Build**: `gpu/bbnet20/CMakeLists.txt` adiciona `extra/net/bbport_p2p_legacy.cpp` ao `bbnet20` e o alvo `bbnet20-p2p-legacy-test` (`EXCLUDE_FROM_ALL`) que compila só o codec + teste, sem crypto nem o codec 0.20. O glob de `extra/*.cpp` do `bbgpu` exclui esse arquivo (`gpu/CMakeLists.txt`), porque ele inclui headers privados do módulo e não pode entrar na biblioteca da GPU.
- **Launcher**: novo `Adw.SwitchRow` "Совместимость с Gen 19 (shadp2p 0.6.x)" no grupo Conexão, desmarcado por padrão (`p2p_gen19` em `DEFAULTS`/`INI_DEFAULTS`); `store()` grava em `settings.json` e `bbport.ini`; `game_environment()` exporta `BB_NET_GEN=19/20` na sessão online; o `run.sh` lê `p2p_gen19` do ini para lançamentos diretos.
- **Testes**: `tests/test_bbport_p2p_legacy.cpp` cobre vetores dourados de datagram/signaling, payload vazio e máximo, rejeição de frames <4 bytes, vports big endian, `Protection` ignorado e a seleção exata de `BB_NET_GEN`; `tests/test_launcher_dev.py` cobre default/ini, env nos dois estados e a row da UI.
- **Substitui a nota da entrada 120**: `BB_NET_GEN` volta a ser exportado, agora com consumidor real no módulo e uma opção explícita na UI ativa; `BB_HOST_*`, `BB_ONLINE_BACKEND` e o seletor da página antiga continuam fora.
- **Limitações**: o modo vale para a sessão inteira (sem codec por peer; peers 19 e 20 misturados não funcionam) e perde digest/crypto/assinatura da 0.20 — inerente à 0.19. A prova fim-a-fim exige um peer Gen 19 real (0.6.7 Linux/Windows) num servidor shadNet privado, ainda não disponível; a validação feita é por vetores dourados e testes do launcher.

### 122. Fluidez: sleep do Frame Gen, Log em Lote, Cache de Save-Slot, Pipeline Cache e Limite ao Vivo
- **Causa raiz (medida)**: a baseline (Ryzen 7 5700X3D + RX 7800 XT, ~162–172 FPS parado) perdia 9,6 s em quadros acima de 50 ms; com o pacote abaixo caiu para 1,2 s, e a mediana de frame time foi de 21,2 ms para 10,8 ms.
- **A2 — sleep dos quadros gerados (`gpu/render/presenter.cpp`)**: `std::this_thread::sleep_until` do MinGW dorme em milissegundos inteiros truncados, então cada quadro interpolado chegava até 1 ms atrasado e o ritmo tremia nessa ordem. Trocado por `Common::SleepUntil` (timer de alta resolução do Windows, `gpu/platform/win32/posix_win32.cpp`) com os últimos 200 µs em spin de `pause` (o jitter de acordar do SO é da mesma ordem); fora de x86-64, `SleepUntil` até o alvo.
- **A3 — limite ao vivo (`gpu/platform/videoout/display.cpp`)**: `period_now`/`immediate_flips` viraram lambdas sem captura que releem `BbFrameRate::Limit()` a cada uso, então F3 e a lista do overlay passam a valer sem reiniciar; `scheduled_fps` compara o FPS **depois** do clamp (`30..480`) e reinicia o agendamento (`next_flip = now`) quando o período muda, e o laço interno de flips imediatos sai (`break`) para o laço externo rearmar. Comparar depois do clamp evita reset inútil (500 → 480 FPS não muda o período).
- **A5 — log em lote (`gpu/common/logging/log.cpp`)**: cada linha era um `fprintf` + `fflush` e, como `src/probe.c` deixa `stderr` sem buffer, cada pedaço do `fprintf` era um `write` próprio. As linhas passam a ser acumuladas e entregues em um `fwrite` por lote: `warn` ou pior na hora, o resto a cada 64 linhas, 64 KiB ou 250 ms. `Common::Log::Flush()` (novo, em `log.h`) é chamado no assert (`common/assert.cpp`), no fechamento da janela (`shim/bbgpu.cpp`) e no "Exit Game" (`shim/bbport_game_menu.cpp`), que usam `_Exit`/`_exit` e não passariam pelo `atexit`; no `exit()` normal o handler registrado descarrega o lote. O lote é um `static` local de função que nunca é destruído (um destrutor estático que logasse o encontraria morto), com `try_to_lock` nos dois caminhos para que uma thread presa em `Write()` não trave a saída. `probe.c` não foi tocado: um segundo `setvbuf` depois do primeiro write seria comportamento indefinido — por isso o lote vive no logger, não no stream.
- **A7 — pipeline cache (`extra/core/shader_precompile/bbport_vk_pipeline_cache.cpp`)**: `s_saved_creates` passou a `std::atomic` (store release no worker, load acquire no `FlushTick`); antes era um `uint64_t` escrito pelo worker e lido pela thread de present, uma corrida de dados que podia reenfileirar um save já feito. O lock exclusivo do worker virou `std::unique_lock(..., std::try_to_lock)`: tomá-lo bloqueante deixava toda criação de pipeline (que usa o lock compartilhado) esperando o tempo da consulta; ocupado, o pedido volta para a fila e é tentado 20 ms depois.
- **B1 — save-slot do BBSC (`gpu/bbnet20/bbsc/core/bloodborne_re.cpp`)**: `InvalidateBbscActiveSaveSlot()` existia sem chamador, então o cache de 60 s nunca era invalidado ao trocar de personagem e os arquivos de progresso/loot podiam ser gravados no slot anterior. A invalidação agora compara (sem I/O) o slot que o runtime mantém em memória (`runtime_savedata_active_slot`) com o do último scan. Também corrigido o cache de resultado vazio: quando a varredura não achava slot, `has_value()` era falso e o TTL não valia — a varredura de disco voltava a rodar a cada chamada, que é justamente o custo que o cache existia para evitar. `s_cache_valid` separa "nunca escaneado" de "escaneado e vazio".
- **A1 — código morto removido**: `extra/core/frame_pacing/bbport_frame_rate_pacer.{h,cpp}`, `bbport_present_pacer.{h,cpp}`, `bbport_frame_rate_tables.h` e os reexports em `gpu/shim/` não tinham chamadores (só se referenciavam entre si). O caminho ativo é `gpu/shim/bbport_frame_rate.cpp` (`FrameStep` + `Redirects`). `docs/FRAME_RATE_AND_SHADERS_ANALYSIS.md` foi reescrito para separar o histórico da engenharia reversa do que roda hoje, e as entradas 33 e 72 deste arquivo passaram a marcar o antigo pacer como histórico.
- **Docs**: `README.md`/`README.ru.md` (seção de confiabilidade, item "Save-slot I/O") passaram a registrar a invalidação do cache por troca de personagem.
- **Validação**: build Windows (`out/win-native`, clang/RelWithDebInfo) de `bb-probe`, `bbnet20` e `pipeline-reconstruct-test` sem erros; `pipeline-reconstruct-test` sai com 0; `extra/tools/audit.sh` volta a sair com 0. O baseline do audit ganhou `bbport_vk_pipeline_cache.cpp` (405 linhas no HEAD, 410 com o A7) e `bbport_shader_pack.cpp` (419, intocado): as duas violações eram anteriores a este pacote e só apareceram agora porque o script passou a ser rodado — ambas pagas na entrada 123.
- **Limitações**: o ganho de fluidez foi medido na máquina citada; `settings-test` continua quebrado no Windows por um problema anterior a esta mudança (o `VERSION` da raiz é achado como o header `<version>` num filesystem case-insensitive).

### 123. Refactor Estrutural de `extra/`: Módulos por Assunto e Zero Arquivos Acima de 300 Linhas
- **Motivo**: o limite do §0 (300 linhas de alvo, 400 de teto) nunca foi cumprido — o `audit.sh` sancionava 14 arquivos como dívida e o maior deles tinha 1086 linhas. O efeito era concreto: seções de overlay, tabelas de strings traduzidas e caminhos de I/O de disco se misturavam em arquivos que ninguém conseguia revisar por inteiro, e números mágicos de offsets/limites ficavam soltos no meio da lógica.
- **Regra aplicada**: um assunto por arquivo, com um `_internal.h` por módulo guardando o que os pedaços compartilham (constantes de domínio, enums nomeados, estado comum). Nenhum símbolo público mudou de nome ou de assinatura; todo arquivo dividido preserva os literais originais (conferido por script comparando os literais do HEAD com os do conjunto novo, inclusive concatenação de strings).
- **Divisões (arquivo original → arquivos novos / linhas)**:
  - `management/sections/bbport_overlay_sections.cpp` (1086, deletado via `git rm`) → 7 `bbport_overlay_section_*.cpp` por aba + `bbport_overlay_ui.{h,cpp}` com `Tr`/`TrList`/`Checkbox`/`Slider`/`ComboOptions`/`Hint` e os namespaces `Theme`/`Size`.
  - `management/i18n/bbport_strings.cpp` (729) → 6 tabelas por área (`_table_{upscaler,visuals,output,effects,ui,camera}.cpp`) + `_internal.h`; a tabela deixou de ser posicional e passou a `{StringId, {en,pt,ru}}` explícito, resolvida por `FindText` em `kTables[]`.
  - `contact/rpc/bbport_discord_rpc.cpp` (708) → `.cpp` (worker/API), `_activity.cpp` (texto), `_art.cpp` (arte do card), `_json.cpp` (SET_ACTIVITY/escape/pid), `_ipc.cpp` (pipe/socket) e `_internal.h`; Client IDs viraram constantes públicas `kBbportClientId`/`kShadps4ClientId`.
  - `contact/game_data/bbport_game_data.cpp` (327) + `_party.cpp` (301) → `.cpp` (relógio e snapshot), `_world.cpp` (mapa/playtime), `_death.cpp` (contador), `_save.cpp` (slot aberto), `_names.cpp` (mapas/chefes), `_status.cpp`, `_party.cpp` + `_party_read.cpp` (leitura das listas de personagem) e dois headers internos; offsets de personagem unificados em um só lugar.
  - `management/prompts/bbport_button_prompts.cpp` (655) → `_table.cpp`, `_draw.cpp`, `_canvas.{h,cpp}`, `.cpp`; `bbport_fps_hud.cpp` (538) → `_metrics.cpp`, `_graph.cpp`, `_internal.h`.
  - `bbport_pad_hook.h` (448) → guarda-chuva de `_identity.h`, `_touch.h`, `_input.h`; `_touch.h` (304) perdeu o evdev Linux para `_touch_evdev.h`. Enums `ExtraPadLayout`/`ExtraPadDevice` e defines `BB_*` substituíram os números soltos (o header é incluído por C, então tudo virou `static inline`).
  - `core/debugger/breakpoint.cpp` (566) → `_signals.cpp`, `_worker.cpp`, `_internal.h`; `mem_scanner.cpp` (305) → `.cpp` (API/estado), `_scan.cpp` (as duas varreduras), `_internal.h` (limites + `MatchesCondition`).
  - `ui/views/view_disasm.cpp` (466) → `.cpp` (estado/API), `_render.cpp`, `_menu.cpp`, `_internal.h`; `ui/tabs/tab_scanner.cpp` (314) → `.cpp` (estado/Render), `_controls.cpp`, `_results.cpp`, `_internal.h`; `ui/keyboard/bbport_keyboard.cpp` (307) → `.cpp` (cursor/gamepad), `_render.cpp` (diálogo), `_internal.h` (tabelas de teclas + enum `ActionKey`).
  - `memoryDev/bbport_memory_dev.cpp` (391) → `.cpp` (consultas), `_settings.cpp` (env/ini), `_internal.h` (tabelas por preset); `core/camera/bbport_mouse_camera.cpp` (325) → `.cpp`, `_patch.cpp` (hooks de imagem), `_internal.h`.
  - `core/shader_precompile/bbport_vk_pipeline_cache.cpp` (410) → `.cpp` (handle, worker, guard, `FlushTick`), `_io.cpp` (load/save com validação de header e escrita tmp+rename), `_internal.h` (timings `kLullNs`/`kBackoffNs`, header do blob, estado).
  - `core/shader_precompile/bbport_shader_pack.cpp` (419) → `.cpp` (caminhos/helpers/migração), `_inspect.cpp` (existe?/stats/verify), `_pack.cpp` (monta o arquivo), `_unpack.cpp` (expande), `_restore.cpp` (semeia cache vazio do mestre), `_internal.h`.
- **Números mágicos nomeados** (amostra): `kPlaytimeMs`, `kMapEntryStride`, `kWorldChrStride`, `kChrBlockArray`, `kScanChunkBytes`/`kScanPageBytes`/`kMaxScanMatches`, `kMaxLineLength`/`kSecondsPerHour`, `kLullNs`/`kBackoffNs`, `kTouchpadWidth`→`BB_TOUCHPAD_*`, `kDeathDebounce`, `kKeyPrefixLength`, `kMaxBlockCharacters`.
- **Correções de link achadas pelo build**: `InstallHandlers()` do breakpoint ficou fora de `namespace detail` (quebrava o `bb-probe`); `FormatCoopStatus` do detail virou `FormatCoopStatusText` (colidia com a API pública homônima); `LoadDriverCache`/`SaveDriverCache` tiveram de ficar no namespace público (o `using detail::X` conflita com a declaração do header e `pipeline_store.cpp` chama `BbVkPipelineCache::SaveDriverCache`); `mem_scanner.cpp` precisou incluir `pattern_scanner.h` para completar `PatternToken`; `bbport_pad_hook_touch.h` ganhou `(void)now` no caminho não-Linux.
- **Resultado**: `extra/` não tem mais **nenhum** arquivo acima de 300 linhas — `baseline_debt` do `audit.sh` está vazio e o exit code do gate voltou a significar algo. Maiores arquivos agora: `bbport_shader_pack_pack.cpp` (204), `bbport_vk_pipeline_cache_io.cpp` (198), `bbport_discord_rpc_ipc.cpp` (290), `bbport_shader_discovery.cpp` (288).
- **Validação**: `ninja -C out/win-native` completo sem erros nem warnings novos; `pipeline-reconstruct-test` (100/100 pipelines, 0 rebuilds) e `assert-dump-test` OK; `extra/tools/audit.sh` com `[1]` vazio e exit 0. `gpu/CMakeLists.txt` usa `GLOB_RECURSE ... CONFIGURE_DEPENDS`, então nenhum arquivo novo exigiu edição de CMake.
- **Limitações**: refactor sem mudança de comportamento pretendida — a validação em jogo da fluidez do pacote 122 continua pendente; as falhas de teste pré-existentes (`settings-test`, `camera-matrices-test`, `frame-generation-test`, `translation-memory-test`, `bbnet20-p2p-legacy-test`) seguem iguais e não foram tocadas.

### 124. Dump de Threads do Relatório de Stall no Windows (diagnóstico do travamento ao sair da clínica)
- **Sintoma relatado**: o jogo congela logo no início, na transição clínica → Yharnam, com `GPU stall report: the guest has submitted nothing for 5 s (submitted 226865, decoded 226865, waiting 0, commands 0)`.
- **Leitura do relatório**: `submitted == decoded` e `waiting 0` significam a GPU **ociosa** — ela consumiu tudo o que o guest entregou. Não é stall de GPU: quem parou foi a thread do jogo. O texto "GPU stall report" é só o nome do vigia (`StallWatch`, `gpu/cp/command_processor.cpp`), que dispara para os dois casos.
- **Lacuna que impedia achar a causa**: o dump de todos os threads do `ReportStall` é `#ifndef _WIN32` (depende de `/proc/self/task` + SIGUSR2, `src/probe.c: dump_frames`). No Windows o log trazia o cabeçalho `command thread ... stack follows` e **nada** depois dele — nem uma pilha, nem um RIP. Sem isso não há como dizer se o guest espera um job, um fence ou um lock.
- **Correção**: `bb_win32_dump_threads()` em `gpu/platform/win32/posix_win32.cpp`, chamada pelo mesmo ponto do `ReportStall` sob as mesmas condições (`BB_STALL_THREADS=0` desliga; uma vez por execução). Usa `CreateToolhelp32Snapshot` + `SuspendThread`/`GetThreadContext` (o mesmo par que o amostrador de `tools/win-sampler` já usava) e nome de thread via `GetThreadDescription`. Imprime, por thread, RIP e RSP e a cadeia de chamadas, com endereços relativos à imagem (`game+0x...` para o eboot, `exe+0x...` para o port) e o módulo dono para o resto (`ntdll.dll+0x...`).
- **Cuidados de segurança adotados**: nada é impresso, alocado ou formatado com uma thread suspensa (linhas montadas em buffer fixo e impressas depois de todas retomadas — uma thread suspensa pode deter o lock do `stdout`); cada leitura da pilha passa por `VirtualQuery` (recusa páginas não comprometidas, `PAGE_GUARD`/`PAGE_NOACCESS` e travessia de região, que derrubariam quem lê); a caminhada exige que o `rsp` seja legível antes de entregá-lo a `RtlVirtualUnwind`, que o `rbp` seja alinhado/legível/estritamente crescente e que todo retorno caia em código carregado; e para no primeiro quadro que não avança.
- **Três correções no próprio dump, achadas no primeiro uso real** (log `dist/BBPort-0.7.8-4-win-x86_64/logs/20261105_110240.log`, 121 threads):
  - endereços fora do exe saíam como hex cru (`rip 0x7ffe8e4e59c4`); agora `where()` resolve o módulo dono via `GetModuleHandleExA(FLAG_FROM_ADDRESS)` e imprime `ntdll.dll+0x...`.
  - a caminhada parava no primeiro chamador fora do `.text` do exe, e **todas** as 121 threads estavam paradas dentro do `ntdll` — nenhuma cadeia foi impressa. Agora ela atravessa a cadeia inteira e só **imprime** os quadros que são código do port (`exe+`) ou do jogo (`game+`).
  - **o motivo de fundo**: a cadeia de frame pointers não serve para threads paradas em chamada de sistema — elas chegam com `rbp = 0` (o trânsito para o kernel não o preserva), então não havia nem de onde começar. A caminhada passou a usar `RtlVirtualUnwind` sobre as tabelas `.pdata` do x64 (o mesmo mecanismo que o próprio Windows usa), que não depende de `rbp`; para a imagem do jogo, que roda nativamente mas não tem `.pdata` registrado neste processo, ela cai no frame pointer (o eboot mantém `rbp`: os prólogos são `push rbp; mov rbp, rsp`). Isso foi verificado num teste isolado: com frame pointer todas as threads em syscall saíam vazias (`rbp 0`); com `RtlVirtualUnwind` a cadeia sai completa e chega nos quadros do exe.
- **Validação**: `ninja -C out/win-native` completo sem erros nem warnings novos. Exercitado em jogo: 121 threads listadas, e foi esse dump que apontou a causa raiz — ver entrada 125. Ganhou teste próprio, `stall-dump-test` (`tests/test_stall_dump_threads.cpp`): como o dump só roda num stall, era o único código do diagnóstico sem cobertura — o teste o chama de verdade com um thread parado em `Sleep`, captura o `stdout` e exige que a lista exista, que os endereços fora do exe tenham nome de módulo e que **alguma cadeia chegue a um quadro do exe** (a caminhada por frame pointer só falharia aí, e é o que se quer pegar).
- **Não é regressão deste trabalho (evidência)**: `logs/20261018_090128.log`, de 18/10, anterior ao refactor e ao pacote de frame pacing, tem a mesma assinatura (`submitted 5941723, decoded 5941723, waiting 0`), e nele a GPU **recuperou** depois (volta a 48,7 FPS, mediana 16,67 ms). Ou seja, o congelamento do guest numa troca de mapa é um sintoma recorrente e anterior; o que faltava era o diagnóstico. Os hooks relacionados (`job slot leak fix`, `blur-kernel clamp`) estavam ligados neste run e o contador de slots devolvidos nunca subiu — nenhuma linha "a failed render job gave its slot back".

### 125. Congelamento ao sair da clínica: laço infinito em `WrapPi` na câmera de mouse
- **Causa raiz (encontrada pelo dump da entrada 124)**: a thread principal do jogo estava presa em `BbMouseCamera::detail::WrapPi(float)`, chamada por `ApplyMouseTurn` (`extra/core/camera/bbport_mouse_camera.cpp`). O corpo era `while (angle > kPi) angle -= kTwoPi;` (e o espelho para o lado negativo) — um laço que desconta **uma volta por iteração**, em `float`. Com `angle` infinito ele nunca termina (`inf - 2*pi == inf`); acima de ~1,3e8 também não, porque a volta fica abaixo de meio ulp do valor e a subtração arredonda de volta para o próprio `angle`; abaixo disso ele termina, mas custa `|angle|/(2*pi)` iterações (até ~2e7 por chamada, e são duas por quadro). Nos três casos o guest para de submeter comandos e o vigia cospe `submitted == decoded`, GPU ociosa — exatamente o que os logs de 0.7.6 e 0.7.8 mostram.
- **Por que só agora**: a câmera de mouse 1:1 é um caminho recente e o `WrapPi` está nele desde o início (o HEAD também tem o `while`: `git show HEAD:extra/core/camera/bbport_mouse_camera.cpp`). O gatilho é o estado da câmera no instante da chamada, não o build — daí a recorrência intermitente com a mesma assinatura em runs anteriores (entrada 124).
- **Correção**:
  - `WrapPi` passou a `std::remainder(angle, kTwoPi)` com guarda `std::isfinite` (não-finito devolve 0). É a operação exata de redução módulo 2π, em um passo, para qualquer magnitude; o resultado de `remainder` cai em `[-pi, pi]` e o único valor fora de `(-pi, pi]`, o `-pi`, é dobrado para `+pi`. A função mudou para `bbport_mouse_camera_internal.h` (continua `inline`, mesmo uso), porque é a parte pura do caminho e agora tem teste.
  - `ApplyMouseTurn` saneia o estado da câmera antes de usá-lo: `yaw` e `pitch` não-finitos lidos de `cam + kYawOffset`/`kPitchOffset` viram 0 — o mesmo padrão do C-009 (a câmera existe mas ainda não foi preenchida); sem isso o NaN seguiria para a aritmética do jogo e um infinito voltaria a custar o quadro inteiro. O mesmo vale depois do delta (`pitch += dy * scale * sign_y`): um `dy` não-finito faria o `pitch` infinito e o valor seria gravado de volta na câmera, então ele é checado de novo antes do clamp. E os limites de pitch só são aplicados quando os **dois limites já descontados do stick** são finitos e ordenados (`lowest_bound <= highest_bound`): antes bastava `highest > lowest`, que é falso para NaN mas verdadeiro para `highest = +inf`, e o `std::clamp` com limite não-finito (ou invertido, vindo do `stick`) gravaria NaN/±inf na câmera.
- **Teste de regressão**: `mouse-camera-angle-test` (`tests/test_mouse_camera_angle.cpp`, novo alvo no `gpu/CMakeLists.txt`), sem GPU nem guest. Cobre a identidade para ângulos pequenos, o limite `±π`, mil voltas nos dois sentidos, e os valores que travavam: `FLT_MAX`, `±1e30`, `±1,3e8` (onde o espaçamento do `float` passa de uma volta), `±inf` e `NaN` — todos precisam voltar um ângulo em `(-π, π]`. O stress de tempo roda num thread com watchdog de 10 s e sai com falha explícita ("o laço sem limite da entrada 125 voltou") em vez de travar a suíte, porque o defeito original **não termina**: um teste que rodasse o laço antigo penduraria em vez de falhar. Conferido na prática: compilado contra um header com o laço antigo, o teste falha.
- **Verificação**: build Windows completo (`ninja -C out/win-native`) sem erros nem warnings novos; `mouse-camera-angle-test` e `stall-dump-test` saem com 0; `llvm-symbolizer --obj=<bb-probe.exe> 0x1405057dc` (base `0x140000000` + o RVA que o dump imprimiu) resolve para `WrapPi` ← `ApplyMouseTurn`, confirmando o achado contra o binário que rodou.
- **Pendente**: reproduzir a transição clínica → Yharnam com esta build e confirmar que o congelamento sumiu. Se voltar, o dump já sai com os frames do guest nesta mesma versão.

