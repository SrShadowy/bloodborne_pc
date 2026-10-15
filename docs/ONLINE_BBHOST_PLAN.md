# Plano: Online via protocolo do bbhost (The Hunter's Dream) — mantendo shadNet como alternativa

> **Objetivo:** permitir que players do bbport (este fork) joguem **co-op, invasão, mensagens,
> bloodstains e placas de invocação junto com players do bbhost**, usando o mesmo servidor
> público (**thehuntersdream.com**), enquanto o caminho atual shadNet (`srv.shadps4.net`)
> continua disponível como opção selecionável.
>
> Documento de estudo + plano técnico. Escrito para acompanhamento durante a implementação.

---

## 1. Sumário executivo

Hoje o bbport já fala com o servidor do bbhost **em parte**: `scripts/online.py` mapeia o
tráfego HTTP do jogo (`ss4.scej-network.jp`, `/summon_messenger`, `/basic_utils/login`) para
**thehuntersdream.com:18671** — o mesmo backend que o bbhost usa para sinais/mensagens.
O gap restante é a **camada de sessão/co-op**: criação de sala, signaling, resolução de peers
e tráfego P2P UDP, que hoje passa pelo cliente **shadNet** (TCP+protobuf para
`srv.shadps4.net:31313`). O bbhost faz essa mesma camada via **HTTP+JSON no :18671** + STUN
com atributos proprietários + relay.

**A implementação = adicionar um segundo backend de rede ao `libbbnet.so` que fala o
protocolo do bbhost, selecionável por config, sem remover o shadNet.**

```
                       ┌─────────────────────────────────────────────┐
   Jogo (guest)        │  libbbnet.so (gpu/bbnet/)                   │
   sceNpMatching2* ────►  np_matching2/* ────┐                       │
   sceNpSignaling* ────►  np_signaling/* ────┤                       │
   sceNet* (P2P)   ────►  sys_net/p2p_sockets ┤                      │
   sceHttp*        ────►  http.cpp ──────────┤                       │
                       │                     ▼                       │
                       │        ┌────────────────────────┐           │
                       │        │  BACKEND ABSTRAÇÃO      │          │
                       │        │  (novo: bbnet_backend)  │          │
                       │        └────┬──────────────┬────┘           │
                       │             │              │                │
                       │      [shadnet]        [bbhost]  (novo)       │
                       │   TCP+protobuf      HTTP+JSON :18671         │
                       │   srv.shadps4.net   thehuntersdream.com      │
                       │   UDP p2p próprio   STUN/relay BBHOST        │
                       └─────────────────────────────────────────────┘
```

---

## 2. Estudo das arquiteturas

### 2.1 bbhost — camada de online (referência a ser portada)

Código em `bbhost/src/`. Total ≈ **6.6k linhas** de C++, divididas em três camadas limpas:

| Camada | Arquivos | Papel |
|---|---|---|
| **Cliente do servidor** | `src/net/account.{h,cpp}` (263), `http.{h,cpp}` (239), `session.{h,cpp}` (761), `stun.{h,cpp}` (161) | Protocolo puro: JSON sobre HTTP (libcurl), sem conhecimento do jogo |
| **HLE das libs PS4** | `src/hle/np_matching2.cpp` (1167), `np_signaling.cpp` (229), `net.cpp` (1501), `http.cpp` (777), `system.cpp` (~500 NP) | Implementa as exports `sceNp*`/`sceNet*`/`sceHttp*` que o jogo chama |
| **Glue de engine** | `src/engine/summon_invite.cpp`, `sos_signs.cpp`, `chara_id.cpp`, `np_test.cpp` | Hooks no eboot 1.09 (endereços hardcoded) — **não portar** (ver §6) |

#### Protocolo do servidor (HTTP + JSON, porta 18671)

Sem protobuf, sem websocket. JSON parseado por lib in-tree (`src/replay/json.cpp`).

**Endpoints de conta** (`account.cpp`) — base `online.auth_server`:

| Chamada | Payload |
|---|---|
| `POST /auth/device/start` | `{DeviceName}` → `{DeviceCode, UserCode, VerificationUri, Interval, ExpiresIn}` |
| `POST /auth/device/poll` | `{DeviceCode}` → `{Status: pending\|slow_down\|approved\|denied\|expired[, Token, OnlineId]}` |
| `POST /auth/account/create` | `{Name, DeviceName}` → `{Token, OnlineId, RecoveryCode}` |
| `POST /auth/recover` | `{Name, RecoveryCode}` → `{Token, OnlineId, RecoveryCode(novo)}` |

