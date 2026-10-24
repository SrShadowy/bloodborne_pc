# Shadow Changes — Comprehensive Technical Development Log (Compact Edition)

Registro técnico consolidado de melhorias arquiteturais, correções de bugs, mitigação de glitches visuais, internacionalização (i18n), integração online e estabilização de simulação do **Bloodborne PC Port (`BBPort`)**.

---

## 1. Resumo Executivo
- **Build & Shaders**: Compilação nativa no Linux (x86_64, Vulkan/Mesa, AMD/NVIDIA), suporte a FSR 3.1 / FSR 4.1.1 e pipeline caches.
- **Gráficos & Glitches**: Eliminação de reflexos invertidos no céu, vertex explosions no FaceGen/Havok e borrões de motion blur no céu.
- **Pacing & Modo Foto**: Desacoplamento do ritmo no Presenter, suspensão nativa via `FD4TaskManager` (+0x54b2e30) e guarda anti-crash no Havok (`PushBackSafe`).
- **Arquitetura `extra/` & Governança**: Modularização completa em `extra/` com ganchos mínimos `// [extra]` conforme `MyGUIDELINES.md`.
- **Ferramentas In-Game**: Scanner de memória com watchlist, desassemblador x86-64 com símbolos e NOP patching, HUD customizável e Discord RPC.
- **Online & Localização**: Suporte a pools BBSC e The Hunter's Dream (bbhost/Discord), e localização trilíngue (PT-BR, EN, RU).

---

## 2. Inverted Reflection Glitch (Planar Reflection in Skybox)
- **Problema**: Prédios invertidos e borrões no céu devido a reflexos planares (`PA_CL_UCP` clip plane) vazando para a skybox e depth buffer.
- **Implementação**:
  - `gpu/shadps4/video_core/renderer_vulkan/vk_rasterizer.cpp`: Filtro em `FilterDraw()` e `FilterDrawPasses()` descartando draws quando `user_clip_plane_enable != 0`.
  - `gpu/shim/bbport_settings.h`: Adicionado toggle `std::atomic<bool> puddle_reflections{false}` com persistência em `bbport.ini`.
- **Resultado**: Skybox 100% limpa e redução de 200–500 draw calls por quadro.

## 3. Vertex Explosions in Cutscenes & FaceGen Geometry
- **Problema**: Distorções e picos de vértices em animações faciais e panos com readbacks desativados ou modo preciso (`BB_READBACKS=2`).
- **Implementação**: Padronizado `BB_READBACKS=1` (Relaxed) e `object_motion=0` em `bbport.ini`.
- **Resultado**: Eliminação de tearing de malha e resolução de deadlocks durante o boot de threads de panos/FaceGen.

## 4. Full Modular Internationalization (i18n) & Code Hygiene
- **Problema**: Strings russas hardcoded e monolíticas dentro das chamadas ImGui no overlay.
- **Implementação**:
  - `extra/management/i18n/bbport_strings.{h,cpp}`: Enum `StringId` com 65+ IDs, macro `S(id)` e suporte dinâmico a EN, PT-BR e RU.
  - `gpu/shim/bbport_settings.h`: Adicionado `enum Language` e persistência de `menu_language`.
  - `gpu/shim/bbport_overlay.cpp`: Interface refatorada para usar `S(id)` com combo interativo de seleção de idioma em tempo real.

## 5. Startup Black Screen Freeze & AvPlayer Pipeline Fixes
- **Problema**: Hang intermitente a 62 FPS na inicialização antes do menu principal.
- **Causas & Solução**:
  - `AvPlayer`: Corrigido erro grave de double-indexing (`m_streams[m_video_stream_index.value()].ffmpeg_index`) e starvation de EOF em `IsActive()`.
  - `skip_intro`: Alterado padrão para `skip_intro=1` no launcher e INI, bypassando a inicialização de vídeos.
  - `Readbacks`: Eliminada exigência de `BB_READBACKS=2`, padronizando `1` (Relaxed).

## 6. Verification, Quick Start & Diagnostic Trials
- Validação contínua via `bash build.sh --test` e `python3 -m unittest discover tests/`. Inicialização via `bash run.sh` ou launcher GTK4. Overlay in-game: <kbd>Insert</kbd> ou <kbd>L3</kbd>+<kbd>R3</kbd>. Diagnósticos de shaders confirmaram anomalias visuais decorrentes do velomap do Yebis.

