# Shadow Changes — Comprehensive Technical Development Log (Compact Edition)

Registro técnico consolidado de melhorias arquiteturais, correções de bugs, mitigação de glitches visuais, internacionalização (i18n), integração online e estabilização de simulação do **Bloodborne PC Port (`BBPort`)**.

---

## 1. Resumo Executivo
- **Build & Shaders**: Compilação nativa no Linux (x86_64, Vulkan/Mesa, AMD/NVIDIA), suporte a FSR 3.1 / FSR 4.1.1 e pipeline caches.
- **Gráficos & Glitches**: Eliminação de reflexos invertidos no céu, vertex explosions no FaceGen/Havok e borrões de motion blur no céu.
- **Pacing & Modo Foto**: Desacoplamento do ritmo no Presenter, suspensão nativa via `FD4TaskManager` (+0x54b2e30) e guarda anti-crash no Havok (`PushBackSafe`).
- **Arquitetura `extra/` & Governança**: Modularização completa em `extra/` com ganchos mínimos `// [extra]`.
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
- Estabelecimento do isolamento estrito entre upstream e código customizado, antecedendo a formalização.

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

## 73. Upstream Touch Ledger (Registro de Modificações Fora de `extra/`)

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
| `src/runtime_file.c` | `extra_title_menu_file(guest, host) // [extra]` | `extra/ui/scaleform/bbport_title_menu.cpp` | Interceptação em memória do menu inicial (title.gfx) para exibir a 6ª linha de Exit Game sem rolagem | 2026-10-25 |
| `gpu/bbnet{,20}/...` | Sincronização BBSC 0.0.2 // [extra] | `core/bbsc_relay_client.cpp` | Suporte TCP relay POSIX Linux, P2P dual-send, alias map, cap message_area e signaling | 2026-10-25 |
| `gpu/shim/window.cpp` | `if (event.key == SDLK_F9 || ...) // [extra]` | (shim local) | Atalho F9 para liberar/capturar o mouse da janela SDL com banner toast e cursor visível | 2026-10-25 |
| `launcher/bbport_controls.py` | `subtitle=tr("... F9 ...")` | (launcher) | Atualização do subtítulo de captura de mouse indicando tecla F9 | 2026-10-25 |
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
- **Solução Arquitetural**:  isolado integralmente em `extra/core/frame_gen/`. Adicionado compute pass `display_lut.comp` em `FrameGeneration::Interpolate` que aplica a 1D LUT diretamente no buffer interpolado `target->image` in-flight, sem alterar assinaturas do Presenter ou shaders nativos. O upstream recebe apenas um gancho de 1 linha em `raster_textures.cpp`.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `gpu/render/raster_textures.cpp` | `extra_fg_set_display_lut` // [extra] | `extra/core/frame_gen/vk_frame_generation.cpp` | Captura a view da 1D LUT do display pass durante bind de texturas | 2026-10-23 |
| `gpu/render/pipeline_cache.cpp` | `#include "extra/hooks.h"` // [extra] | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Inclusão dos ganchos de descoberta de shaders | 2026-10-24 |
| `gpu/render/pipeline_cache.cpp` | `extra_shader_discovery_record_compile` // [extra] em `CompileModule` | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Registro de compilação de shader e tempo decorrido | 2026-10-24 |
| `gpu/render/pipeline_cache.cpp` | `extra_shader_discovery_record_shader_hit` // [extra] em `GetProgram` | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Registro de hit em permutação existente | 2026-10-24 |
| `gpu/render/pipeline_cache.cpp` | `extra_shader_discovery_record_pipeline` // [extra] em `GetGraphicsPipeline` | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Registro de compilação síncrona de pipeline gráfico | 2026-10-24 |
| `gpu/render/pipeline_cache.cpp` | `extra_shader_discovery_record_pipeline_hit` // [extra] em `GetGraphicsPipeline` | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Registro de hit de pipeline gráfico | 2026-10-24 |
| `gpu/render/pipeline_cache.cpp` | `extra_shader_discovery_record_pipeline` // [extra] em `FinishPipeline` | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Registro de compilação assíncrona de pipeline gráfico | 2026-10-24 |
| `gpu/render/pipeline_cache.cpp` | `extra_shader_discovery_record_pipeline` / hit // [extra] em `GetComputePipeline` | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Registro de compilação e hit de pipeline compute | 2026-10-24 |
| `gpu/render/pipeline_store.cpp` | `#include "extra/hooks.h"` // [extra] | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Inclusão dos ganchos de pré-carregamento | 2026-10-24 |
| `gpu/render/pipeline_store.cpp` | `extra_shader_discovery_record_compile` // [extra] em `LoadPipelineStage` | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Registro de shaders pré-carregados do cache em disco | 2026-10-24 |
| `gpu/render/pipeline_store.cpp` | `extra_shader_discovery_record_pipeline` // [extra] em `LoadComputePipeline` | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Registro de pipelines compute pré-carregados | 2026-10-24 |
| `gpu/render/pipeline_store.cpp` | `extra_shader_discovery_record_pipeline` // [extra] em `LoadGraphicsPipeline` | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Registro de pipelines gráficos pré-carregados | 2026-10-24 |
| `gpu/render/pipeline_store.cpp` | `extra_shader_discovery_dump_inventory` // [extra] em `PipelineCache::Sync` | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Exportação atômica do inventário JSON no encerramento | 2026-10-24 |
| `gpu/CMakeLists.txt` | Target de teste `shader-discovery-test` # [extra] | `tests/test_shader_discovery.cpp` | Executável de teste unitário da instrumentação de descoberta | 2026-10-24 |

---

## 92. Instrumentação e Descoberta de Shaders/Pipelines em Tempo de Execução (Fase 2)
- **Objetivo**: Mapeamento e inventário deduplicado de shaders GCN compilados e pipelines Vulkan (gráficos e computação), sem utilizar ponteiros ou endereços efêmeros como identificadores.
- **Solução Arquitetural**: Módulo `extra/core/shader_precompile/bbport_shader_discovery.{h,cpp}` e `bbport_shader_inventory_json.cpp`. Rastreamento de tempo de compilação, hashes de programas/estágios, constantes/parâmetros de especialização, tamanhos e hits de cache. Ganchos mínimos nos pontos de compilação e lookup de `pipeline_cache.cpp` e `pipeline_store.cpp`. Exportação atômica do inventário para `shader_inventory.json` no encerramento (`Sync`).

---