Fluxo: o cliente mostra um código (`XXXX-XXXX`) e abre `VerificationUri?code=...` no browser
(link com Discord acontece no site); o cliente faz poll até `approved` e guarda `Token`.
**Sem senha.** A conta é o dispositivo (token), o `OnlineId` é o nome no jogo.

**Endpoints de sessão/matchmaking** (`session.cpp`):

| Chamada | Payload |
|---|---|
| `POST /mp/matching2/context_start` | `{OnlineId, SignalingAddr, SignalingPort, MappedAddr, MappedPort}` |
| `POST /mp/matching2/create_room` | endpoint + `MaxMembers` + extras (`HostArea/HostLevel/HostPos/MemberTag`) → `{ResKind, RoomId, SessionId, MemberId}` |
| `POST /mp/matching2/join_room` | endpoint + `RoomId` → `{…, OwnerMemberId, Members[], HostRuleset}` |
| `POST /mp/matching2/leave_room` | `{SessionId, MemberId}` |
| `POST /mp/matching2/heartbeat` | `{SessionId, MemberId}` → `{InRoom}` — **a cada 5s; servidor derruba aos 15s** |
| `POST /mp/matching2/kick_member` | `{SessionId, MemberId, KickerMemberId, OptData(base64 ≤16B)}` |
| `POST /mp/matching2/signaling_update` | quando o endereço mapeado muda |
| `POST /np/signaling/resolve` | `{OnlineId}` → `{Addr, Port}` |
| `POST /np/events/poll` | `{OnlineId, Cursor, Categories:["matching2"]}` → evento + `NextCursor` |
| `POST /np/events/ack` | `{OnlineId, Cursor}` |

Objeto endpoint enviado em tudo:
`{OnlineId, LocalAddr, LocalPort, PublicAddr, PublicPort, MappedAddr, MappedPort}`.
Regra de NAT: `Mapped*` diferente de `Local*` vence.

**Eventos** (nome do evento vem no poll): `guest_invite`, `room_member_joined`,
`room_member_left` (`Reason`), `room_closed`, `room_member_kicked`, `peer_deactivated`.

**O "co-op contract" (timings — crítico, copiar fielmente):**
todas as respostas/callbacks são agendadas com delays fixos no dispatcher:
- reply de create room: **+300 ms**
- signaling ESTABLISHED: **+500 / +800 ms**
- MEMBER_LEFT: **+100 ms**; reply de LeaveRoom: **+300 ms**
- ContextStarted (0x6F02): **+200 ms** após `context_start`, mesmo sem servidor ("online mas sozinho")
- poller: **250 ms**, com backoff ×2 até 4000 ms (60 s se conta recusada)
- heartbeat: **5 s**; keepalive STUN: **15 s**; RTT do ping = `2 × stun_rtt_us`

**P2P UDP:**
- Porta por instância: **9307** (`online.p2p_port`; segunda instância pega a próxima livre).
- Header do datagrama (mantido no wire, formato do kernel PS4 — "para interoperar com shadPS4"):
  `[0xff][flags][src vport][dst vport]`, flags `0x80 P2P | 0x40 byte-vports | 0x20 comid`,
  vports de 1 byte no Bloodborne (**40** boot, **30** sessão).
- **STUN** (RFC 5389) com atributos proprietários: `BBHOST-HELLO 0x8100` (token),
  `BBHOST-RELAY 0x8101` (token + vport + endereço observado).
- **Hole punching**: probe de 8 bytes `{0xfe,'b','b','h','p',0,0,0}` a cada **500 ms**, máx
  **40 iterações (20 s)**, para endereço público E local do peer.
- **Relay** (fallback): `[0xfb]['R'][token 8][dst vport u16] <datagrama>` (cliente→servidor,
  enviado para a porta STUN); `[0xfb]['r'][src vport u16] <datagrama>` (servidor→cliente).
- Keepalive STUN a cada 15 s; endereço mapeado cacheado 120 s; mudança → `signaling_update`.

**Threading:** dispatcher único (`bb-np`) é o **único** que chama callbacks do guest;
poller (`bb-np-poll`), keepalive (`bb-np-keep`), reader UDP por porta, worker HTTP por request.