## 9. Definitive Resolution: Motion Blur & Velocity Map (Velomap) Artifacts in the Sky
- **Problema**: Faixas horizontais e verticais no céu com câmera em movimento causadas por vetores de velocidade infinitos gerados para o domo celeste ($Z \approx 1.0$).
- **Implementação**: Em `vk_rasterizer.cpp`, descartada a escrita de vetores de velocidade quando a geometria pertence ao sky dome / plano de fundo distante.
- **Resultado**: Céu perfeitamente estável com motion blur ativo sem produzir faixas na tela.

## 10. Modular In-Game Memory Scanner & Interactive Watchlist
- **Implementação**: Módulo `extra/core/debugger/mem_scanner.{h,cpp}` e `extra/ui/tabs/tab_scanner.cpp`.
- **Recursos**: Escaneamento de tipos (u8..u64, float, double) no espaço de memória guest com comparadores (`Exact`, `Increased`, `Decreased`, `Changed`), watchlist com freeze em tempo real e persistência JSON.

## 11. Real-Time Debugger, x86-64 Disassembler, CPU State, & NOP Patching
- **Implementação**: `extra/core/debugger/breakpoint.{h,cpp}` e `extra/ui/views/view_disasm.{h,cpp}`.
- **Recursos**: Hooking em `mprotect`/`trap` para hardware breakpoints, desassemblador Zydis/LLVM integrado, inspeção ao vivo de registradores x86-64 e toggle de NOP patching reversível.

## 12. Memory Hex Inspector & Multi-Type Data Inspector
- **Implementação**: `extra/ui/views/view_memviewer.{h,cpp}` e `extra/ui/tabs/tab_hexview.{h,cpp}`.
- **Recursos**: Visualizador hexadecimal de 16 bytes por linha, navegação rápida por endereços, edição direta de bytes e inspeção tipada contextual.

## 13. Comprehensive UX/UI Modernization, Font Safety & Toasts
- **Implementação**: `extra/ui/ui_manager.{h,cpp}`.
- **Recursos**: Sistema de notificações visuais toast com desvanecimento suave, fallbacks seguros de glifos UTF-8 para evitar asserts do ImGui e hierarquia de janelas docking.

## 14. RADV/AMD GPU Crash (Error 23 / Context Lost) & Indirect Dispatch Clamping
- **Problema**: Falha no driver RADV ao processar chamadas de compute shader indiretas com contagens de workgroup desproporcionais geradas pelo jogo original.
- **Implementação**: Em `gpu/render/rasterizer.cpp`, aplicação de clamping nos buffers de dispatch indireto antes da submissão à GPU.

## 15. Frametime Stabilization, Lock-Free Memory Bounds & Log Quieting
- **Implementação**: Otimização no envio de comandos de apresentação e supressão de mensagens repetitivas de log de shader warnings em release builds.

## 16. Dynamic FSR 4.1.1 Precision Detection (FP8 on RDNA 4 vs Emulated FP8 vs INT8)
- **Implementação**: Detecção automática de suporte nativo a FP8 em hardware AMD RDNA 4 (`VK_KHR_shader_float_controls2`), com fallback para FP8 emulado ou INT8 em arquiteturas anteriores.

## 17. Full Keyboard & Mouse (KBM) Integration with 360° Analog Mouse Look
- **Implementação**: `extra/core/camera/bbport_mouse_camera.{h,cpp}` e `extra/ui/keyboard/bbport_keyboard.cpp`.
- **Recursos**: Interceptação em `0x143cde0` adicionando delta do mouse diretamente aos ângulos de câmera (`ChrExFollowCam`) sem emular stick analógico, eliminando zonas mortas.

## 18. GTK4 / Libadwaita Launcher Modernization, Pango Markup & Trilingual Support
- **Implementação**: Modularização do launcher em `launcher/` com suporte nativo a PT-BR, EN e RU, validação de presets de configuração e estilização Adwaita moderna.

## 19. FSR 3.1 Frame Generation Integration
- **Implementação**: `extra/core/frame_gen/vk_frame_generation.{h,cpp}`.
- **Recursos**: Interpolação de quadros Vulkan em tempo real utilizando vetores de movimento e buffers de profundidade, dobrando a fluidez visual para telas de 60/120/144 Hz.

## 20. Code Architecture Modernization & Modular Decomposition (CODE_GUIDELINES.md)
- Estabelecimento do isolamento estrito entre upstream e código customizado, antecedendo a formalização do `MyGUIDELINES.md`.

