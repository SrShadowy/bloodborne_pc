# WHAT_NEEDS_TO_BE_FIXED.md — Soluções Pragmáticas e Arquitetura de Logs

Documento técnico com implementações cirúrgicas (princípio Ponytail: menor alteração completa) e instrumentação obrigatória de **telemetria e logs** para que nenhuma regressão de I/O, memória ou rede volte a ocorrer às escuras.

---

## 1. Diretriz de Observabilidade & Logs Obrigatórios

Para evitar diagnósticos no escuro e garantir que relatos de usuários sejam imediatamente decifráveis através dos arquivos `.log`:
1. **Tags Padronizadas**: Todo log relacionado a essas correções deve utilizar prefixos estruturados:
   * `[BBSC Save]` — para I/O, slots e persistência de dados.
   * `[BBSC MemScan]` — para tempo de busca, intervalo de endereços e quantidade de memória escaneada.
   * `[BBSC LoadCover]` — para estados de tela de carregamento, teleporte e timeouts de tela preta.
   * `[AvPlayer]` — para ciclo de vida de cutscenes, playback time e controle de loop de attract mode.
   * `[Net P2P]` — para identificação de versão de framing por peer (Gen 19 vs Gen 20).
   * `[BBSC Invasion]` — para detecção de tipo de invasor/pacto, teleporte e fluxo de saída.
2. **Medição de Latência**: Operações de I/O de disco e varredura de memória devem registrar tempos em microssegundos (`us`) ou milissegundos (`ms`).
3. **Thresholds de Alarme (`LOG_WARNING`)**:
   * I/O de disco $> 10\text{ ms}$ $\rightarrow$ Warning explícito de travamento de disco.
   * Memory scan $> 100\text{ ms}$ $\rightarrow$ Warning de saturação de CPU/RAM.
   * Load cover aguardando $> 12\text{ s}$ $\rightarrow$ Warning e desarme defensivo da tela preta.

---

## 2. Soluções Pragmáticas & Código de Correção

### [P0] 1. Eliminação do I/O Síncrono em `BbscActiveSaveSlot()`

#### Diagnóstico
A cada 2 a 5 segundos, chamadas a `BbscActiveSaveSlot()` executam `std::filesystem::directory_iterator` e `fs::last_write_time` em todos os 10 slots de cada usuário. No Windows (NTFS), isso causa chamadas síncronas ao kernel (`FindFirstFileW`, `GetFileAttributesExW`), travando a thread principal e provocando quedas periódicas de FPS (ex: 60 $\rightarrow$ 48 FPS).