### 2.2 bbport atual — camada de online (a ser estendida)

Código em `gpu/bbnet/` (**~48k linhas**, vendored do **shadp2p**, fork GPL-2 do shadPS4) +
roteador em `src/runtime_net.c` (92 linhas).

**Roteamento de imports:** `runtime_resolve` → `runtime_services_resolve`
(`src/runtime_services.c:470`) → `runtime_net_resolve` (`src/runtime_net.c`) — prefixos
`sceNet/sceNp/sceHttp/sceSsl` (exceto trophy/commerce/profile-dialog) vão para
`dlopen("gpu/libbbnet.so")` → `bbnet_resolve(scoped_nid)` → tabela dos 17 `RegisterLib`
em `bbnet_glue.cpp`. O módulo exporta só `bbnet_init`/`bbnet_resolve` (`-Bsymbolic`, `RTLD_LOCAL`).

**Cliente shadNet** (`shadps4/shadnet/`):
- TCP **plaintext**, IPv4, header de 15 bytes LE
  (`u8 tipo | u16 cmd | u32 total_size | u64 packet_id`), payload = protobuf
  (length-prefixed, `shadnet.proto`, lite runtime).
- Endpoints: `BB_SHADNET_SERVER` (default `srv.shadps4.net:31313`), WebAPI REST
  `BB_SHADNET_WEBAPI` (`:31315`, convite via `Authorization: Bearer <token>`).
- Login: `{npid, password, token, title_id, ...}` — **conta por NPID+senha**, modelo diferente
  do bbhost (device flow).
- Comandos: 0–12 conta/amigos, 30–40 score, **100–115 matchmaking**
  (`ContextStart, CreateRoom, JoinRoom, LeaveRoom, SearchRoom, ...`).
- Notificações: `RoomEvent (10)`, `RoomMessage (11)`, `Friend* (5–8)`, `WebApiPushEvent (17)`.

**P2P atual** (`shadps4/core/libraries/network/p2p_sockets.cpp`, `P2PTransport`):
- Um socket UDP por instância; **UPnP (miniupnpc)**; header de **4 bytes** (`src/dst vport`,
  network order); `dst_vport=0xffff` = tráfego interno com magia `"SHAD"` (0x21 matching2,
  0x10 control, senão signaling/STUN echo de 6 bytes).
- Handshake signaling próprio: pacotes `SHAD` 0x05/0x06/0x07/0x10
  (`np_signaling_state.h:49-119`), porta UDP de signaling = **TCP+1 (31314)**.
- Peer resolvido via room data (`addr/port`) ou `RequestSignalingInfos` pelo canal TCP.

**Já interoperável com o bbhost:** `scripts/online.py` escreve `user/host_overrides.json`
mapeando `https://ss4.scej-network.jp:20443` → `BB_COMMUNITY_SERVER` (default
`https://thehuntersdream.com`) e `/summon_messenger/*` → porta **18671**.
Aplicado em `http.cpp` (`ApplyHostOverride`, ~linha 341). **Mensagens/bloodstains/placas
já funcionam no servidor do bbhost.**

**Diferenças-chave vs bbhost (o que reconciliar):**

| | bbhost | bbport atual |
|---|---|---|
| Transporte matchmaking | HTTP+JSON :18671 | TCP+protobuf :31313 |
| Conta | device flow, token, sem senha | NPID+senha (shadNet) |
| Header P2P | `[0xff][flags][vport][vport]` (1B vport) | 4 bytes (src/dst vport) |
| STUN | RFC 5389 + `BBHOST-HELLO/RELAY` | handshake `"SHAD"` próprio |
| Relay | sobre a porta STUN (0xfb) | não (usa UPnP) |
| Eventos | poll HTTP 250 ms com cursor | push por TCP |
| Fallback NAT | STUN + punch + relay | UPnP + punch |

> **Nota importante:** o header P2P do bbhost é "mantido no wire para interoperar com
> shadPS4" — ou seja, o próprio bbhost **escolheu um formato que jogadores shadPS4
> entenderiam**. Vale verificar no `simnet.py`/`client.h` se o framing bbhost é
> compatível com o `P2PTransport` atual antes de decidir trocar o framing (fase 3).

---

## 2.5 Conformidade com `MyGUIDELINES.md`

O plano segue os padrões do projeto. Implicações concretas:

- **Tamanho de arquivo (§2):** o cliente do bbhost não pode ser copiado como está —
  `session.cpp` (657 linhas) e `np_matching2.cpp` (1167) excedem o limite de 300.
  Decomposição por responsabilidade (não por contagem arbitrária):
  | Novo arquivo (gpu/bbnet/bbhost/) | Responsabilidade | Máx. ~ |
  |---|---|---|
  | `account.h/cpp` | device flow (start/poll/create/recover) | 250 |
  | `http_json.h/cpp` | POST/GET JSON sobre cpp-httplib + erros | 200 |
  | `session_room.h/cpp` | create/join/leave/heartbeat (cliente→servidor) | 300 |
  | `session_poll.h/cpp` | poller de eventos com cursor+ack+backoff | 250 |
  | `session_state.h/cpp` | contexto, peers, ids provisórios, mutexes | 250 |
  | `dispatch.h/cpp` | fila de eventos com delays ("co-op contract") | 200 |
  | `stun.h/cpp` | codec STUN + attrs BBHOST | 170 |
  | `p2p_frame.h/cpp` | header `[0xff][flags][vport]` e relay `0xfb` | 200 |
  | `backend.h` | interface `OnlineBackend` + seleção | 100 |
  | `backend_shadnet.cpp` | adaptador do cliente shadNet existente | 200 |
  | `backend_hd.cpp` | orquestra account+session+stun (HdBackend) | 250 |
- **Separação de responsabilidades (§3):** protocolo (HTTP/JSON/STUN) não conhece o
  jogo; o `backend_hd.cpp` é o único ponto que fala com o HLE (`np_matching2_mm.cpp`).
  A UI de conta fica no launcher (Python), não no módulo C++.
- **Tratamento de erros (§7):** toda resposta do servidor é validada (`ResKind`,
  campos obrigatórios); falha de conta ≠ falha de rede ≠ recusa (401) — estados
  distintos expostos ao launcher; nada de retry cego (backoff é explícito e limitado).
- **Compatibilidade (§9):** comportamento padrão **não muda** — default
  `BB_ONLINE_BACKEND=shadnet`; env vars existentes (`BB_SHADNET_*`) mantidas.
- **Testes (§10):** cada fase entrega seu teste (ver §4); nenhum passo é dado sem
  build + teste executado; o que não puder ser automatizado é dito explicitamente.
- **Escopo mínimo (§11):** nada de portar hooks de engine, servidor próprio ou
  features especulativas nesta rodada (§6).

---

## 3. Decisões de design

1. **Dois backends, um selecionador.** Novo módulo `gpu/bbnet/bbhost/` (protocolo client)
   dentro do `libbbnet.so`, atrás de uma pequena abstração. Variável
   `BB_ONLINE_BACKEND=shadnet|bbhost` (default `shadnet` para não quebrar nada), exposta no
   launcher como combo "Serviço online". `BB_ONLINE=1` continua ligando o módulo.
2. **Reaproveitar o HLE existente.** As exports `sceNpMatching2*`/`sceNpSignaling*` do
   shadp2p continuam; só a implementação **atrás** delas muda quando o backend é `bbhost`
   (as funções `Mm*` de `np_matching2_mm.cpp` chamam o backend em vez de `ShadNetClient`).
3. **Conta do bbhost fica no launcher** (GTK4): tela de vínculo com device flow
   (`/auth/device/start` → mostrar `UserCode` → abrir `VerificationUri` → poll até aprovar),
   guardando `{OnlineId, Token}` em `~/.config/bbport/account.json` (ou no `user/`),
   injetado como `BB_HOST_TOKEN`/`BB_HOST_ONLINE_ID` no ambiente do jogo.
4. **Licença.** bbhost é **GPL-3**, bbport é **GPL-2**. Não copiar código literalmente:
   reimplementar o protocolo a partir desta especificação + os sims Python de referência
   (o protocolo JSON não é copyrightável; a implementação nova será do fork). Se alguma
   cópia for inevitável, isolar no módulo GPL-3 com nota — decidir caso a caso.
5. **Não portar os hooks de engine** do bbhost (§6): timeout de placa 30s, 5 jogadores,
   chara_id — dependem de endereços do eboot 1.09 CUSA00900; nosso alvo é CUSA03173.

---

## 4. Plano de implementação (fases)