## 21. Watchdog Timeout & FSR 3.1 Frame Generation Stabilization
- **Problema**: Travamentos do compositor Vulkan por esgotamento de recursos quando a taxa de quadros oscilava.
- **Implementação**: Inclusão de fences de sincronização no `Presenter` e controle de frame pacing adaptativo.

## 22. Hunter's Dream Lamp Teleport Crash & TextureCache Memory Safety Guards
- **Problema**: SIGSEGV ao teletransportar entre lâmpadas devido ao descarte prematuro de imagens no `TextureCache`.
- **Implementação**: Inclusão de referenciamento compartilhado e guarda de retenção de memória em `texture_cache.cpp`.

## 23. Image Trembling / Jitter Resolution & Frame Generation Restoration
- **Problema**: Tremulação da imagem estática ao aplicar vetores de câmera com frame generation ativo.
- **Implementação**: Desacoplamento da matriz histórica de projeção em `vk_rasterizer.cpp` e sanitização de jitter sub-pixel.

## 24. FPS Counter Overlay Stabilization & Max-Size Locking
- Trava de tamanho e alinhamento do contador de FPS in-game para evitar oscilações visuais no canto superior da tela.

## 25. FSR 4.1.1 FP8 Detection Restoration
- Correção de flags de identificação de recursos de ponto flutuante de 8 bits em drivers Vulkan recentes.

## 26. FSR Upscaled Output Presentation Restoration (`RasterScaling` Lifecycle Fix)
- Correção do ciclo de vida das imagens redimensionadas no pipeline de apresentação do Vulkan (`Presenter::Present`).

## 27. GPU-Timeline Indirect Dispatch Sanitizer (`sanitize_indirect.comp`)
- Compute shader dedicado para higienizar argumentos indiretos na própria GPU antes de pipelines de iluminação e partículas.

## 28. Player Death Screen / Scene Transition Flip Synchronization & Assertion Fix
- Sincronização segura das transições de tela cheia (tela de morte e loads) evitando asserts no swapchain da apresentação.

## 29. Interactive Virtual Keyboard with Full Gamepad & Mouse Support for IME Dialog
- **Implementação**: `extra/ui/keyboard/bbport_keyboard.{h,cpp}`.
- Teclado virtual na tela com suporte total a gamepad e mouse para criação de nome de personagem e diálogos IME do PS4.

## 30. Watchpoint / Single-Step Debugger Crash Fix (SIGTRAP Exit 133 Resolution)
- Correção no tratador de sinal `SIGTRAP` garantindo retorno limpo após a execução de instruções com trap flags ativas.

## 31. Shader Cache Loading & Driver Pipeline Cache Fixes
- Otimização do carregamento de caches de pipeline Vulkan em disco, acelerando inicializações subsequentes e reduzindo stutters de compilação.

## 32. Upstream 0.51 Release Merge & shadNet Online Subsystem
- Mesclagem da versão 0.51 do upstream e estruturação inicial dos protocolos de rede online.

## 33. In-Game Menu Frame Generation & Sub-millisecond Pacing Smoothing
- Ajuste no pacer de apresentação para evitar interpolação desnecessária durante a navegação em menus estáticos.

## 34. In-Game Menu Pop-Up Overlap Elimination via Native Steps Slider & Dynamic Selection Indicator
- Resolução de sobreposição de elementos na interface através de sliders com passos discretos (`Kind::Steps`).

## 35. Frame Generation Cadence Stabilization & 4-Slot Ring Buffer (120 FPS Output at 60 FPS Lock)
- Fila circular de 4 slots para apresentação suave a 120 FPS interpolados a partir de 60 FPS reais.

## 36. Launcher Architectural Modularization & Shader Cache Cleaning System
- Criação de módulos especializados (`bbport_config.py`, `bbport_devices.py`, `bbport_online.py`) e botão para limpeza de cache de shaders corrompidos.

## 37. Real-Time Shader & Pipeline Compilation Indicator in In-Game Overlay
- Indicador visual leve no overlay exibindo quando shaders Vulkan estão sendo compilados em segundo plano.

## 38. Native Frame Limit Configuration via `bbport.ini` (`fps_limit`)
- Suporte a limitação configurável de taxa de quadros (30..480 FPS) via arquivo INI sem demandar patches manuais.

## 39. FSR 3.1 Frame Generation & Object Motion Stabilization
- Estabilização de vetores de movimento de malhas dinâmicas durante o passe de upscaling.