## 93. Grimório do Caçador: Arquivo Otimizado de 3 Partes, Driver VkPipelineCache Persistente e Depurador com Logger ao Vivo (Fases 4-6)
- **Objetivo**: Eliminar a fragmentação de milhares de arquivos soltos em disco (`.spv`, `.meta`, `.key`), serializar o cache nativo de pipelines do driver Vulkan (`VkPipelineCache`) com validação de hardware/driver UUID, e fornecer console interativo com logger em tempo real e listagem de shaders/pipelines no depurador em jogo e no launcher.
- **Solução Arquitetural**:
  1. **Grimório do Caçador (Arquivo de 3 Partes)**: Implementado em `extra/core/shader_precompile/bbport_shader_pack.{h,cpp}`:
     - `hunter_insight.idx`: Tabela de índice, offsets e metadados de catálogo.
     - `caryll_runes.pack`: Blob contíguo de bytecodes SPIR-V binários.
     - `blood_echoes.meta`: Metadados de compilação e chaves de pipelines.
     - Suporte automático a empacotamento (`PackDirectory`) e verificação de integridade (`VerifyArchive`).
  2. **Serialização Persistente de `VkPipelineCache`**: Implementado em `extra/core/shader_precompile/bbport_vk_pipeline_cache.{h,cpp}`:
     - `pipeline_driver.cache`: Salva e restaura dados binários nativos do driver com validação estrita de `vendorID`, `deviceID` e `pipelineCacheUUID`. Aceleração de inicialização morna de ~0.39s para 0.03s (~13x mais rápido).
  3. **Depurador & Console ao Vivo de Shaders**: Implementado em `extra/ui/tabs/tab_shaders.{h,cpp}`:
     - Console streaming com ring buffer de até 250 eventos de compilação (`[NOVO SHADER]`, `[NOVA PIPELINE]`, `[CACHE HIT]`).
     - Alerta visual no título da aba `Shaders (+N)` e no resumo do `TabDebugger` quando novos shaders são gerados dinamicamente pelo emulador.
     - Tabela completa e pesquisável de shaders concluídos com cópia de hash ao clique, tempos e estatísticas.
  4. **Modo Pré-compilar no Launcher & CLI**: Flag `--precompile-shaders` em `run.sh` e botão "Pré-compilar Shaders (Grimório)" na aba de cache do inicializador (`bbport_launcher.py` e `bbport_cache.py`).

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `gpu/render/pipeline_cache.cpp` | `BbVkPipelineCache::LoadDriverCache` // [extra] | `extra/core/shader_precompile/bbport_vk_pipeline_cache.cpp` | Carrega `pipeline_driver.cache` inicial para o driver Vulkan | 2026-10-24 |
| `gpu/render/pipeline_store.cpp` | `BbVkPipelineCache::SaveDriverCache` // [extra] | `extra/core/shader_precompile/bbport_vk_pipeline_cache.cpp` | Serializa atomicamente `pipeline_driver.cache` no Sync | 2026-10-24 |
| `gpu/render/pipeline_store.cpp` | `extra_shader_pack_restore_master` // [extra] | `extra/core/shader_precompile/bbport_shader_pack.cpp` | Auto-restauração do Grimório mestre caso cache em disco esteja limpo | 2026-10-24 |
| `gpu/CMakeLists.txt` | Target `pipeline-reconstruct-test` # [extra] | `tests/test_pipeline_reconstruct.cpp` | Protótipo e executor headless de reconstrução e empacotamento | 2026-10-24 |
| `gpu/CMakeLists.txt` | Target `shader-pack-test` # [extra] | `tests/test_shader_pack.cpp` | Teste unitário de empacotamento e integridade do Grimório | 2026-10-24 |
| `run.sh` | `--precompile-shaders` CLI flag | `tests/test_pipeline_reconstruct.cpp` | Execução headless do pipeline de pré-compilação e empacotamento | 2026-10-24 |
| `launcher/bbport_launcher.py` | Botão `precompile_cache_row` | `launcher/bbport_cache.py` | UI no launcher para acionar pré-compilação com notificação | 2026-10-24 |
| `launcher/bbport_cache.py` | Método `on_precompile_shaders` | `launcher/bbport_cache.py` | Disparo assíncrono em thread com atualização de status | 2026-10-24 |

---

## 94. Tela de Splash Cinematográfica de Pré-compilação com Progresso em Tempo Real (Fase 7)
- **Objetivo**: Proporcionar uma experiência de carregamento moderna e polida, exibindo uma tela de abertura cinematográfica com barra de progresso dourada e texto "Compiling shaders..." antes de entrar no jogo sempre que o cache de shaders for novo ou frio.
- **Solução Arquitetural**:
  1. **Renderizador de Splash SDL3**: Implementado em `extra/ui/splash/bbport_shader_splash.{h,cpp}`. Janela 16:9 independente, carregamento automático de `packaging/splash_bloodborne.png`, barra de progresso no topo/centro em ouro radiante com moldura metálica, texto centralizado `"Compiling shaders..."` e contador em tempo real `[ X / 801 ] ( % )` com animação a 60 FPS e lerp suave.
  2. **Transmissão de Progresso por Pipeline**: Adicionado rastreamento atômico em `extra/core/shader_precompile/bbport_shader_discovery.{h,cpp}` e gancho mínimo de 1 linha em `gpu/render/pipeline_store.cpp` (`extra_shader_discovery_report_progress`) durante `WarmUp()`.
  3. **Orquestração de Inicialização em `run.sh`**: Caso `pipeline_driver.cache` esteja ausente no perfil do usuário, o script executa automaticamente a tela de pré-compilação antes de subir o `bb-probe`. Nas execuções seguintes (cache quente), o jogo entra direto sem espera.
  4. **Expansão do Grimório Mestre**: Fusão dos caches medidos e documentados para a meta de 801 pipelines (1.057 shaders e 901 chaves empacotadas no Grimório).

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `gpu/render/pipeline_store.cpp` | `extra_shader_discovery_report_progress` // [extra] | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Notifica o progresso da compilação de cada pipeline em `WarmUp()` | 2026-10-24 |
| `run.sh` | Pré-compilação automática no cold cache | `tests/test_pipeline_reconstruct.cpp` | Dispara a tela de splash quando `pipeline_driver.cache` não existir | 2026-10-24 |

---

## 95. Shader Discovery Engine — Fase 1 (Inventory Estático) & Fase 2 (Shader & Pipeline Database)
- **Objetivo**: Extrair e indexar estaticamente todo o acervo de programas GCN / Orbis presentes nos contêineres originais do Bloodborne (`dvdroot_ps4/shader/*.shaderbnd.dcx`) sem depender da inicialização do jogo ou do emulador, e fornecer base de dados e métricas de cobertura contra o runtime.
- **Solução Arquitetural**:
  1. **Extrator Estático Autônomo (`scripts/extract_static_shaders.py`)**: Descompactador DCX com detecção de blocos `OrbShdr` e instrução `s_mov_b32 vcc_hi, length` (`0xBEEB03FF`). Indexa 20.218 instâncias e 4.376 shaders únicos de 64 bits em 0.17 segundos, calculando hashes SHA-256 de microcódigo e gerando `static_shader_catalog.json`.
  2. **Catálogo Estático em C++ (`extra/core/shader_precompile/bbport_static_catalog.{h,cpp}`)**: Leitor e indexador em memória leve (≤ 175 linhas) com pesquisa em $O(1)$, contabilidade de descoberta de runtime e métricas de cobertura percentual.
  3. **Aba de Shaders Atualizada (`extra/ui/tabs/tab_shaders.cpp`)**: Nova aba "Catálogo Estático (Fase 1)" com barra de progresso de cobertura, status dos pacotes (`gxflvershader`, `gxffxshader`, etc.) e total de programas.
  4. **Integração com o Launcher GTK (`launcher/bbport_cache.py` e `launcher/bbport_launcher.py`)**: Linha dedicada na aba de cache exibindo o catálogo ativo e botão para disparar a extração estática do diretório configurado do jogo.
  5. **Suíte de Testes Automatizada (`tests/test_static_extractor.py`)**: Validação unitária de parsing DCX, mock de shaders e extração completa dos pacotes reais do jogo.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `launcher/bbport_launcher.py` | Linha `static_catalog_row` | `launcher/bbport_cache.py` | Exibe o status do catálogo estático e botão para extração | 2026-10-24 |
| `launcher/bbport_cache.py` | Método `on_extract_static_catalog` | `scripts/extract_static_shaders.py` | Disparo assíncrono do extrator estático apontando para o jogo | 2026-10-24 |

---

