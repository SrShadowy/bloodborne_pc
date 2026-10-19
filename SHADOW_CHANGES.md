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

## 6. Verification & Test Matrix
- Validação contínua através de testes automatizados (`settings-test`, `motion-history-test`, `ui-composition-test`, `python3 -m unittest discover tests/`) e builds de release com LTO (`bash build.sh`).

## 7. Quick Start Reference
- Inicialização via Launcher GTK4: `bash launcher/bb-launcher.sh`.
- Inicialização direta CLI: `bash run.sh`.
- Acesso ao overlay in-game: Tecla <kbd>Insert</kbd> ou controle <kbd>L3</kbd> + <kbd>R3</kbd>.

## 8. Sky Streaks Investigation: Diagnostic Trials & Attempted Solutions
- Testes diagnósticos isolando shaders de pós-processamento (DoF, Blur, SSAO) e drivers AMD RDNA 4 (RADV HiZ/DCC), confirmando que anomalias visuais no céu decorriam do mapa de velocidade (velomap) do Yebis.

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

## 55. Modernização do Desassemblador x86-64 e Visualizador de Memória: Resolução de Símbolos, Seleção e Cópia Interativa, Inspeção de Registradores e Modos de Tela Cheia
- **Implementação**: `extra/core/debugger/symbol_manager.{h,cpp}`, `extra/ui/views/view_disasm.{h,cpp}`, `extra/ui/views/view_memviewer.{h,cpp}`.
- Catálogo de 45+ símbolos do retail 1.09, anotação automática de operandos, seleção de linhas com realce colorido, cópia com <kbd>Ctrl+C</kbd> e modos de tela cheia (Dividido, Apenas Disasm, Apenas Hex).

## 56. Correção do Botão "Copiar Regs" (Linux Wayland), Interatividade por Registrador e Roadmap Priorizado do Memory Viewer
- Pipeline multi-camada de área de transferência (`SetClipboardSafe`) suportando Wayland e X11, feedback visual no clique e menu de contexto interativo em cada célula de registrador.

## 57. Resolução de Crash Crítico no Depurador e Limpeza Geral dos Botões Redundantes da Interface
- Correção de abort por Dear ImGui assert em `view_registers.cpp` e remoção de botões redundantes na navegação de memória.

## 58. Implementação de Multiplayer Online Baseado no bbhost & The Hunter's Dream (Fases 0, 1 e 2)
- Suporte a servidores BBSC e The Hunter's Dream (THD) via autenticação Discord no navegador com polling em segundo plano.

## 59. Reorganização Arquitetural em `extra/hosts/`, Conformidade com `MyGUIDELINES.md` e Ganchos Upstream Não-Invasivos
- Centralização dos subsistemas de rede em `extra/hosts/` com hooks desacoplados via `extra/hooks.h`.

## 60. Diagnóstico e Resolução de Erro de Sessão PSN ("Não tens sessão iniciada") e Ativação de Logs
- Mock correto de identificação de ticket PSN e inicialização de sessão de matchmaking sem exigir autenticação na PSN real.

## 61. Correção de Despacho Seguro de Callbacks NP, Proteção de Ponteiros Nulos e Desacoplamento do Provedor shadNet
- Isolamento de chamadas assíncronas do runtime de rede prevenindo deadlocks em callbacks de status de rede.

## 62. Resolução de Queda da Conexão Ingame (Game Server 20443, Injeção de Headers e Keepalive de Sinalização)
- Injeção de pacotes keepalive periódicos na porta de sinalização 20443, evitando timeouts de conexão em sessões prolongadas.

## 63. Resolução de Desconexão ao Interagir com Poças de Sangue / Fantasmas (Mock de NP WebAPI para blockList/friendList e Sincronização de PlayLogs)
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
| `tests/test_settings.cpp` | Validação de labels não nulos // [extra] | (teste unitário) | Teste de regressão para integridade de todos os 11 efeitos em todos os idiomas | 2026-10-19 |

---

## 75. Pulo Universal de Cutscenes In-Engine e Vídeos Pré-Renderizados (AvPlayer) via Teclado
- **Causa**: O primeiro vídeo (`sprj_opening.mp4` / `sprj_advertise.mp4`) é reproduzido pelo `libSceAvPlayer`, antes da inicialização das estruturas de Scaleform menu flow (`kMenuFlow`), fazendo com que `bbgpu_input_blocked()` retornasse 0 e o `apply_keyboard` desviasse o <kbd>Esc</kbd> para `BTN_CIRCLE`.
- **Solução Arquitetural**: Módulo `BbVideoTrack` (`extra/core/engine_fixes/bbport_video.{h,cpp}`) monitorando o ciclo de vida do player. Ao pressionar <kbd>Enter</kbd> ou <kbd>Esc</kbd>, `bbgpu_video_skip()` executa `player->Stop()` limpo e seguro em thread, disparando `StateStop` para o motor do jogo avançar imediatamente, além de desmascarar `BTN_OPTIONS` (0x8) para cutscenes in-engine.