## 40. Standalone GTK4 & Libadwaita Launcher Bundling (PyInstaller)
- Script de empacotamento standalone do launcher sem dependências externas de sistema para maior portabilidade.

## 41. In-Game Photo Mode (Modo Foto), Simulation Freeze, HUD Suppression & Screenshot Engine
- **Implementação**: `extra/management/sections/bbport_overlay_sections.cpp` e `extra/core/screenshot/bbport_screenshot.{h,cpp}`.
- **Recursos**: Pausa de simulação do mundo, ocultação dinâmica do HUD Scaleform (`hide_hud`) e captura de telas limpas em alta resolução para PNG sem overlay.

## 42. Fix Crash 23 on Yebis Post-Processing Assertion (`sceKernelDebugRaiseException`)
- Correção de assert no pós-processamento do Yebis ao calcular amostras de blur fora dos limites de tela.

## 43. Optional In-Game Info HUD / OSD (ImGui)
- HUD informativo leve e discreto configurável via overlay exibindo taxas de quadros, frametimes e uso de memória.

## 44. Native Discord Rich Presence (RPC) Integration
- **Implementação**: `extra/contact/rpc/bbport_discord_rpc.{h,cpp}`.
- Integração em tempo real com o Discord exibindo status de jogo, localização e nível do caçador.

## 45. CPU Engine Optimization & Render Thread Stutter Removal
- Eliminação de esperas ativas (spinlocks) na sincronização CPU-GPU, liberando ciclos de processador para o thread principal.

## 46. Discord RPC & Game Data Modularization (`gpu/shim/rpc/`)
- Desacoplamento da leitura de dados de jogo e serialização de presença externa.

## 47. Fix Real-Time Area / Map Name Resolution & Offsets
- Tabela de resolução de nomes de áreas baseada em offsets do `CurrentMapList` (`0x553B148`).

## 48. Fix Total In-Game Playtime Pointer & Regression Guard
- Resolução do ponteiro de tempo de jogo acumulado no `PlayerGameData` (+0x690) com guardas contra estouros de 64 bits.

## 49. Detection of Summoned NPC Hunters & WorldChrMan Phantoms
- Detecção em tempo real de fantasmas de NPCs aliados e invasores através da lista de entidades do `WorldChrMan`.

## 50. Dynamic In-Game Death Counter with Save Slot Persistence & F10 Controls
- Contador de mortes persistido por slot de salvamento com incremento automático em animações de morte e atalho <kbd>F10</kbd>.

## 51. Dedicated Discord Rich Presence ImGui Window & Granular Privacy/Customization Controls
- Janela de configurações dedicadas de privacidade do Discord no overlay (ocultar nível, área ou detalhes do personagem).

## 52. Dynamic Controller Layout Detection & In-Game Prompts Customization (PlayStation, Xbox, Nintendo Switch, Keyboard/Mouse)
- **Implementação**: `extra/management/prompts/bbport_button_prompts.{h,cpp}`.
- Detecção automática do tipo de controle conectado com substituição dinâmica de texturas de botões in-game.

## 54. Modularização Arquitetural em `extra/` e Arquitetura de Interceptação por Hooks (Clean Upstream Merges)
- Isolamento total de novas funcionalidades nas pastas de domínio `extra/` (`core/`, `ui/`, `management/`, `contact/`, `tools/`), limitando alterações em upstream a chamadas pontuais marcadas com `// [extra]`.

## 55. Modernização do Desassemblador x86-64 e Visualizador de Memória
- **Implementação**: `extra/core/debugger/symbol_manager.{h,cpp}`, `extra/ui/views/view_disasm.{h,cpp}`, `extra/ui/views/view_memviewer.{h,cpp}` com catálogo de 45+ símbolos e modos de tela cheia.
## 56. Correção do Botão "Copiar Regs" (Linux Wayland) e Interatividade
- Pipeline multi-camada de área de transferência (`SetClipboardSafe`) suportando Wayland e X11.
## 57. Resolução de Crash Crítico no Depurador e Limpeza de Botões Redundantes
- Correção de abort por Dear ImGui assert em `view_registers.cpp` e polimento da interface.
## 58. Implementação de Multiplayer Online Baseado no bbhost & The Hunter's Dream
- Suporte a servidores BBSC e The Hunter's Dream (THD) via autenticação Discord no navegador.
## 59. Reorganização Arquitetural em `extra/hosts/` e Ganchos Upstream
- Centralização dos subsistemas de rede em `extra/hosts/` com hooks desacoplados via `extra/hooks.h`.
## 60. Diagnóstico e Resolução de Erro de Sessão PSN e Ativação de Logs
- Mock correto de identificação de ticket PSN e inicialização de sessão de matchmaking sem exigir autenticação na PSN real.
## 61. Correção de Despacho Seguro de Callbacks NP e Provedor shadNet
- Isolamento de chamadas assíncronas do runtime de rede prevenindo deadlocks em callbacks de status de rede.
## 62. Resolução de Queda da Conexão Ingame (Game Server 20443)
- Injeção de pacotes keepalive periódicos na porta de sinalização 20443, evitando timeouts de conexão.