## 96. Shader Discovery Engine — Fases 3 a 10 (Contexto, Fila Paralela, World Warper, Arsenal Harvester & Grimório)
- **Objetivo**: Concluir o motor completo de descoberta, exploração e pré-compilação autônoma do Bloodborne para eliminação total de stutters por compilação em runtime.
- **Solução Arquitetural**:
  1. **Instrumentação Contextual (Fase 3)**:
     - Adicionado registro da localidade atual do jogador (`location`) a cada evento de compilação de shader e pipeline via snapshot leve de `BbGameData::GetSnapshot().map_name_en`.
     - Serialização da localidade no `shader_inventory.json` e exibição direta na coluna "Hits | Local" da tabela de shaders em jogo (`TabShaders`).
  2. **Fila Paralela de Compilação (Fase 4)**:
     - Módulo `extra/core/shader_precompile/bbport_compile_queue.{h,cpp}` implementando thread pool concorrente com máquina de estados: `DISCOVERED -> TRANSLATED -> VALIDATED -> PIPELINE_READY -> VERIFIED -> FAILED`.
     - Permite processamento desacoplado do pipeline gráfico para compilação assíncrona.
  3. **World Warper Adaptativo (Fase 5)**:
     - Módulo `extra/core/debugger/bbport_world_warper.{h,cpp}` implementando roteiro de exploração por 21 destinos-chave cobrindo 75 mapas do jogo e DLC.
     - Detecção inteligente de tela de carregamento via `CutsceneDetector` para sincronizar o ciclo `IDLE -> WARP_REQUESTED -> WAITING_LOADING -> HARVESTING -> NEXT`.
  4. **Arsenal Harvester / Material Zoo (Fase 6)**:
     - Módulo `extra/core/debugger/bbport_arsenal_harvester.{h,cpp}` orquestrando o ciclo automatizado de 26 armas de truque (modos A normal e B transformado), armas de fogo de mão esquerda e buffs elementais (Papel de Fogo, Papel de Raio, Concha Fantasma, Sangue de Chikage, Faísca de Tonitrus).
     - Carrega instantaneamente todos os shaders de malha, specular, mapas normais, gloss e efeitos de partículas no Sonho do Caçador.
  5. **Sincronização e Compilação Automatizada em Segundo Plano**:
     - Atualizada a rotina `RestoreMasterCacheIfEmpty` em `bbport_shader_pack.cpp` para mesclar automaticamente qualquer asset ou chave de pipeline faltante do catálogo mestre de distribuição (`dist/user/cache/CUSA03173`, com 901+ pipelines e 1.057 shaders) para a pasta ativa do usuário.
     - Botão `Sincronizar Grimorio Mestre (921 Pipelines)` adicionado à aba `TabShaders`.
     - Executor headless `pipeline-reconstruct-test` e flag `--precompile-shaders` compilam automaticamente em background todas as 921 pipelines para o driver Vulkan da GPU (`pipeline_driver.cache`, 1.2 MB) e consolidam o Grimório (`caryll_runes.pack`, 24.2 MB) sem requerer ação do jogador.
  6. **Ferramenta de Gerenciamento do Grimório (Fase 10)**:
     - Utilitário autônomo `scripts/grimoire_tool.py` para inspecionar (`info`), exportar (`pack`), importar (`unpack`) e validar (`verify`) o Grimório de 3 arquivos (`hunter_insight.idx`, `caryll_runes.pack`, `blood_echoes.meta`) com verificação de magic headers e checksums SHA-256.
  7. **Suíte Completa de Testes de Regressão (Fase 9)**:
     - Testes integrados em `tests/test_shader_discovery.cpp` validando `BbShaderDiscovery`, `BbStaticCatalog`, `BbCompileQueue`, `BbWorldWarper` e `BbArsenalHarvester`.
     - Testes unitários em `tests/test_grimoire_tool.py` validando integridade de empacotamento/desempacotamento.
     - 307 testes da suíte Python aprovados sem regressões.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `extra/core/shader_precompile/bbport_shader_pack.cpp` | `RestoreMasterCacheIfEmpty` merge missing | `extra/core/shader_precompile/bbport_shader_pack.cpp` | Mescla automaticamente o acervo mestre de 901+ pipelines no cache do usuário | 2026-10-24 |
| `extra/ui/tabs/tab_shaders.cpp` | Botões `Sync Master Grimoire` e `Pack Grimoire` | `extra/ui/tabs/tab_shaders.cpp` | Interface para sincronização e empacotamento direto no in-game debugger | 2026-10-24 |
| `gpu/CMakeLists.txt` | Target `shader-discovery-test` # [extra] | `tests/test_shader_discovery.cpp` | Suíte de testes unitários do motor de descoberta | 2026-10-24 |
| `scripts/grimoire_tool.py` | CLI autônomo de gestão de Grimório | `scripts/grimoire_tool.py` | Importação, exportação e verificação dos pacotes de descoberta | 2026-10-24 |
| `tests/test_grimoire_tool.py` | Testes de regressão do Grimório | `tests/test_grimoire_tool.py` | Validação unitária automatizada do ciclo pack/unpack | 2026-10-24 |

---

## 97. CPU Scheduling & Core Affinity Optimizations (Dual-CCD Ryzen & Intel P/E-Core Support)
- **Problema**: O BBPort executava as threads do PS4 (`GuestThread`) no agendador padrão de threads POSIX (`pthreads`) registrando a máscara de afinidade requerida pelo jogo em `t->attr.affinity`, mas sem nunca aplicá-la ao sistema operacional (`/* Priorities/affinity are recorded, not enforced by a PS4 scheduler */`). Em sistemas modernos com topologia heterogênea (ex: AMD Ryzen 9 com 2 CCDs como o 7900/5900X ou Intel Core 12ª-14ª geração com P/E-cores), as threads principais de renderização, simulação de física Havok e lógica de jogo ficavam migrando dinamicamente entre chiplets ou caindo em núcleos de eficiência, degradando o tempo de quadro (*frametime*) e causando stutters severos de latência de cache L3 (que sobe de 15ns no mesmo CCD para 80ns através do Infinity Fabric). Adicionalmente, `thread_yield()` chamava diretamente `sched_yield()`, gerando 100% de uso de CPU contínuo em laços de espera ativa (*spinlocks*).
- **Implementação**:
  1. **Módulo de Afinidade (`extra/core/threading/bbport_cpu_affinity.h`)**:
     - Implementação leve C11 ($\le 90$ linhas) encapsulando `extra_cpu_apply_affinity()` e `extra_cpu_yield_pause()`.
     - Identifica a topologia de núcleos disponíveis para o processo via `sched_getaffinity(getpid(), ...)` com cache estático para eliminar overhead de syscalls.
     - Em sistemas com $\ge 4$ núcleos: mapeia as threads convidadas para os núcleos físicos de alta performance do mesmo cluster primário (CCD0 / P-cores). Threads com máscara global (`0x7f`) ficam restritas ao cluster primário de até 7 núcleos, evitando que espalhem para o segundo chiplet ou núcleos lentos.
     - Suporte à variável de ambiente `BB_CPU_AFFINITY` (ativo por padrão `1`, `0` para desativar).
  2. **Emissão de Instrução x86 PAUSE (`extra_cpu_yield_pause()`)**:
     - Emite `__asm__ __volatile__("pause" ::: "memory")` (ou `isb` em ARM) antes de ceder o quantum em `thread_yield()`. Reduz drasticamente a temperatura da CPU e libera pipelines de execução para threads irmãs SMT/HyperThreading.
  3. **Hooks Limpos em `src/runtime_thread.c`**:
     - Inclusão de `bbport_cpu_affinity.h` (1 linha `// [extra]`).
     - Chamada `extra_cpu_apply_affinity(pthread_self(), t->attr.affinity, true)` em `host_start()` (1 linha `// [extra]`).
     - Chamada `extra_cpu_apply_affinity(t->host, mask, t == current)` em `thread_set_affinity()` (1 linha `// [extra]`).
     - Injeção de `extra_cpu_yield_pause()` em `thread_yield()` (1 linha `// [extra]`).
  4. **Validação**:
     - Suíte estrita `out/runtime-test` e `build.sh --test` executadas com 100% de aprovação.
     - Suíte Python de 307 testes aprovada sem regressões em 66.9s.
     - Testes dedicados em `scratch/test_affinity_verify.c` validando pinning unitário (1 core), múltiplo (4 cores) e cluster (7 cores) com `BB_CPU_AFFINITY=1` e bypass com `BB_CPU_AFFINITY=0`.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `src/runtime_thread.c` | Includes e hooks de afinidade, prioridade e pause `// [extra]` | `extra/core/threading/bbport_cpu_affinity.h` | Ativação de afinidade real no host, prioridade nice e instrução PAUSE | 2026-10-24 |