#### Solução Pragmática (Cache com TTL + Alarme de I/O)
Em [`gpu/bbnet20/bbsc/core/bloodborne_re.cpp`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/gpu/bbnet20/bbsc/core/bloodborne_re.cpp#L5905):
* Cachear o `BbscSaveSlot` resolvido.
* Em gameplay, o personagem jogado **nunca muda de slot**. A varredura de disco deve acontecer apenas uma vez na inicialização ou após um intervalo seguro (TTL de 60 segundos), ou sob invalidação explícita.
* Instrumentar a chamada para medir a latência exata do disco em microssegundos.

```cpp
// gpu/bbnet20/bbsc/core/bloodborne_re.cpp
static std::optional<BbscSaveSlot> s_cached_active_slot;
static s64 s_last_save_slot_scan_ms = 0;
constexpr s64 BbscSaveSlotCacheTtlMs = 60'000; // 60 segundos de cache

void InvalidateBbscActiveSaveSlot() {
    s_cached_active_slot = std::nullopt;
    s_last_save_slot_scan_ms = 0;
}

std::optional<BbscSaveSlot> BbscActiveSaveSlot() {
    const s64 now = SteadyNowMs();
    if (s_cached_active_slot.has_value() && (now - s_last_save_slot_scan_ms < BbscSaveSlotCacheTtlMs)) {
        return s_cached_active_slot;
    }

    const auto t0 = std::chrono::steady_clock::now();
    try {
        s_cached_active_slot = BbscActiveSaveSlotUnchecked();
        s_last_save_slot_scan_ms = now;
    } catch (const std::exception& e) {
        LOG_WARNING(Debug, "[BBSC Save] Could not read save folder: {}", e.what());
        return std::nullopt;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    if (s_cached_active_slot.has_value()) {
        LOG_INFO(Debug, "[BBSC Save] Active slot resolved: user='{}' slot={} (scan took {} us)",
                 s_cached_active_slot->user, s_cached_active_slot->slot, dur_us);
    }
    if (dur_us > 10'000) { // Alerta de I/O lento (>10 ms)
        LOG_WARNING(Debug, "[BBSC Save] SLOW save directory iteration: {} us (disk latency warning)", dur_us);
    }
    return s_cached_active_slot;
}
```

---

### [P0] 2. Clamping do Memory Scan de 1 TB & Supressão de Re-scans

#### Diagnóstico
No 0.7 upstream, o scanner do BBSC busca padrões até `hi = 0x10000000000ULL` (1 Terabyte). No Windows, sem SEH, a rotina dispara milhares de `ReadProcessMemory` (1 MB por syscall) e `VirtualQuery` por todo o espaço mapeado. Em máquinas com 32 GB+ de RAM, isso congela a CPU. Além disso, `StartBbscToolScan()` dispara essa busca pesada para cada item de `BbscDreamToolChecks`.

#### Solução Pragmática (Clamping a 72 GB / Guest Direct Heap + Single-Execution Guard + Latency Log)
Em [`gpu/bbnet20/bbsc/core/bloodborne_re.cpp`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/gpu/bbnet20/bbsc/core/bloodborne_re.cpp#L11410):
1. **Clamping de Endereço**: No shadPS4, a memória direta do PS4 (`USER_MIN`) inicia em `0x1000000000ULL` (64 GB) e vai até `0x1200000000ULL` (72 GB = 64 GB + 8 GB de RAM física do PS4). O upstream acidentalmente colocou um zero a mais (`0x10000000000ULL` = 1 Terabyte), forçando o Windows a varrer 950 GB de espaço não alocado com `VirtualQuery` e `ReadProcessMemory`. Fix: Travar o teto máximo de busca em `0x1200000000ULL`.
2. **Guarda de Execução Única**: `StartBbscToolScan()` deve rodar no máximo uma única vez por sessão.
3. **Log de Telemetria de Scan**: Registrar tamanho da agulha, intervalo escaneado, quantidade de hits e tempo total gasto em milissegundos.

```cpp
// gpu/bbnet20/bbsc/core/bloodborne_re.cpp
constexpr u64 BbscMaxGuestScanAddress = 0x1200000000ULL; // Teto de 72 GB (0x1000000000 Direct Heap + 8 GB PS4 RAM)

static std::atomic<bool> s_bbsc_tool_scan_ever_done{false};

void StartBbscToolScan() {
    if (s_bbsc_tool_scan_ever_done.exchange(true)) {
        return; // Já foi executado nesta sessão; nunca re-escanear!
    }
    if (bbsc_tool_scan_running.exchange(true)) {
        return;
    }
    bbsc_tool_scan_done = false;
    std::thread([] {
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::pair<size_t, u64>> sites;
        for (size_t i = 0; i < BbscDreamToolChecks.size(); ++i) {
            const auto needle = BbscToolCheckBytes(BbscDreamToolChecks[i].item, BbscToolCompareEqual);
            const u64 scan_hi = std::min(0x10000000000ULL, BbscMaxGuestScanAddress);
            
            const auto hits = Core::Bbsc::FindInMemory(needle, BbscScanLow(), scan_hi, 8);
            for (const u64 hit : hits) {
                sites.emplace_back(i, hit + 7);
            }
        }
        {
            std::scoped_lock lock{bbsc_tool_mutex};
            bbsc_tool_sites = std::move(sites);
        }
        bbsc_tool_scan_done = true;
        bbsc_tool_scan_running = false;

        const auto t1 = std::chrono::steady_clock::now();
        const auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        LOG_INFO(Debug, "[BBSC MemScan] Tool scan completed: {} sites found across {} items (took {} ms)",
                 bbsc_tool_sites.size(), BbscDreamToolChecks.size(), dur_ms);
        if (dur_ms > 100) {
            LOG_WARNING(Debug, "[BBSC MemScan] SLOW tool scan detected: {} ms (RAM scan overhead)", dur_ms);
        }
    }).detach();
}
```

---

### [P1] 3. Desarme de Tela Preta no `TickBbscLoadCover()` (Timeout Defensivo)

#### Diagnóstico
Enquanto o modo `why == "wipe"` possui timeout de saída de 20s/2s após o término da tela de loading do jogo, os modos `summon`, `join` e `follow` não possuem verificação pós-carregamento. Se os pacotes de posição do host não chegarem pela rede (falha de crossplay), a rotina fica gravando tela preta até atingir o limite global de **75 segundos**.

#### Solução Pragmática (Post-Load Settlement Timeout de 12s + State Logs)
Em [`gpu/bbnet20/bbsc/core/bloodborne_re.cpp (TickBbscLoadCover)`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/gpu/bbnet20/bbsc/core/bloodborne_re.cpp#L7171):
* Quando a tela de carregamento do próprio jogo (`game_screen_seen`) é concluída e o jogo não está mais carregando, iniciar um cronômetro de assentamento.
* Se após **12 segundos** o teleporte não tiver chamado `EndBbscLoadCover`, forçar a liberação da tela preta com log de aviso.

```cpp
// gpu/bbnet20/bbsc/core/bloodborne_re.cpp - dentro de TickBbscLoadCover()
if (bbsc_load_cover.game_screen_seen) {
    const u64 global = GetGlobalState();
    const u64 menu = ReadValue<u64>(image_base + MenuManPointerOffset, 0);
    const bool loading = (ReadablePointer(global, 0x10) && ReadValue<u8>(global, 0x08) != 0) ||
                         (ReadablePointer(menu, 0x810) && ReadValue<s32>(menu, 0x804) != 0 &&
                          !bbsc_load_cover.own_screen);
    if (loading) {
        bbsc_load_cover.load_over_ms = 0;
    } else if (bbsc_load_cover.load_over_ms == 0) {
        bbsc_load_cover.load_over_ms = now;
        LOG_INFO(Debug, "[BBSC LoadCover] Native load screen dropped for '{}'; awaiting placement (timeout window: 12s)",
                 bbsc_load_cover.why);
    } else {
        const s64 since = now - bbsc_load_cover.load_over_ms;
        constexpr s64 BbscPostLoadMaxWaitMs = 12'000; // 12 segundos no máximo após carregar o mapa
        if (since >= BbscPostLoadMaxWaitMs) {
            LOG_WARNING(Debug, "[BBSC LoadCover] Post-load timeout ({} ms since load end, why='{}') - forcing screen back",
                        since, bbsc_load_cover.why);
            BBSC_TIMELINE("LOAD", "load cover forced in by post-load timeout ({} ms, why='{}')",
                          since, bbsc_load_cover.why);
            EndBbscLoadCover("post-load placement timeout fallback");
            return;
        }
    }
}
```

---

### [P1] 4. Supressão do Loop de Fade Preto no Windows (`AvPlayer` & Attract Mode)

#### Diagnóstico
No Windows, se os codecs de vídeo do FFmpeg (`avcodec-63.dll`, etc.) abortam a reprodução logo após o `Start`, a engine do jogo recebe `StateStop` instantaneamente. O jogo volta para a tela de título, e o timer de ociosidade da tela de título tenta rodar a cutscene de novo, gerando um **fade out $\rightarrow$ fade in infinito**.

#### Solução Pragmática (Detecção de Parada Rápida + Cooldown + Skip Intro)
1. **Configuração padrão**: Garantir `skip_intro=1` por padrão na inicialização do Windows ([`launcher/bbport_config.py`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/launcher/bbport_config.py#L176) e [`run.py`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/run.py)).
2. **Instrumentação em `Player::Announce`** ([`gpu/platform/avplayer/player.cpp`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/gpu/platform/avplayer/player.cpp#L93)):
   * Registrar o tempo exato de duração do vídeo.
   * Se o vídeo parar em menos de 1000 ms repetidamente, registrar um warning e suprimir emissão imediata para evitar que o attract mode cicle a tela em preto.

```cpp
// gpu/platform/avplayer/player.cpp - dentro de Player::Announce()
static s64 s_last_play_start_ms = 0;
static int s_rapid_stop_counter = 0;

void Player::Announce(State reached) {
    const s64 now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    if (reached == State::Play) {
        s_last_play_start_ms = now;
        Emit(AvPlayerEvents::StatePlay);
    } else if (reached == State::Stop || reached == State::EndOfFile) {
        const s64 playback_duration = now - s_last_play_start_ms;
        LOG_INFO(Lib_AvPlayer, "[AvPlayer] Playback reached state {} (duration: {} ms)",
                 static_cast<int>(reached), playback_duration);

        if (playback_duration < 1000 && s_last_play_start_ms != 0) {
            s_rapid_stop_counter++;
            LOG_WARNING(Lib_AvPlayer, "[AvPlayer] Premature video termination ({} ms, count={})",
                        playback_duration, s_rapid_stop_counter);
        } else {
            s_rapid_stop_counter = 0;
        }
        Emit(reached == State::Stop ? AvPlayerEvents::StateStop : AvPlayerEvents::StateEndOfFile);
    } else {
        // Demais estados mantidos
        ...
    }
}
```

---

### [P2] 5. Negociação Dinâmica de Framing P2P (Gen 19 vs Gen 20) no `Decode()`

#### Diagnóstico
A versão 0.19 (usada em builds anteriores do Windows / shadp2p) utiliza o cabeçalho puro `[src vport][dst vport][payload]`. A versão 0.20 utiliza o marcador `0xFF` no primeiro byte e `FlagValid` (`0x80`) no segundo byte. Clientes Windows antigos e clientes 0.7 novos descartam os pacotes uns dos outros.

#### Solução Pragmática (Detecção por Pacote + Log de Identificação de Peer)
Em [`extra/net/bbport_p2p_legacy.cpp`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/extra/net/bbport_p2p_legacy.cpp):
* No método `Decode(packet, from)`:
  * Se `packet[0] == 0xFF && (packet[1] & 0x80)` $\rightarrow$ O peer fala **Gen 20**.
  * Se não tem o marcador e possui tamanho $\ge 4$ $\rightarrow$ O peer fala **Gen 19**.
* Registrar a versão detectada do peer para que o envio correspondente (`EncodeDatagram`) use o codec certo para aquele destinatário.
* Log estruturado:
  ```cpp
  LOG_INFO(Core_Net, "[Net P2P] Auto-negotiated framing for peer {}: Gen {} (sample packet: {} bytes)",
           from.ToString(), is_gen20 ? 20 : 19, packet.size());
  ```

---

### [P2] 6. Correção de Flags na Compilação do Windows (`build_windows.sh`)

#### Diagnóstico
Em [`extra/tools/build_windows.sh (linha 23)`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/extra/tools/build_windows.sh#L23), o CMake está configurado com:
`-DBB_NET=OFF`
Isso impede que a `bbnet20.dll` seja gerada na distribuição do Windows, deixando o pacote sem módulo de rede ou dependente de DLLs externas incompatíveis.

#### Solução Pragmática
Alterar para `-DBB_NET=ON` e adicionar a cópia de `bbnet20.dll` para a pasta `$STAGE/online/bbnet20.dll` no script de empacotamento.

---

### [P1] 7. Correção da Rotina de Invasores & Pactos de Adversário (`bloodborne_re.cpp`)

#### Diagnóstico
A rotina de invasão apresenta falhas graves ao lidar com covenants (Executioners vs Vilebloods, Hunter of Hunters) e estados de morte:
1. **Verificação Incompleta de Tipo**: Em L4596 e L4907, o código verifica apenas `GetLocalPlayerType() == 2` (invasor por Sinister Bell). Invasores de pacto usam os tipos `12` (`0x0C`) e `13` (`0x0D`). Como a checagem falha, invasores de pacto são tratados como phantoms de co-op, sofrendo teleporte forçado para a posição do host (com loading screen duplo) e tentativa de seguir o host em viagens de mapa.
2. **Respawn Indevido de Invasor Morto no Mundo do Host**: Em L8438, a checagem de morte do invasor avalia apenas `IsInvaderType(GetLocalCharacterType())`. Durante o teardown de morte, `WorldChrMan` frequentemente já zerou o ponteiro do personagem local (retornando `-1`). Como a flag `local_invader` fica `false`, o invasor cai na lógica de sessão mantida de co-op e revive dentro do mundo do host em vez de retornar ao seu próprio mundo (morte vanilla).

#### Solução Pragmática (Detecção Completa de Invasores + Fallback de Slot + Log Estruturado)
Em [`gpu/bbnet20/bbsc/core/bloodborne_re.cpp`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/gpu/bbnet20/bbsc/core/bloodborne_re.cpp):
1. **Definição de Tipos**: Atualizar `IsInvaderType` para cobrir todos os tipos de adversário:
   ```cpp
   inline bool IsInvaderType(s32 type) {
       return type == 2 || type == 12 || type == 13;
   }
   ```
2. **Checagem Bivalente (Slot + WorldChr)**:
   ```cpp
   inline bool IsLocalPlayerInvader() {
       return IsInvaderType(GetLocalPlayerType()) || IsInvaderType(GetLocalCharacterType());
   }
   ```
3. **Substituições**:
   * Em `TickBbscJoinTeleport` (L4596): trocar `GetLocalPlayerType() == 2` por `IsLocalPlayerInvader()`.
   * Em `TickBbscFollowHost` (L4907): trocar `GetLocalPlayerType() == 2` por `IsLocalPlayerInvader()`.
   * Na rotina de morte (L8438): usar `IsLocalPlayerInvader()` para garantir retorno imediato ao próprio mundo.
   * Telemetria: Adicionar log `[BBSC Invasion]` ao registrar início ou término de invasão:
     ```cpp
     LOG_INFO(Debug, "[BBSC Invasion] Local player evaluated as invader (slot_type={}, chr_type={})",
              GetLocalPlayerType(), GetLocalCharacterType());
     ```

---

### [P0] 8. Fonte do Log no Launcher & Tela Branca em Proton/Wine vs Windows Nativo

#### Diagnóstico 1: Log com Letras "Derretidas" / Blocos Brancos
* **Causas Identificadas**:
  1. No Windows/Proton, ao usar `Gtk.TextView(monospace=True)`, o Fontconfig/GDI mapeia a família genérica `monospace` para a fonte raster legada de 1-bit `Courier` (`coure.fon`).
  2. A regra CSS possuía `line-height: 1.35;` (inválido na especificação CSS do GTK4, que desconfigura as métricas do Pango) e `gtk-hint-font-metrics=True` forçando truncamento de subpixels.
  3. O `start.cmd` forçava `GSK_RENDERER=gl`. No OpenGL do GTK4 sob Wine, a renderização de fontes bitmap em atlas de textura GL sofre corrupção severa de coordenadas UV, gerando os blocos brancos sólidos da captura.
* **Soluções Pragmáticas Aplicadas**:
  1. No [`launcher/bbport_launcher_log.py`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/launcher/bbport_launcher_log.py) e [`launcher/bbport_log.py`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/launcher/bbport_log.py), definir `monospace=False` e classe `.log-terminal`.
  2. No [`launcher/bbport_launcher.py`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/launcher/bbport_launcher.py):
     - Remover o `line-height: 1.35;` inválido e remover `gtk-hint-font-metrics`.
     - Priorizar `"Lucida Console"` (presente nativamente como `lucon.ttf` em todas as instalações do Proton):
       `font-family: "Lucida Console", "Liberation Mono", "DejaVu Sans Mono", "Consolas", "Courier New", monospace;`
     - Detecção dinâmica de Wine: se executando sob Wine/Proton, configurar `GSK_RENDERER=cairo` (renderizador vetorial estável por CPU, imune a corrupções de atlas GL do Wine). Se Windows nativo, manter `gl`.
  3. No [`dist/.../start.cmd`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/dist/BBPort-0.7.6-4-win-x86_64/start.cmd) e [`extra/tools/build_windows.sh`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/extra/tools/build_windows.sh):
     - Remover o hardcode `set GSK_RENDERER=gl`, substituindo por detecção condicional para respeitar o modo Cairo sob Wine.

#### Diagnóstico 2: Tela Branca / Travamento ao Rodar Build de Windows no Proton
* **Cadeia Exata de Execução e Falha**:
  1. Ao iniciar o `bb-probe.exe`, a janela Win32 SDL é criada com sucesso (ficando branca enquanto aguarda o primeiro frame).
  2. O Vulkan inicializa até a criação da swapchain (`Swapchain: Mailbox, 4 images...`).
  3. Imediatamente a seguir, o `Rasterizer` chama `BbGuestMemory::Install()`.
  4. O build de Windows (`#ifdef _WIN32`) só implementa o modo `HostImport` via `VK_EXT_external_memory_host` com flag `0x80` (`VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT`).
  5. **Conflito 1 (Wine)**: O Wine (`win32u.dll` / `winevulkan.dll`) mapeia apenas handles NT do Windows (`0x2` / `0x4`). Ele não traduz `0x80`, emitindo `fixme:vulkan:win32u_vkCreateBuffer Unsupported handle types 0x80` e criando o buffer no host Linux sem as propriedades de memória externa.
  6. **Conflito 2 (Kernel Linux AMDGPU + Mesa RADV)**: Como o sistema possui GPU AMD (Radeon RX 9070 XT), o driver Linux `amdgpu` exige estritamente memória puramente anônima (`AMDGPU_GEM_USERPTR_ANONONLY`) para `userptr`. O Wine mapeia as seções em tmpfs (`/dev/shm`). Quando a rotina de probe `RunOnce` submete os comandos (`cmd.fillBuffer` / `cmd.copyBuffer`), o kernel Linux rejeita a submissão:
     `radv/amdgpu: The CS has been rejected, see dmesg for more information (-1).`
  7. O Vulkan retorna `VK_ERROR_DEVICE_LOST` e o processo chama `std::_Exit(23)`, morrendo instantaneamente e deixando a janela SDL congelada em branco.
* **Por que funciona no Linux nativo e no Windows nativo, mas falha no Proton?**:
  - **Linux Nativo (`run.sh`)**: Em placas AMD (`0x1002`), o BBPort desativa `HostImport` e utiliza **dma-buf** (`Mode::DmaBuf`).
  - **Windows Nativo**: O driver oficial AMD Adrenalin (`amdxx64.dll`) roda via WDDM e aceita `VK_EXT_external_memory_host` nativamente.
  - **Proton (Windows sobre Linux com AMD)**: O executável Windows tenta `HostImport` (0x80), o Wine descarta a flag no buffer e o driver AMD Linux rejeita o ponteiro importado.
* **Soluções Pragmáticas**:
  1. Em [`src/probe.c`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/src/probe.c): adicionado `AttachConsole(ATTACH_PARENT_PROCESS)` no Windows para que os logs de stdout/stderr não sejam engolidos pelo subsistema GUI, além de `fflush` imediato.
  2. Em [`gpu/common/assert.cpp`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/gpu/common/assert.cpp) e [`gpu/common/logging/log.cpp`](file:///home/shadowy/Documentos/GitHub/bloodborne_pc/gpu/common/logging/log.cpp): flush imediato antes de `_Exit(23)`.
  3. Para quem testa o pacote no Linux com GPU AMD: o Proton sempre esbarrará na ausência de tradução de `0x80` no Wine somada à rejeição de userptr do `amdgpu` do kernel Linux. Para testes funcionais da lógica da porta e do launcher no Linux, utilize o launcher nativo ou execute a build nativa; a build Windows destina-se a PCs com Windows 10/11 onde os drivers proprietários suportam `VK_EXT_external_memory_host`.

---

## 3. Checklist de Validação & QA

Ao testar as alterações, filtrar os logs para confirmar os novos marcadores:

```bash
# 1. Conferir que o I/O de disco parou de rodar a cada 2s e o cache está ativo:
grep "\[BBSC Save\]" logs/*.log

# 2. Conferir que o scan de memória não trava mais a CPU em RAM alta:
grep "\[BBSC MemScan\]" logs/*.log

# 3. Conferir se algum LoadCover excedeu 12s e foi desarmado pelo timeout defensivo:
grep "\[BBSC LoadCover\]" logs/*.log

# 4. Conferir a estabilidade da reprodução de vídeos:
grep "\[AvPlayer\]" logs/*.log

# 5. Conferir a negociação de versões P2P:
grep "\[Net P2P\]" logs/*.log

# 6. Conferir o reconhecimento de invasores e pactos:
grep "\[BBSC Invasion\]" logs/*.log
```

Com essas correções e logs estruturados, qualquer usuário ou testador que reportar problemas terá as métricas exatas de tempo de disco, tempo de RAM e motivo de timeout gravadas no arquivo de log da sessão.