### Fase 0 — Fundação e spec (sem risco)
- [x] Extrair do bbhost (lendo, não copiando) uma spec definitiva do wire: todos os
      endpoints, campos exatos, timings (§2.1 já cobre ~90%).
- [x] Estudar `bbhost/tools/nettest/simclient.py` e `simnet.py` — são a **referência
      byte-exata** do protocolo; vão guiar os testes.
- [x] Criar `gpu/bbnet/bbhost/` com o transporte HTTP+JSON (`http_json.h/cpp`) usando
      **cpp-httplib + OpenSSL** que já estão no build do bbnet (não trazer libcurl);
      nlohmann já é dependência do bbnet. Teste `bbhost-http-test`
      (`tests/test_bbhost_http.cpp`, ninja -C out/gpu bbhost-http-test).

### Fase 1 — Conta (launcher) ✔ concluída
- [x] `launcher/bbport_bbhost.py` (novo, sem GTK): o fluxo de conta do protocolo bbhost —
      `device_start/poll/wait/account_create/recover`, checando `ResKind` (recusa é
      `ResKind != 0` com `Message`, seja 2xx ou 429) e guardando `{OnlineId, Token}` em
      `~/.config/bbport-launcher/account.json` (0600, com as settings).
      **Decisão registrada (desvio do rascunho acima):** o fluxo vive no launcher
      (Python stdlib, §3.3 "conta fica no launcher"), porque a UI é GTK e o C++ não
      tem quem o chame — implementar `account.{h,cpp}` só para um CLI bridge seria
      código morto + binário extra. O lado C++ recebe na **Fase 2** apenas o estado
      do token (`BB_HOST_TOKEN`/`BB_HOST_ONLINE_ID` → `X-BB-Token` nas chamadas de
      sessão), sem reimplementar os endpoints.
- [x] Launcher (página *Online*, agora em `launcher/bbport_online.py` / `OnlineMixin`):
      combo "Сервис онлайн" (shadnet|bbhost); quando bbhost: grupo "Учётная запись The
      Hunter's Dream" com "Ввязать" (mostra `XXXX-XXXX`, abre o browser, poll com
      spinner e botão Cancelar), "Создать", "Восстановить", exibe o OnlineId vinculado
      e salva o token. Servidor próprio num EntryRow (vazio = thehuntersdream.com:18671,
      esquema validado ao digitar). shadNet segue visível e ativo nos dois modos até a
      Fase 2.
- [x] `bbport_config.py`: `online_backend` (default `shadnet`), `online_bbhost_server`;
      ao lançar com backend bbhost exportar `BB_ONLINE_BACKEND`, `BB_HOST_TOKEN`,
      `BB_HOST_ONLINE_ID`, `BB_HOST_SERVER=thehuntersdream.com`, `BB_HOST_SCHEME=https`
      (valor desconhecido vira shadnet; servidor inválido cai no público).
- [x] i18n: novas strings EN/PT/PT_BR em `bbport_i18n.py` (ZH cai no EN por `tr()`;
      `launcher/locales/*.json` não tem consumo no launcher atual — não tocado).
- [x] Teste: `tests/test_bbhost_account.py` (unittest + servidor HTTP local em
      127.0.0.1: wire, lentidão, cancelamento, expiração, recusas, account.json,
      export de env). `python3 tests/test_bbhost_account.py` → 29/29.
- **Estado honesto:** o seletor, o vínculo de conta e a exportação de env funcionam
  ponta a ponta; **o jogo ainda usa shadNet para a sessão** (nada lê
  `BB_ONLINE_BACKEND`/`BB_HOST_*` até a Fase 2 — coop não muda de comportamento).

### Fase 2 — Backend de sessão (o grosso)
- [ ] `gpu/bbnet/bbhost/account.{h,cpp}` (escopo reduzido na Fase 1): só o estado do
      token vindo do launcher (`BB_HOST_TOKEN`/`BB_HOST_ONLINE_ID`) + detecção de 401
      para `X-BB-Token` nas chamadas de sessão; os endpoints `/auth/*` ficam no
      launcher (`bbport_bbhost.py`), já implementados e testados.
- [ ] `gpu/bbnet/bbhost/session.{h,cpp}`: espelhar `net/session.cpp` do bbhost —
      contexto, create/join/leave room, heartbeat 5s, poller 250ms com cursor+ack,
      keepalive, fila de eventos com **os mesmos delays** (300/200/100 ms…).