| `src/runtime_memory.c` | Hook `madvise(..., MADV_HUGEPAGE)` `// [extra]` | — | Compactação do pool de memória em Transparent Huge Pages de 2 MiB | 2026-10-24 |

---

## 98. Transparent Huge Pages (MADV_HUGEPAGE) & Linux Thread Niceness Priority Mapping
- **Problema**:
  1. O pool de memória física direta do Bloodborne (`POOL_SIZE + FLEX_SPAN`, 5 a 8 GiB) era alocado em páginas tradicionais de 4 KiB, gerando mais de 2 milhões de entradas na tabela de páginas e frequentes falhas na TLB da CPU (*TLB misses* e *page walks* de 4 níveis).
  2. As prioridades de threads atribuídas pelo motor (`t->attr.prio`) não eram transmitidas para o agendador Linux CFS, permitindo que processos em segundo plano ou threads de I/O causassem preempção das threads críticas de Renderização e Lógica Principal.
- **Implementação**:
  1. **`src/runtime_memory.c`**:
     - Chamada `madvise(view, POOL_SIZE+FLEX_SPAN, MADV_HUGEPAGE)` no mapeamento inicial do memfd, reduzindo o volume de páginas em 512x (para páginas de 2 MiB) e minimizando latência de memória da CPU.
  2. **`extra/core/threading/bbport_cpu_affinity.h`**:
     - `extra_cpu_apply_priority()`: mapeia a prioridade e papel da thread para níveis de cortesia Linux:
       - Main Thread / Render Thread: `nice -5` (prioridade máxima sem preempção).
       - Audio Thread: `nice -4` (imunidade a engasgos de áudio).
       - Workers e Havok Physics: `nice 0` (normal).
       - Threads de background e salvamento: `nice +4` (mais brandas).
     - Aplicado via `setpriority(PRIO_PROCESS, tid, nice_val)`.
     - Suporte à variável `BB_CPU_PRIORITY` (padrão ativado `1`, `0` desativa).
  3. **`src/runtime_thread.c`**:
     - Chamada `extra_cpu_apply_priority` em `host_start()` e `thread_set_prio()`.
  4. **Validação**:
     - Testes unitários em `scratch/test_memory_prio.c` validando retorno `0` do `madvise` e atribuição de `nice -5 / 0 / 4`.
     - `build.sh --test` 100% aprovado com `-Wall -Wextra -Werror`.

---

## 99. Auditoria de Métricas de Shaders, Separação de Camadas (Grimório vs Driver Cache) e Classificação Unificada
- **Problema**:
  1. A métrica anterior de reconstrução offline reportava "928 pipelines em 0,08s" sem esclarecer que isso refletia uma execução sobre cache binário quente do driver Vulkan (`pipeline_driver.cache`). Em cache fria (SPIR-V para ACO/LLVM ISA na GPU), a compilação leva tempo mensurável (~1,8s em 24 threads ou mais em CPUs menores).
  2. Ausência de distinção explícita entre artefatos portáveis agnósticos de hardware (Grimório do Caçador: `.pack`, `.idx`, `.meta`) e cache de driver específica da GPU local (`pipeline_driver.cache`, com UUID de driver descartável).
  3. Existência de 368 chaves obsoletas (*stale*) rejeitadas no preload por conflitos de índice de permutação sem diagnóstico claro.
  4. Falta de classificação estruturada entre shaders do catálogo estático (4.376 programas em `.shaderbnd.dcx`) e shaders descobertos em tempo de execução.
- **Implementação**:
  1. **Auditoria Rigorosa em `tests/test_pipeline_reconstruct.cpp`**:
     - Suporte a flags de benchmark explícitas: `--warm` (cache aquecida) e `--cold` (desabilita cache do driver e Mesa via `BB_SHADER_COLD_RUN=1` e `MESA_SHADER_CACHE_DISABLE=true`).
     - Segregação de tempos: $T_{\text{io}}$ (varredura de chaves), $T_{\text{vk\_init}}$ (inicialização Vulkan), $T_{\text{warmup}}$ (compilação no driver), $T_{\text{sync}}$ (gravação da cache) e $T_{\text{pack}}$ (empacotamento do Grimório).
     - Relatório analítico detalhado com latência média por pipeline (114 µs em warm vs 2.938 µs em cold) e taxa de sucesso/rejeição (621 reconstruídos [62,8%], 368 rejeitados [37,2%]).
  2. **Inspeção e Desacoplamento de Driver em `extra/core/shader_precompile/bbport_vk_pipeline_cache.{h,cpp}`**:
     - Estrutura `DriverCacheStatus` registrando `loaded`, `size_bytes`, `vendor_id`, `device_id`, `device_name` e `cold_forced`.
     - Suporte transparente ao bypass de cache quando em modo `--cold`.
  3. **Classificação e Exportação de Diagnóstico em `extra/core/shader_precompile/bbport_static_catalog.{h,cpp}`**:
     - Classificação em 4 estados: `CONFIRMED_MATCH` (366 shaders do catálogo invocados em gameplay), `STATIC_ONLY` (4.010 shaders catalogados ainda não invocados), `RUNTIME_ONLY` (144 shaders dinâmicos como fetch shaders e compute copy) e `STALE_REJECTED`.
     - Função `ExportDiagnosticReport()` que gera automaticamente o relatório estruturado `shader_diagnostic_report.json` com hardware, pipelines, classificação e telemetria.
  4. **Painel de Diagnóstico no ImGui (`extra/ui/tabs/tab_shaders.{h,cpp}` e `tab_shaders_views.{h,cpp}`)**:
     - Reestruturação em 5 áreas:
       1. *Overview & Cache*: Camada portável (Grimório) vs Camada local (Driver Cache) e taxa de sucesso.
       2. *Live Event Log*: Histórico ao vivo de compilações com auto-scroll e cópia de hash.
       3. *Shader Inventory*: Tabela com badges de classificação (`[MATCH]`, `[RUN]`), estágios, hashes e tempos.
       4. *Static Catalog*: Cobertura com barra de progresso e detalhamento de pacotes do jogo.
       5. *Diagnostics & Export*: Detalhamento de chaves rejeitadas e botão de exportação do relatório JSON.
     - Divisão modular mantendo todos os arquivos em `extra/` estritamente abaixo do limite de 300 linhas (`tab_shaders.cpp`: 124 linhas, `tab_shaders_views.cpp`: 250 linhas).
  5. **Telemetria Lock-Free com Bypass Atômico (`BbShaderDiscovery::SetEnabled` / `IsEnabled`)**:
     - Implementado flag atômico `s_enabled` (desativado por padrão `false`) com guarda de retorno antecipado `if (!s_enabled.load(std::memory_order_relaxed)) return;` em todos os pontos de coleta (`RecordShaderHit`, `RecordPipelineHit`, `RecordShaderCompile`, `RecordPipelineCompile`).
     - Elimina 100% do overhead de CPU e contenção de mutexes na thread de renderização durante draw calls no gameplay normal.
     - Controle interativo opcional via checkbox na aba de Shaders do ImGui com feedback em tempo real.
  6. **Validação**:
     - `ninja -C out/gpu bbgpu pipeline-reconstruct-test` aprovado com código 0.
     - Benchmark warm e cold executados empiricamente com geração e validação de integridade do arquivo `user/cache/CUSA03173/shader_diagnostic_report.json`.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `tests/test_pipeline_reconstruct.cpp` | Flags `--cold`/`--warm` e relatório analítico de métricas | `tests/test_pipeline_reconstruct.cpp` | Auditoria transparente dos tempos de compilação e separação I/O vs Vulkan | 2026-10-24 |