## 63. Resolução de Desconexão ao Interagir com Poças de Sangue / Fantasmas
- Mock de endpoints WebAPI de lista de amigos e bloqueios, impedindo desconexões ao carregar gravações de fantasmas no chão.
## 64. Correção e Empacotamento do Módulo Online (`libbbnet.so`) para Pacotes Distribuíveis (`package.py` e Docker)
- Ajuste de dependências dinâmicas no script de empacotamento para gerar builds distribuíveis autossuficientes.
## 65. Resolução Completa do Carregamento de Bibliotecas Empacotadas (`libfmt.so.10` / RPATH / LD_LIBRARY_PATH)
- Inclusão de `$ORIGIN` no RPATH dos binários gerados, garantindo carregamento confiável da `libfmt` sem conflitos com pacotes do sistema.
## 66. Resolução do Crash `Signal: 4 (ILL)` e da Janela Preta no Launcher (Retrocompatibilidade GTK4/Libadwaita)
- Shims de retrocompatibilidade para distros com versões legadas de GTK4 e libadwaita (como Ubuntu 22.04 LTS).
## 69. Correção do Scanner de Memória (Teto de Matches, Cobertura Completa do Heap PS4 e Vetorização)
- Teto de segurança para resultados de varredura, evitando sobrecarga de memória na interface gráfica ao escanear valores comuns.
## 70. Suporte à Tecla de Caminhar (Hold Walk / Meio Analógico) e Resolução de Invocação Online (BBSC / Sinos)
- Mapeamento de tecla dedicada para caminhar lentamente no teclado e correção de timeouts na troca de pacotes de sinos online.
## 71. Correção de Crash SIGSEGV no Overlay Gráfico (`RenderGraphicsSettings`) e Expansão de Localização PT-BR
- Realinhamento dos 11 efeitos em `kEffects` (`extra/management/i18n/bbport_strings.cpp`) adicionando o efeito `Bloom`, e guardas defensivas contra ponteiro nulo no ImGui.
## 72. Correção de Frame Pacing Simétrico (50/50) no Frame Generation e Desacoplamento de Ritmo
- Correção de cálculo de intervalo real entre inícios de quadros da engine e implementação de sleep híbrido de alta precisão (coarse sleep + spin-wait com `pause`), eliminando o gráfico em dente de serra (8ms/25ms) e estabilizando a entrega simétrica dos quadros interpolados e reais.