- [ ] `gpu/bbnet/bbhost/stun.{h,cpp}`: codec STUN RFC5389 + attrs `0x8100/0x8101`.
- [ ] Abstração: `struct OnlineBackend { ContextStart; CreateJoinRoom; LeaveRoom;
      SearchRoom; ...; poll events; }` em `np_matching2_mm.cpp` — implementação
      `ShadNetBackend` (atual, sem mudança de comportamento) e `HdBackend` (novo).
- [ ] Mapear as respostas JSON do bbhost para os structs/orbis que o `np_matching2.cpp`
      entrega ao jogo (RoomId→SceNpRoomId, Members[], signaling callbacks 0x1101/0x1102…).
- [ ] `sceNpSignaling`: `ActivateConnection` → `/np/signaling/resolve` + member ids
      provisórios `0xff00+`; status/estabelecido com delays 200/400 ms.
- [ ] NP core: `sceNpGetState`=SIGNED_IN quando há token; `sceNpGetOnlineId` lê
      `BB_HOST_ONLINE_ID`; `sceNpAuthGetAuthorizationCode`→`"DUMMY"`; WebAPI → respostas
      vazias canônicas (como o bbhost faz).
- [ ] Teste: `tests/test_online_bbhost.c` (espelhar `tests/test_online.c`) + mock do
      servidor em `tests/` (pequeno servidor HTTP de resposta fixa para CI).

### Fase 3 — P2P UDP
- [ ] Decisão após verificar compatibilidade do framing (ver §2.2): se o header
      `[0xff][flags][vport][vport]` for aceitável ao `P2PTransport` atual, **adicionar**
      esse modo de framing em `p2p_sockets.cpp` em vez de substituir.
- [ ] Integrar STUN bbhost (porta configurável, default 3478) + keepalive 15s +
      `signaling_update` quando o mapeamento mudar.
- [ ] Hole punching (probe `bbhp`, 500ms×40) para endereços público+local dos peers da sala.
- [ ] Relay `0xfb` como fallback quando punch falhar (o shadPS4 atual não tem relay).
- [ ] Manter UPnP como camada extra (o bbhost não usa; pode ajudar ainda mais).
- [ ] Testar contra `simnet.py` do bbhost com as matrizes de NAT
      (`none/full/addr/port/sym`) — rodar o sim num container/venv e apontar o jogo pra ele.

### Fase 4 — HTTP do jogo
- [ ] Confirmar que `ApplyHostOverride` cobre tudo que o bbhost reescreve
      (incl. playlog `bb-playlog-*.s3.amazonaws.com` → `:18671`).
- [ ] Enviar `X-BB-Token` (e regra `X-BBHost-Ruleset` se quisermos mods isolados)
      nas requisições do jogo quando backend bbhost — em `http.cpp`.
- [ ] `scripts/online.py`: gerar os overrides a partir do backend selecionado.

### Fase 5 — Overlay/UX
- [ ] Notificações do overlay (já há stubs em `bbnet_glue.cpp:345-402` que só logam):
      plugar no overlay ImGui do libbbgpu (conta conectada, sala, convites, estado do relay).
- [ ] Status "conectado ao Hunter's Dream / shadNet" no HUD junto ao indicador de shaders.
- [ ] Teste de sessão real: dois clientes (um bbport, um bbhost) — cenários do
      `bbhost/tools/nettest/bench.py` (coop2, five, kick, crash, churn) como checklist.

### Fase 6 — Hardening
- [ ] Reconexão/backoff (2s→30s, como o worker atual de 500 ms já faz).
- [ ] Recuperação de token recusado (HTTP 401 → poller lento + aviso no launcher).
- [ ] Logging por env (`BB_HOST_TRACE=1`), espelhando `BBHOST_NET_TRACE`.
- [ ] Docs: `docs/ONLINE.md` (EN) com config, contas, servidor próprio.

---

## 5. matriz de esforço estimada

| Fase | Linhas novas (est.) | Depende de |
|---|---|---|
| 0 Fundação | ~400 | — |
| 1 Conta (launcher+client) | ~700 (Python+C++) | 0 |
| 2 Sessão/matchmaking | ~1800–2500 | 0 |
| 3 P2P/STUN/relay | ~900–1400 | 2 |
| 4 HTTP jogo | ~200 | 1 |
| 5 Overlay/UX | ~400 | 2,3 |
| 6 Hardening | ~300 | tudo |
| **Total** | **~4.7k–6.9k** | |