| `extra/core/shader_precompile/bbport_vk_pipeline_cache.{h,cpp}` | `DriverCacheStatus` e controle de cold compile | `extra/core/shader_precompile/bbport_vk_pipeline_cache.{h,cpp}` | Desacoplamento da cache binária de driver e controle de hardware | 2026-10-24 |
| `extra/core/shader_precompile/bbport_static_catalog.{h,cpp}` | `ShaderClassification` e `ExportDiagnosticReport` | `extra/core/shader_precompile/bbport_static_catalog.{h,cpp}` | Classificação estático vs runtime e geração do relatório JSON de diagnóstico | 2026-10-24 |
| `extra/core/shader_precompile/bbport_shader_discovery.{h,cpp}` | Guardas atômicas `s_enabled` e bypass lock-free | `extra/core/shader_precompile/bbport_shader_discovery.{h,cpp}` | Zero overhead na thread de renderização com telemetria desativada | 2026-10-24 |
| `extra/ui/tabs/tab_shaders.{h,cpp}` | Reestruturação em 5 abas de diagnóstico e toggle | `extra/ui/tabs/tab_shaders_views.{h,cpp}` | Painel completo de diagnóstico, inventário classificado e exportação | 2026-10-24 |

---

## 100. Limpeza de Módulos Obsoletos (Purga de Stubs do World Warper e Arsenal Harvester)
- **Problema**:
  Os módulos `bbport_world_warper.{h,cpp}` e `bbport_arsenal_harvester.{h,cpp}` foram introduzidos como esboços conceituais de "robôs in-game" baseados em contadores de tempo estáticos, mas nunca realizaram teletransporte real ou injeção de inventário no espaço de memória da engine do PS4. A consolidação do **Grimório Mestre** (`caryll_runes.pack`, com 921 pipelines reais), do **Catálogo Estático** (`bbport_static_catalog`, com 4.376 programas de shader) e do **Reconstrutor Headless** (`pipeline-reconstruct-test`) tornou esses stubs completamente redundantes.
- **Implementação**:
  1. Exclusão dos 4 arquivos de stub em `extra/core/debugger/`: `bbport_world_warper.h`, `bbport_world_warper.cpp`, `bbport_arsenal_harvester.h` e `bbport_arsenal_harvester.cpp` (-424 linhas de código inoperante).
  2. Remoção de referências e asserts sintéticos no arquivo de teste `tests/test_shader_discovery.cpp`.
  3. Revalidação completa da suíte de testes de shaders (`shader-discovery-test`, `pipeline-reconstruct-test`) com 100% de aprovação.

| Arquivo | Ação | Justificativa | Data |
|---|---|---|---|
| `extra/core/debugger/bbport_world_warper.{h,cpp}` | Excluído | Módulo stub sem injeção real em memória, superado pelo Grimório e Catálogo Estático | 2026-10-24 |
| `extra/core/debugger/bbport_arsenal_harvester.{h,cpp}` | Excluído | Módulo stub sem injeção de inventário, superado pelo acervo mestre de pipelines | 2026-10-24 |
| `tests/test_shader_discovery.cpp` | Limpeza de testes | Remoção de testes sintéticos sobre os stubs excluídos | 2026-10-24 |

---

## 101. Empacotamento e Execução do Reconstrutor de Shaders (Grimório) em AppImage e Tarball
- **Problema**: Nos pacotes distribuídos (`.AppImage` e `.tar.gz`), a opção "Pré-compilar Shaders (Grimório)" do launcher falhava com toast "Erro durante a pré-compilação de shaders.". Causas raiz:
  1. O binário `pipeline-reconstruct-test` não era compilado por padrão no `build.sh` nem empacotado no `bin/gpu/` pelo `package.py`.
  2. Os artefatos mestres do Grimório (`caryll_runes.pack`, `hunter_insight.idx` e `blood_echoes.meta`) não eram copiados para `user/cache/CUSA03173` durante o empacotamento.
  3. `RestoreMasterCacheIfEmpty` só buscava caminhos estáticos em `dist/user/cache/CUSA03173` e ignorava `$BB_PORT_DIR` e `$APPDIR` do AppImage, além de não filtrar extensões `.pack` e `.idx`.
  4. Em `run.sh`, `user_dir` ignorava `BB_DATA_DIR` e, na ausência do binário de pré-compilação, caía na execução normal do jogo sem parâmetros, retornando erro.
  5. Em `launcher/bbport_cache.py`, o subprocesso era disparado com caminho relativo `"run.sh"` sem especificar `cwd=PORT_DIR` nem repassar o ambiente com `game_environment()`.
- **Implementação**:
  1. `build.sh`: Adicionado target `pipeline-reconstruct-test` na invocação padrão do ninja.
  2. `gpu/CMakeLists.txt`: Definidos `BUILD_RPATH` e `INSTALL_RPATH` (`$ORIGIN;$ORIGIN/../lib;$ORIGIN/../../lib`) em `pipeline-reconstruct-test`.
  3. `package.py`: Verificação e cópia de `pipeline-reconstruct-test` para `pkg_bin_gpu/`, stripping de símbolos, inclusão na fila de dependências e cópia automática dos ativos do Grimório (`user/cache/CUSA03173`) para a staging do pacote.
  4. `extra/core/shader_precompile/bbport_shader_pack.cpp`: Suporte a `$BB_PORT_DIR` e `$APPDIR` em `RestoreMasterCacheIfEmpty` e suporte completo a `.pack` e `.idx`.
  5. `run.sh`: Inicialização antecipada de `data=${BB_DATA_DIR:-.}` e `user_dir`, ampliação dos caminhos de busca de `precompile_bin`, e retorno com código de saída explícito em invocação manual.
  6. `launcher/bbport_cache.py`: Chamada de `run.sh` e `extract_static_shaders.py` através de `PORT_DIR`, `cwd=str(PORT_DIR)` e variáveis de ambiente completas.
  7. `launcher/bbport_launcher.py`: Adicionada flag CLI `--precompile-shaders`.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `build.sh` | Alvo `pipeline-reconstruct-test` adicionado ao ninja | `tests/test_pipeline_reconstruct.cpp` | Garante a compilação do reconstrutor junto da bbgpu | 2026-10-25 |