## 73. Upstream Touch Ledger (Registro de Modificações Fora de `extra/` Conforme `MyGUIDELINES.md`)

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `gpu/render/presenter.cpp` | `#include "extra/hooks.h" // [extra]` | `extra/hooks.h` | Re-export central de hooks | 2026-10-19 |
| `gpu/render/presenter.cpp` | `Extra::PresentPacer::OnPresentStart(); // [extra]` | `extra/core/frame_pacing/bbport_present_pacer.cpp` | Início de pacing de apresentação, cálculo de intervalo e reset de unpause | 2026-10-19 |
| `gpu/render/presenter.cpp` | `Extra::PresentPacer::OnRealFramePresented(); // [extra]` | `extra/core/frame_pacing/bbport_present_pacer.cpp` | Timestamp de frame real após apresentação | 2026-10-19 |
| `gpu/render/presenter.cpp` | `Extra::PresentPacer::SleepInterpolated(...); // [extra]` | `extra/core/frame_pacing/bbport_present_pacer.cpp` | Sono de pacing seguro com teto de 16.667ms | 2026-10-19 |
| `gpu/shim/bbport_frame_rate.cpp` | `#include "extra/hooks.h" // [extra]` | `extra/hooks.h` | Re-export central de hooks | 2026-10-19 |
| `gpu/shim/bbport_frame_rate.cpp` | `!Extra::PhotoPause::ShouldEnablePacing(...) // [extra]` | `extra/core/frame_pacing/bbport_photo_pause.cpp` | Ativação de hooks de simulação e suporte ao Modo Foto em qualquer taxa (>= 30 FPS) | 2026-10-19 |
| `gpu/shim/bbport_frame_rate.cpp` | `Extra::PhotoPause::HandleFrameStep(...) // [extra]` | `extra/core/frame_pacing/bbport_photo_pause.cpp` | Congelamento total da simulação (IA, mundo, animações) com live->dt = 0.0f | 2026-10-19 |
| `gpu/shim/bbport_frame_rate.cpp` | `cmpl/jle/vxorps detour // [extra]` em `HookRunSpeed` | (shim local) | Detour com guarda contra divisão por zero (`dt <= 0.001f`) | 2026-10-19 |
| `gpu/shim/bbport_overlay.cpp` | `if (!label) label = ""; // [extra]` em `Checkbox`/`Slider` | (shim local) | Guarda defensiva contra ponteiro nulo no ImGui | 2026-10-19 |
| `gpu/shim/bbport_overlay.cpp` | `if (event.key == SDLK_F10 ...) // [extra]` | (shim local) | Atalho global F10 / Pause para alternar congelamento com banner toast | 2026-10-19 |
| `gpu/bbnet20/vendor/shadps4/core/libraries/net/net_upnp.cpp` | `#if MINIUPNPC_API_VERSION >= 18 // [extra]` | (vendor compat) | Compatibilidade com miniupnpc legado (Ubuntu 22.04 LTS / glibc 2.35) | 2026-10-19 |
| `gpu/bbgpu.h` | `int bbgpu_input_blocked(void); int bbgpu_video_skip(void); // [extra]` | `extra/core/engine_fixes/bbport_video.cpp` | Consulta de bloqueio e pulo de vídeo ativo | 2026-10-19 |
| `gpu/shim/bbgpu.cpp` | `bbgpu_input_blocked() // [extra]` | `extra/core/engine_fixes/bbport_video.cpp` | Wrapper combinando InputBlocked e BbVideoTrack::IsActive | 2026-10-19 |
| `gpu/platform/avplayer/avplayer.cpp` | `BbVideoTrack::OnCreate/OnClose // [extra]` | `extra/core/engine_fixes/bbport_video.cpp` | Rastreamento do ciclo de vida de instâncias do AvPlayer | 2026-10-19 |
| `src/runtime_pad.c` | `extra_pad_apply_cutscene_skip(...) // [extra]` | `extra/management/prompts/bbport_pad_hook.h` | Emissão de BTN_OPTIONS com Esc/Enter e pulo de vídeo via AvPlayer::Stop | 2026-10-19 |
| `gpu/bbnet{,20}/include/core/memory.h` | `extra_query_memory_protection` hook // [extra] | `extra/core/debugger/mem_map.cpp` | Hook de consulta de mapa de memória para ponteiros estáticos do BBSC | 2026-10-19 |
| `gpu/bbnet{,20}/.../bloodborne_re.cpp` | save root, give guards // [extra: Teefpyre] | `extra/core/bbsc/bbport_bbsc_kit.cpp` | Raiz de saves AppImage ($BB_GPU_USER_DIR / $BB_USER_DIR) | 2026-10-20 |
| `gpu/bbnet{,20}/.../bbsc_memscan.cpp` | extra_bbsc_find_in_memory // [extra: Teefpyre] | `extra/core/bbsc/bbport_bbsc_kit.cpp` | Varredura segura guest backing / maps | 2026-10-20 |
| `gpu/bbnet{,20}/bbnet_glue.cpp` | extra_bbsc_setup_save_dir // [extra: Teefpyre] | `extra/core/bbsc/bbport_bbsc_kit.cpp` | Detecção dinâmica de saves em múltiplos title IDs | 2026-10-20 |
| `gpu/bbnet{,20}/.../http.cpp` | rotas de summon (/create, /get...) // [extra: Teefpyre] | (upstream inline) | Suporte a rotas encurtadas do BBSC e fix de stale summon | 2026-10-20 |
| `extra/core/bbsc/bbport_bbsc_kit.{h,cpp}` | `extra_bbsc_save_root`, `extra_bbsc_find_in_memory`, `extra_safe_read` | (novo, em `extra/`) | Raiz de saves, varredura contínua e leitura de memória segura contra SIGSEGV/SIGBUS | 2026-10-20 |
| `package.py` | `--appimage-extract-and-run` só se a ferramenta for AppImage | (ferramenta de empacotamento) | appimagetool estático falhava silenciosamente e o AppImage enchuto não era gerado | 2026-10-19 |