Rede pura do bbhost ≈ 6.6k linhas; nosso total é comparável porque o HLE do guest
(`np_matching2`, imports, threads) **já existe** no bbnet — só o atrás muda. A
decomposição em arquivos ≤300 (§2.5) distribui o total em ~12 módulos pequenos.

### Definition of Done (por fase, §16 das diretrizes)

Uma fase só está completa quando:
- [ ] Comportamento pedido implementado e verificado (build executado, não só escrito).
- [ ] Backend shadnet inalterado funcional (`tests/test_online.c` passa).
- [ ] Testes da fase escritos e executados (ou justificativa do porquê não).
- [ ] Arquivos ≤300 linhas (ou motivo documentado).
- [ ] Erros de rede/conta/servidor distinguíveis nos logs.
- [ ] Sem env var ou default novo não documentado.
- [ ] Sem mudanças em arquivos não relacionados.

## 6. O que NÃO portar (por enquanto)

- **Hooks de engine** (`summon_invite`, `sos_signs` timeout 30s, `chara_id`,
  playlog acelerado): endereços hardcoded do eboot **1.09 CUSA00900**; nosso dump é
  **CUSA03173**. Requereriam mapear offsets equivalentes — projeto futuro separado.
- **Servidor próprio**: o servidor é projeto fechado; o público
  (thehuntersdream.com) basta. `online.host`/`auth_server` configuráveis já dão
  espaço para auto-hospedagem futura se aparecer um clone open-source.
- **Plugins/ruleset** (`X-BBHost-Ruleset`): enviar **`vanilla` sempre** nesta rodada —
  os dois clientes de referência mandam o ruleset próprio (simclient.py; o bbhost usa
  `own_ruleset()`="vanilla") e sem os hooks de engine não há mods que mudá-lo; players
  vanilla encontram vanilla.
- **5 jogadores / 2 invasores**: enhancement de engine, não de rede.

## 7. Riscos e mitigações

| Risco | Mitigação |
|---|---|
| Timing do "co-op contract" errado → invocação falha silenciosamente | Copiar delays exatos; usar `bench.py` do bbhost como checklist de cenários |
| Framing P2P incompatível | Verificar cedo (início da fase 3) contra `simnet.py`; o header bbhost foi desenhado p/ shadPS4 |
| Conta/servidor público mudar | Spec congelada a partir do `simclient.py`; testes com mock |
| Regressão do shadNet | Backend shadnet intocado; testes existentes (`tests/test_online.c`) continuam; novo teste para bbhost |
| Licença GPL-3 vs GPL-2 | Reimplementar do protocolo, não copiar; documentar origem de cada decisão |
| "1 em 4 summonings falha" (issue conhecida do bbhost) | Bug do servidor/protocolo, não nosso — documentar |

## 8. Referências rápidas (arquivos-chave)

**bbhost (leitura):**
- `src/net/session.cpp` — coração do protocolo (rooms, poller, heartbeat, keepalive)
- `src/net/account.cpp` — device flow
- `src/net/stun.cpp` + `tests/stun_test.cpp` — codec STUN
- `src/hle/net.cpp:1559-1501` — punch/relay/stun do lado HLE
- `src/hle/np_matching2.cpp` — eventos e timings do "co-op contract"
- `tools/nettest/simclient.py`, `simnet.py`, `bench.py` — **referência executável**
- `docs/online.md`, `bbhost.example.toml`

**bbport (mudar):**
- `src/runtime_net.c` — roteador de imports (não muda)
- `gpu/bbnet/bbnet_glue.cpp` — init/registros do módulo (ponto do backend)
- `gpu/bbnet/shadps4/core/libraries/np/np_matching2/np_matching2_mm.cpp` — chamadas
  `shadnet::` a abstrair (ponto de costura principal)
- `gpu/bbnet/shadps4/core/libraries/np/np_signaling/` — signaling a abstrair
- `gpu/bbnet/shadps4/core/libraries/network/p2p_sockets.cpp` — framing P2P
- `gpu/bbnet/shadps4/core/libraries/network/http.cpp:341` — host overrides (fase 4)
- `scripts/online.py` — gera `host_overrides.json`
- `launcher/bbport_launcher.py:814` (página Online) e `launcher/bbport_config.py:336`
  (env) — fase 1