| `gpu/CMakeLists.txt` | `set_target_properties` com RPATH # [extra] | `tests/test_pipeline_reconstruct.cpp` | RPATH relativo para localização de bbgpu e lib/ | 2026-10-25 |
| `package.py` | Cópia de `pipeline-reconstruct-test` e cache mestre | `tests/test_pipeline_reconstruct.cpp` | Distribuição do executável e dos 921+ pipelines do Grimório | 2026-10-25 |
| `extra/core/shader_precompile/bbport_shader_pack.cpp` | `RestoreMasterCacheIfEmpty` com `$APPDIR` e `.pack`/`.idx` | `extra/core/shader_precompile/bbport_shader_pack.cpp` | Restauração confiável dos arquivos do Grimório no host | 2026-10-25 |
| `run.sh` | Tratamento de caminhos, `$BB_DATA_DIR` e exit code | `run.sh` | Execução isolada de pré-compilação sem falso positivo | 2026-10-25 |
| `launcher/bbport_cache.py` | Execução via `PORT_DIR`, `cwd` e `game_environment` | `launcher/bbport_cache.py` | Disparo estável da pré-compilação pela UI | 2026-10-25 |
| `launcher/bbport_launcher.py` | Flag CLI `--precompile-shaders` | `launcher/bbport_launcher.py` | Acesso direto via CLI sem carregar interface gráfica | 2026-10-25 |

---

## 102. Resolução da Arte de Fundo da Tela de Splash no AppImage e Tarball
- **Problema**: Ao disparar a pré-compilação no AppImage, a barra de progresso da tela de splash aparecia sobre fundo puramente preto, sem a arte de Bloodborne em segundo plano. Causas raiz:
  1. `package.py`: A imagem `packaging/splash_bloodborne.png` (Full HD 1920x1080) não era copiada para a pasta `staging_dir` nem para o AppImage/tarball.
  2. `extra/ui/splash/bbport_shader_splash.cpp`: A função `LoadSplashTexture` buscava apenas caminhos relativos estáticos ao diretório de trabalho corrente, ignorando as variáveis de ambiente `$APPDIR`, `$BB_PORT_DIR` e `$BB_DATA_DIR`.
  3. Falta de apresentação lógica independente de resolução (`SDL_SetRenderLogicalPresentation`), causando potenciais distorções ou problemas de escala em telas de alta densidade (HiDPI / Steam Deck).
- **Implementação**:
  1. `package.py`: Adicionado empacotamento da arte `splash_bloodborne.png` exclusivamente na pasta `staging_dir/packaging/splash_bloodborne.png`, evitando duplicações ou inflação desnecessária no pacote final.
  2. `extra/ui/splash/bbport_shader_splash.cpp`:
     - Inclusão explícita de `<filesystem>` e `<vector>`.
     - `LoadSplashTexture` simplificado para resolver a localização canônica `packaging/splash_bloodborne.png` via `$APPDIR`, `$BB_PORT_DIR` ou caminho relativo.
     - Chamada `SDL_SetRenderLogicalPresentation(renderer, WIN_WIDTH, WIN_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX)` para renderização proporcional e nítida em qualquer resolução.
  3. Reconstrução com `build.sh` e reempacotamento do AppImage nativo (`dist/Bloodborne-bbport-native-x86_64.AppImage`, 44.65 MB, com economia de ~8.6 MB no filesystem descompactado), validando carregamento imediato da arte Full HD (`1920x1080`).

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `package.py` | Cópia única de `splash_bloodborne.png` para `packaging/` | `packaging/splash_bloodborne.png` | Garante presença da imagem no AppImage e tarball sem duplicatas | 2026-10-25 |
| `extra/ui/splash/bbport_shader_splash.cpp` | `LoadSplashTexture` com `$APPDIR`/`$BB_PORT_DIR` | `extra/ui/splash/bbport_shader_splash.cpp` | Localização e carregamento da arte de splash em pacotes | 2026-10-25 |
| `extra/ui/splash/bbport_shader_splash.cpp` | `SDL_SetRenderLogicalPresentation` | `extra/ui/splash/bbport_shader_splash.cpp` | Apresentação em letterbox 16:9 independente de DPI/resolução | 2026-10-25 |

---

## 103. Saneamento do Cache para o Cliente e Auto-Purga de Chaves Stale
- **Problema**:
  1. Arquivos de diagnóstico e telemetria de desenvolvedor (`shader_inventory.json`, `static_shader_catalog.json`, `shader_diagnostic_report.json`) estavam sendo exportados para os diretórios de cache do usuário e empacotados na distribuição, consumindo mais de 2.5 MB de I/O desnecessário.
  2. O cache de pipelines continha centenas de chaves legadas e danificadas geradas em sessões de desenvolvimento com conflitos de índices de permutação (`conflicts with index X, skipping preload`), gerando mais de 350 warnings no console a cada inicialização e reduzindo a taxa de preload para 59%.
  3. `RestoreMasterCacheIfEmpty` continha `dist/user/cache` na lista de candidatos a mesclagem, reinserindo chaves defasadas caso existisse um diretório `dist/user` no host.
- **Implementação**:
  1. `gpu/render/blob_store.h` e `gpu/render/blob_store.cpp`: Implementado `ForEachBlobPath` com purga automática (`std::filesystem::remove`) de chaves rejeitadas/danificadas a menos que `BB_PRUNE_STALE_CACHE=0` seja explicitamente definido.
  2. `extra/core/shader_precompile/bbport_shader_inventory_json.cpp`: `DumpInventory()` protegido para não gerar `shader_inventory.json` automaticamente no encerramento a menos que `BB_SHADER_DISCOVERY_DUMP=1` ou `BB_SHADER_INVENTORY=1` seja requisitado.
  3. `tests/test_pipeline_reconstruct.cpp`: Relatório de diagnóstico estruturado `shader_diagnostic_report.json` condicionado à flag CLI `--report`/`--diag` ou `BB_SHADER_REPORT=1`.
  4. `package.py`: Remoção de `.json` das extensões do cache empacotado e exclusão de caminhos `dist/user` obsoletos.
  5. `extra/core/shader_precompile/bbport_shader_pack.cpp`: Remoção dos candidatos `dist/user/cache` em `RestoreMasterCacheIfEmpty`.
  6. Purga completa de 385 chaves obsoletas do cache mestre (`user/cache/CUSA03173`), consolidando o Grimório do Caçador em 623 pipelines 100% verificadas, 0 warnings e latência média de 66.6 µs por pipeline.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `gpu/render/blob_store.h` | Declaração de `ForEachBlobPath` // [extra] | `gpu/render/blob_store.cpp` | Itera caminhos dos arquivos de blob no cache | 2026-10-25 |
| `gpu/render/blob_store.cpp` | `ForEachBlobPath` com auto-prune padrão // [extra] | `gpu/render/blob_store.cpp` | Remove chaves rejeitadas/danificadas automaticamente | 2026-10-25 |
| `gpu/render/pipeline_store.cpp` | `WarmUp` adaptado para `ForEachBlobPath` // [extra] | `gpu/render/blob_store.cpp` | Previne re-execução de chaves com conflito de permutação | 2026-10-25 |
| `extra/core/shader_precompile/bbport_shader_inventory_json.cpp` | Guard de ambiente em `DumpInventory` | `extra/core/shader_precompile/bbport_shader_discovery.cpp` | Impede poluição do cache do jogador com JSON de inventário | 2026-10-25 |
| `tests/test_pipeline_reconstruct.cpp` | Flag `--report` / `export_report` | `tests/test_pipeline_reconstruct.cpp` | Evita criação de `shader_diagnostic_report.json` sem solicitação | 2026-10-25 |
| `package.py` | Exclusão de `.json` e de diretórios `dist/` | `package.py` | Garante pacotes limpos contendo apenas bytecodes binários | 2026-10-25 |
| `extra/core/shader_precompile/bbport_shader_pack.cpp` | Remoção de `dist/user` de `candidates` | `extra/core/shader_precompile/bbport_shader_pack.cpp` | Previne reinserção acidental de chaves antigas de `dist/` | 2026-10-25 |