---

## 75. Pulo Universal de Cutscenes In-Engine e Vídeos Pré-Renderizados (AvPlayer) via Teclado
- **Causa**: O primeiro vídeo (`sprj_opening.mp4`) roda via `libSceAvPlayer` antes do Scaleform flow, fazendo `<Esc>` disparar `BTN_CIRCLE`.
- **Solução Arquitetural**: Módulo `BbVideoTrack` (`extra/core/engine_fixes/bbport_video.{h,cpp}`). Ao pressionar <kbd>Enter</kbd> ou <kbd>Esc</kbd>, `bbgpu_video_skip()` executa `player->Stop()` e desmascara `BTN_OPTIONS` (0x8).

## 76. Empacotamento de Módulos Online (`libbbnet20.so` e `libbbnet.so`) no Pacote Distribuível
- **Causa**: `package.py` só copiava `libbbnet.so`, ignorando `libbbnet20.so` exigido pelo BBPort 0.6+.
- **Solução**: `package.py` atualizado para detectar, empacotar e rastrear dependências dinâmicas de ambos os módulos com fallback em `run.sh`.

## 77. Discord Rich Presence (Client ID Padrão e Controles)
- **Causa/Solução**: ID apontava para shadPS4; atualizado para BBPort oficial (`1560890616811102219`) com controles no launcher e overlay.

## 78-83. Melhorias de Estabilidade, Compatibilidade BBSC e Empacotamento
- **78-80**: `MakeWritable` antes de escrita em `kCaCopy`/`kModelLodSite`; `BB_PREBUILT=1` consolidado em `AppRun`; validação de ponteiros do eboot sem re-leitura de `/proc/self/maps` em `mem_map.{h,cpp}`.
- **81-83**: Saves e hunter tools do BBSC no AppImage (`extra_bbsc_save_root`, `extra_bbsc_bell_scan_forever`); leitura segura contra SIGSEGV/SIGBUS em memória DMABUF via `extra_safe_read`; FFmpeg n6.1 minimal interno.

## 84. Integração das Correções BBSC Linux (Teefpyre) & Busca Direta via `SoloParamRepository`
- **Causas/Solução**: Falhas em invocações Seamless Co-op por rotas sem headers, stale summon, porta duplicada em `online.py` e queda de 60->15 FPS. Módulo `extra/core/bbsc/bbport_bbsc_kit.{h,cpp}` com resolução via `SoloParamRepository` direto (`BbscGetSoloParamRepository()`).

## 85-88. Hyprland/Wayland, Compatibilidade Steam e Correção de Duplicação no `bbport.ini`
- **85-87**: Seletor `BB_VIDEO_BACKEND=auto|x11|wayland` em `extra/core/video_backend/bbport_video_backend.{h,cpp}`; `GSK_RENDERER=cairo` sob `GDK_BACKEND=*x11*`; auto-detecção de XWayland no Hyprland (`AppRun`/launcher).
- **88**: Remoção de chamada duplicada `put("frame_generation")` em `gpu/shim/bbport_settings.cpp` e deduplicação em `Save()` e `save_ini()` (`bbport_config.py`).