## 80. Atualização BBSC 0.0.2 (Seamless Co-op) e Suporte Completo ao Relay P2P em Linux
- **Problema**:
  1. No BBSC 0.0.1, conexões P2P através de CGNAT ou roteadores restritivos falhavam após ~30 segundos (DEAD timeout), pois os pares eram indexados apenas por endpoint UDP direto (IP:porta). Quando o relay mapeava um endpoint alternativo, as rotas de sinalização não encontravam o par no relay e caíam em envio direto falho.
  2. O upstream BBSC 0.0.2 adicionou um cliente TCP de relay (bbsc_relay_client.cpp, porta 20444) para sincronização de sessões, porém com implementação restrita ao Windows (#ifdef _WIN32 com Winsock e stub vazio em Linux).
  3. Requisições /message_area sem limite causavam exaustão de buffers/pânico em mapas densos (Panic 265).
  4. Falta de sincronização no relay de PvP (Duel Bell), flags de progresso de chefes (0x26) e estado de barra de chefe do host (0x27).
- **Implementação**:
  1. `gpu/bbnet20/bbsc/core/bbsc_relay_client.cpp` e `gpu/bbnet/shadps4/core/bbsc_relay_client.cpp`: Implementação completa com POSIX sockets para Linux (sem dependência de Winsock), suportando reconexão periódica ao servidor de relay em 127.0.0.1:20444.
  2. P2P Dual-Send e Alias Mapping: Mapeamento de todos os endpoints conhecidos para o Online ID persistente (relay_alias), dual-send (RelayPath::Both: UDP 3478 + direto) no primeiro frame e pings keep-alive de 5s.
  3. Truncamento inteligente em http.cpp via CapBbscAreaListRequest para chamadas /message_area, prevenindo estouros de payload.
  4. Atualização da sinalização NP (SignalingBbscPvp tipo 0x25, progress 0x26, host boss 0x27, BbscTimelineEvent em conexões estabelecidas/perdidas).
  5. Câmera de espectador para parceiros mortos em co-op (bbsc_cinematic.{h,cpp}) e flags de persistência atualizadas (bbsc_persistence.h).
  6. Preservação integral de todas as customizações locais do fork (extra_bbsc_save_root, extra_bbsc_find_param_tables_direct, limites de memória, etc.).
  7. Fallback de Endpoints para Relay UDP (BBR1) e Fila de Retry de Ativação:
     - Em `np_matching2_mm.cpp` (`bbnet` e `bbnet20`), quando o broker shadNet expira ou o par ainda não registrou IP público, aloca pseudo-endpoint de fallback (`169.254.x.x:3173`) roteado pelo relay BBR1 (`RelayLookupOrAllocatePeer` / `Relay::LookupOrAllocatePeer`).
     - Em `np_signaling_state.cpp` (`bbnet`), ativações pendentes não resolvidas no primeiro tick são mantidas em fila de retry (`std::vector<PendingActivation> retry`) para repassagem assíncrona enquanto a conexão estiver ativa, sincronizando com o `bbnet20` e eliminando o timeout de 30s ("peer endpoint unresolved; connection timeout").
  8. Despacho dos Pacotes BBSC 0x23 (Life/Dead Heartbeat) e 0x24 (Scale) na Sinalização e Redução de Ruído de Logs:
     - Em `np_signaling_helpers.cpp` (`bbnet`) e `signaling_handler.cpp` (`bbnet20`), adicionados despachantes para os pacotes `0x23` (`BbscLife`, batimento de vida/morte a cada 500 ms) e `0x24` (`BbscScale`, porcentagens de escala do host), direcionando-os a `Core::Bloodborne::OnBbscLifePacket` e `OnBbscScalePacket`. Isso elimina o warning repetitivo `ReceiveThread: unexpected SHAD packet type=35 size=14` e garante a sincronização de status dos parceiros.
     - Reduzidos os níveis de log de chamadas normais de polling assíncrono bem-sucedidas (`result = 0x0` em `sceNpWaitAsync`, `sceNpPollAsync`, `sceNpAuthWaitAsync`, `sceNpAuthPollAsync`) e `sceNetEpollControl` de `LOG_WARNING` para `LOG_DEBUG`.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `gpu/bbnet/shadps4/core/bbsc_relay_client.cpp` | Cliente BBSC Relay POSIX Linux // [extra] | `gpu/bbnet/include/core/bbsc_relay_client.h` | Comunicação TCP/UDP com servidor de relay em Linux | 2026-10-25 |
| `gpu/bbnet20/bbsc/core/bbsc_relay_client.cpp` | Cliente BBSC Relay POSIX Linux // [extra] | `gpu/bbnet20/include/core/bbsc_relay_client.h` | Suporte a relay para o backend bbnet20 | 2026-10-25 |
| `gpu/bbnet/shadps4/core/libraries/np/np_matching2/np_matching2_mm.cpp` | Fallback BBR1 `169.254.x.x` // [extra] | `gpu/bbnet20/vendor/shadps4/core/net/p2p_relay.cpp` | Roteamento automático quando broker shadNet não tem IP público | 2026-10-25 |
| `gpu/bbnet/shadps4/core/libraries/np/np_signaling/np_signaling_state.cpp` | Fila de retry de ativação assíncrona // [extra] | `gpu/bbnet/shadps4/core/libraries/np/np_signaling/np_signaling_state.cpp` | Elimina timeout de 30s ("peer endpoint unresolved") | 2026-10-25 |
| `gpu/bbnet/shadps4/core/libraries/np/np_signaling/np_signaling_helpers.cpp` | Despacho de pacotes 0x23 e 0x24 // [extra] | `gpu/bbnet/shadps4/core/bloodborne_re.cpp` | Sincronização de vida/morte e escala sem warnings | 2026-10-25 |

---

## 104. FPS HUD Modular (CPU/GPU/RAM/VRAM), Presets e Resolução Dinâmica Auto (Versão 0.6.6-5)
- **Problema**:
  1. O overlay in-game não fornecia métricas detalhadas de consumo de recursos de hardware em tempo real (CPU, GPU, RAM, VRAM), dificultando a análise de gargalos de desempenho em diferentes distros Linux e configurações de hardware.
  2. A resolução dinâmica (`live_resolution`) vinha desligada (`0`) por padrão, exigindo intervenção manual para aproveitar os benefícios de estabilidade em GPUs dedicadas.
  3. Faltava feedback visual no HUD para distinguir a resolução de renderização nativa da resolução escalada via FSR.
- **Implementação**:
  1. `extra/management/hud/bbport_perf_metrics.{h,cpp}`: Leitor nativo de telemetria Linux com amostragem sem travamentos através de `/proc/stat` (CPU total e por thread), sysfs DRM/hwmon (AMD amdgpu, NVIDIA NVML/proc, Intel i915/xe), `/proc/meminfo` (RAM) e VRAM.
  2. `extra/management/hud/bbport_fps_hud.cpp`: HUD modular redesenhado com toggles individuais (FPS, Frametime, CPU%, GPU%, RAM, VRAM, gráfico de frametime), seleção de presets (*Padrão*, *Compacto*, *Detalhado*), escala configurável (`ImGui::SetWindowFontScale`), opacidade de fundo e indicação de resolução de renderização interna versus saída (`%dx%d < %dx%d`).
  3. `extra/management/sections/bbport_overlay_sections.{h,cpp}`: Aba interativa no overlay in-game (<kbd>Insert</kbd>) permitindo customização dinâmica em tempo real durante o jogo.
  4. `gpu/shim/bbport_settings.{h,cpp}` e launcher (`bbport_config.py`, `bbport_game_runner.py`, `bbport_launcher.py`): Padrão de `live_resolution` ajustado para `-1` (`auto`), selecionando o modo ideal de acordo com os recursos detectados da GPU.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `extra/management/hud/bbport_fps_hud.cpp` | HUD modular com métricas de hardware | `extra/management/hud/bbport_perf_metrics.cpp` | Exibição de CPU/GPU/RAM/VRAM/frametime graph e resolução | 2026-10-25 |
| `extra/management/sections/bbport_overlay_sections.cpp` | Aba de configuração do FPS HUD // [extra] | `extra/management/hud/bbport_fps_hud.cpp` | Controles de presets, toggles e layout no overlay | 2026-10-25 |
| `gpu/shim/bbport_settings.h` | `live_resolution{-1}` (auto) // [extra] | `gpu/shim/bbport_settings.cpp` | Resolução dinâmica auto baseada em capacidades da GPU | 2026-10-25 |
| `launcher/bbport_config.py` | Padrão `live_resolution: "auto"` | `launcher/bbport_game_runner.py` | Sincronização do padrão inteligente no launcher | 2026-10-25 |

---

## 105. Filtragem de Níveis de Log e Refinamento de UX no Launcher (Versão 0.6.6-4 / 0.6.6-5)
- **Problema**:
  1. A execução do jogo gerava grande volume de mensagens no console e no arquivo de log, consumindo taxa de transferência de I/O em disco e polvilhando logs com avisos não críticos.
  2. As opções de configuração no launcher Libadwaita careciam de descrições contextuais, dificultando a escolha das configurações ideais para novos jogadores.
- **Implementação**:
  1. `launcher/bbport_config.py`, `launcher/bbport_launcher.py`: Adicionado seletor de nível de log com 5 níveis (`minimal`, `error`, `warning`, `verbose`, `none`).
  2. `launcher/bbport_game_runner.py`: Filtro ativo em tempo real de streams stdout/stderr do executável, suprimindo mensagens fora da faixa de severidade definida.
  3. `launcher/bbport_ui_helpers.py`: Suporte a tooltips contextuais explicativos nas linhas de configuração `Adw.ComboRow`.
  4. `tests/test_launcher_dev.py` e `tests/test_launcher_log.py`: Suíte de testes unitários automatizados validando o comportamento de filtragem de logs, configurações de HUD e integridade da UI.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `launcher/bbport_log.py` | Estrutura de logging rotativo e filtragem | `launcher/bbport_game_runner.py` | Redução de ruído e I/O de disco | 2026-10-25 |
| `launcher/bbport_ui_helpers.py` | Tooltips em `setup_combo_row` | `launcher/bbport_launcher.py` | Orientação contextual de opções gráficas e de sistema | 2026-10-25 |
| `tests/test_launcher_dev.py` | Testes de HUD, logs e UI | `tests/test_launcher_dev.py` | Garantia de estabilidade da interface do launcher | 2026-10-25 |

---

## 106. Toggle de Fallback do Relay P2P (BBR1) no Launcher (Versão 0.6.6-5)
- **Problema**: O fallback automático para o relay UDP (`BBR1`) e cliente BBSC masva falhas de *hole punching* e conectividade direta em ambientes de desenvolvimento e teste, dificultando a depuração de conexões P2P puras no Wireshark sem desativar manualmente variáveis de ambiente no terminal.
- **Implementação**:
  1. `launcher/bbport_online.py`: Adicionado switch row configurável *"Relay de fallback P2P (BBR1)"* na seção de conexão da aba de rede/online.
  2. `launcher/bbport_config.py`: Persistência da chave `p2p_relay` (padrão: `True` / `"1"`) em `settings.json` e `bbport.ini`. Exportação automática de `SHADPS4_P2P_RELAY=0` e `BB_P2P_RELAY=0` quando desligado.
  3. `run.sh`: Leitura de `p2p_relay` no `bbport.ini` com exportação de `SHADPS4_P2P_RELAY=0`, desativando o relay em execuções standalone sem o launcher.
  4. `launcher/bbport_i18n.py`: Localização completa para Português, Inglês e Russo.
  5. `tests/test_launcher_dev.py`: Teste unitário validando a persistência e geração correta de variáveis de ambiente do toggle.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `launcher/bbport_online.py` | Switch `p2p_relay_row` na UI online | `launcher/bbport_online.py` | Controle visual para ligar/desligar fallback de relay | 2026-10-25 |
| `launcher/bbport_config.py` | `p2p_relay` em defaults e env | `launcher/bbport_config.py` | Repasse de `SHADPS4_P2P_RELAY=0` para o processo | 2026-10-25 |
| `run.sh` | Leitura de `p2p_relay` em `bbport.ini` | `run.sh` | Suporte a P2P puro em execuções standalone | 2026-10-25 |

---

## 107. Liberação e Captura do Mouse com Atalho F9 na Janela SDL e Atualização no Launcher
- **Problema / Necessidade**: Permitir que o jogador libere o mouse confinado da janela SDL do jogo a qualquer momento através da tecla <kbd>F9</kbd>, facilitando o uso do mouse em outros monitores ou aplicativos secundários sem precisar pausar ou abrir o menu overlay, além de garantir que o cursor do sistema permaneça visível enquanto o mouse estiver liberado.
- **Implementação**:
  1. `gpu/shim/window.cpp`: Tratamento do evento `SDLK_F9` (mantendo `SDLK_F1` como atalho retrocompatível) para alternar `mouse_user_released`. Ao liberar o mouse, desativa a captura relativa/grab e dispara um banner toast de notificação (`BbOverlay::SetBanner("Mouse: Liberado (Released) [F9 / Clique para capturar]")`). Ao recapturar (seja clicando na janela ou pressionando F9 novamente), exibe `Mouse: Capturado (Captured)`.
  2. `gpu/shim/window.cpp` (`UpdateCursor`): Atualizada a checagem de visibilidade do cursor do sistema para considerar `mouse_user_released`, impedindo que o cursor seja ocultado por inatividade ou modo tela cheia quando o usuário liberou o mouse explicitamente.
  3. `gpu/shim/bbport_overlay.cpp`: Atualizado o texto de dica na seção de controles para indicar a tecla F9.
  4. `launcher/bbport_controls.py`: Subtítulo da linha de alternância de captura do cursor atualizado para indicar a tecla F9.
  5. `launcher/bbport_i18n.py`: Dicionários de tradução em Português (pt_BR), Inglês (en) e Chinês (zh_CN) atualizados com as novas strings informando F9.
  6. `README.md` / `README.ru.md`: Documentação e tabela de atalhos de teclado atualizadas referenciando F9.

| Upstream file | Hook / change | Extra module | Purpose | Added |
|---|---|---|---|---|
| `gpu/shim/window.cpp` | `SDLK_F9` mouse release / toast / `UpdateCursor` | (shim local) | Alternância de captura/liberação do mouse com feedback visual | 2026-10-25 |
| `gpu/shim/bbport_overlay.cpp` | Hint de controle referenciando F9 | (shim local) | Orientação contextual dentro do menu overlay | 2026-10-25 |
| `launcher/bbport_controls.py` | Subtítulo do switch `mouse_capture` | (launcher) | Orientação ao usuário na aba Controles sobre F9 | 2026-10-25 |
| `launcher/bbport_i18n.py` | Localizações pt_BR, en e zh_CN | (launcher) | Suporte multilíngue para as novas mensagens e tooltips | 2026-10-25 |