## 89. Suporte a Touchpad PS4/PS5 (DualShock 4/DualSense) e Opção de Desativar Giroscópio
- **Causa**: SDL3 em Linux sem drivers HIDAPI expunha touchpad como mouse evdev (0 touchpads detectados). Clique no touchpad era mapeado apenas para lado esquerdo (gestos) e giroscópio acionava gestos indesejados durante o combate.
- **Solução Arquitetural**: Hints HIDAPI e enhanced reports em `window.cpp` e `runtime_pad.c`. Detecção de clique esquerdo (<0.5) e direito (>=0.5) em `extra/management/prompts/bbport_pad_hook.h` preservando toques físicos e menu Personal Effects. Checkbox "Desativar Giroscópio" no launcher e overlay via `BB_DISABLE_GYRO`/`disable_gyro`.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| gpu/shim/window.cpp | `BbVideoBackend::ApplyHint()`, `TraceRequest()`, `TraceCreated` | `extra/core/video_backend/bbport_video_backend` | Backend SDL configurável + trace | 2026-10-20 |
| gpu/vulkan/swapchain.cpp | `BbVideoBackend::SurfaceZeroExtent(zero_extent)` | idem | Reportar surface 0x0 no log | 2026-10-20 |
| run.sh | Configuração do backend: `BB_VIDEO_BACKEND`/`video_backend=` | idem (bash) | Decisão única de backend | 2026-10-20 |
| gpu/shim/bbport_settings.cpp | Remoção de put duplicado e deduplicação em Save() | (shim local) | Impedir acúmulo de frame_generation no ini | 2026-10-20 |
| src/runtime_pad.c | `extra_pad_process_touchpad`, `extra_pad_read_sensors` // [extra] | `extra/management/prompts/bbport_pad_hook.h` | Touchpad DS4/DualSense L/R + gyro toggle | 2026-10-21 |
| scripts/online.py | Sintaxe f-string Python 3.10 | (scripts) | Compatibilidade com PyInstaller / Python < 3.12 | 2026-10-20 |
## 90. Pin do SDL3 no Build Ubuntu/Docker (Detecção de Controles Flydigi Inconsistente)
- **Problema**: o AppImage nativo (pipeline Ubuntu 22.04) só listava o dock do Flydigi Apex 5, enquanto o AppImage Nix listava dock + controle — e cada rebuild do Docker podia resultar em comportamento diferente de gamepad.
- **Causa**: `packaging/Dockerfile.ubuntu22` compilava o SDL3 de `-b main` (branch móvel, ainda por cima sujeita ao cache de camadas do Docker — snapshots antigos de meses), enquanto o build Nix usa `pkgs.sdl3` do nixpkgs (release fixa). O SDL3 tem um driver HIDAPI dedicado Flydigi (`src/joystick/hidapi/SDL_hidapi_flydigi.c`, default ligado via `SDL_HINT_JOYSTICK_HIDAPI`, forçado a `"1"` em `gpu/shim/window.cpp` e `src/runtime_pad.c`) que reivindica o controle Flydigi e esconde a interface HID genérica (`SDL_HIDAPI_ShouldIgnoreDevice`); entre snapshots mudaram a detecção de identidade e o handshake de status do vendor (`data[10]`→`data[9]`, IDs APEX5 128/129), então um SDL antigo/novo falhava em adquirir o Apex 5 via dock e o controle sumia de `SDL_GetGamepads` (lista do launcher).
- **Solução**: SDL3 pinado em `ARG SDL_REF=release-3.4.18` (mesma tag do nixpkgs) no `Dockerfile.ubuntu22`; os dois pipelines passam a empacotar o mesmo SDL e os builds ficam reproduzíveis. Rebuild da imagem com `--no-cache` (ou mudar `SDL_REF`) para descartar snapshot antigo em cache.
- **Workaround para usuários afetados**: `SDL_JOYSTICK_HIDAPI_FLYDIGI=0` ao iniciar (desliga o driver Flydigi, volta ao caminho evdev genérico); no SDL novo, o driver exige a opção "Allow third-party apps to take over mappings" no app Flydigi Space Station para adquirir o controle.

## 91. Sincronização 1:1 de Brilho/Contraste (Display Pass 1D LUT) em Quadros Gerados (FSR 3.1)
- **Problema**: Ao alterar o ajuste de brilho nas opções do jogo, o Frame Generation apresentava flicker constante (60/120 Hz) alternando entre o brilho padrão e o configurado. O Bloodborne aplica a calibração de brilho por meio de um shader de Display Pass (`0x38d65b32`) que amostra uma 1D LUT (`sampler1DArray`) de 3 camadas (R, G, B), porém o FSR 3.1 interpolava a imagem antes desse pass.
- **Solução Arquitetural**: Conforme `MyGUIDELINES.md`, isolado integralmente em `extra/core/frame_gen/`. Adicionado compute pass `display_lut.comp` em `FrameGeneration::Interpolate` que aplica a 1D LUT diretamente no buffer interpolado `target->image` in-flight, sem alterar assinaturas do Presenter ou shaders nativos. O upstream recebe apenas um gancho de 1 linha em `raster_textures.cpp`.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `gpu/render/raster_textures.cpp` | `extra_fg_set_display_lut` // [extra] | `extra/core/frame_gen/vk_frame_generation.cpp` | Captura a view da 1D LUT do display pass durante bind de texturas | 2026-10-23 |

