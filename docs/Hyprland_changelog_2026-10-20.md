# Plano: compatibilidade Hyprland / Wayland (CachyOS) — relato de usuário

Data: 2026-10-20 · Status: em andamento

## 1. O que isso muda na análise

O relato esclarece **duas coisas** que antes eram ambíguas:

1. **Na máquina dele, o caminho Wayland para toolkits está quebrado**: todos os builds (original,
   baguetta) só abrem janela com `GDK_BACKEND=x11 QT_QPA_PLATFORM=xcb GSK_RENDERER=cairo` via
   Steam. Sem eles, "abrem mas sem janela" (GTK/Qt sob Wayland nativo não mapeiam). Ou seja: o
   ambiente força **XWayland** para qualquer toolkit (GTK e Qt) — não é exclusividade do jogo.
2. **O fork crasha com exatamente esses options.** Como eles são aplicados a **dois** processos
   diferentes no fork — o **launcher GTK4** e o **jogo SDL3** — ainda precisamos saber **onde** o
   crash acontece: no launcher (janela GTK) ou no jogo (janela SDL/Vulkan).

Consequência para as hipóteses: **H4 (crash no launcher GTK) sobe de prioridade** — o launcher do
fork é um app GTK4/libadwaita muito mais novo e complexo que o dos outros builds. E o crash do
jogo sob XWayland (H1/H2) continua vivo: com os options, o jogo também roda sob XWayland (o
`run.sh`/`AppRun` forçam SDL x11 sob Steam).

## 2. Estado do que já foi feito (commit `f26538d`)

- `BB_VIDEO_BACKEND=auto|x11|wayland` no `run.sh` (+ `video_backend=` no `bbport.ini`, env vence
  ini), com a decisão impressa no stdout. `auto` mantém X11/XWayland sob Steam (Overlay/Steam
  Input) e Wayland fora dele.
- Módulo `extra/core/video_backend/bbport_video_backend.{h,cpp}` + hooks de 1 linha em
  `gpu/shim/window.cpp` (o antigo hint `[extra]` de 4 linhas virou 1 hook `ApplyHint()`) e
  `gpu/vulkan/swapchain.cpp` (`SurfaceZeroExtent`).
- Toda decisão/trace vai para stdout com flush: `Window: backend X requested`,
  `Window WxH on <driver>`, `Swapchain: surface 0x0 — window not visible`.
- Teste `tests/test_video_backend.py` (6 casos). Build `bbgpu` limpo.
- **Workaround imediato ao usuário:** `BB_VIDEO_BACKEND=wayland %command%` (ignora o forçamento
  x11 do Steam) — se a janela abrir nativamente, a suspeita H1/H2 confirma.

## 2.1 Fix do launcher (commit seguinte) — para o crash sob `GDK_BACKEND=x11`

Com o esclarecimento (a máquina dele precisa de XWayland para *todos* os toolkits), o launcher
também roda sob XWayland. Feito em `launcher/`:

- **Renderer:** com `GDK_BACKEND` terminando em `x11`, o default do renderer vira `cairo` (o
  fallback conhecido no Hyprland/XWayland) em vez de `gl`. (O usuário já passava
  `GSK_RENDERER=cairo`, mas isso cobre quem não passa.)
- **Log de crash/startup:** `faulthandler` + linha de ambiente em
  `~/.config/bbport-launcher/launcher.log` — captura o traceback mesmo sob Steam (que esconde o
  stderr). Este é o passo que fecha a H4: o próximo crash do launcher deixa rastro.
- **Seletor "Оконный бэкенд"** (Авто / X11 (XWayland) / Wayland) na aba Gráficos, salvo em
  `video_backend` (settings + `bbport.ini`) e exportado como `BB_VIDEO_BACKEND`; i18n pt/en/ru.
  Assim o usuário não precisa mais de env no terminal nem de editar o ini à mão.

## 3. Diagnóstico pendente — separar launcher × jogo (resposta pronta, en-US)

A chave é rodar **fora do Steam** (o Steam engole o stderr) e capturar o traceback do Python,
que é o que distingue um crash de launcher de um crash de jogo:

> Thanks — this is very helpful. On your machine the Wayland path is broken for toolkits, so we
> need to know **where** the fork crashes: the GTK launcher or the game itself. Steam hides the
> error output, so please run these **in a terminal** (not through Steam) and paste the last
> lines:
>
> 1. **Launcher under your exact options** (should crash if it is the launcher):
>    `GDK_BACKEND=x11 QT_QPA_PLATFORM=xcb GSK_RENDERER=cairo ./launcher/bb-launcher.sh`
> 2. **Launcher natively** (should open the window): `./launcher/bb-launcher.sh`
> 3. **Game only, with evidence**:
>    `BB_SAVE_LOG=1 BB_VIDEO_BACKEND=x11 ./launcher/bb-launcher.sh`
>    → attach `logs/<time>.log`. If it opens but no window, also run `hyprctl clients` while it
>    runs (the log now prints `Swapchain: surface 0x0 — window not visible` in that case).
> 4. **What is your Steam "target"?** The AppImage (opens the launcher first) or
>   `bbport-launcher --play` / `start.sh --play` (game only, no GTK window)?
>
> The fork's own test command that should work today:
> `BB_VIDEO_BACKEND=wayland %command%`

## 4. Hipóteses atualizadas

| # | Hipótese | Onde o crash aparece | Experimento |
|---|---|---|---|
| **H4** (nova, alta) | Crash no **launcher GTK4** sob `GDK_BACKEND=x11` + `GSK_RENDERER=cairo` (novo código libadwaita/compat do fork) | Comando 1 crasha; comando 2 abre | Pedir traceback do Python (comando 1) |
| H1 | Crash do jogo na criação da janela SDL sob XWayland | Comando 3: sem `Window WxH on x11` no log | Log do comando 3 |
| H2 | Crash do jogo depois da janela, no grab/warp/cursor novo do fork | Comando 3: com `Window ... on x11` e crash logo após | Log do comando 3 (+ `mouse_look=0` para confirmar) |
| H3 | "Sem janela": janela não mapeada pelo Hyprland | `Swapchain: surface 0x0` no log | Log + `hyprctl clients` |
| H5 | Ambiente Steam (SLR container) + CachyOS | Comando 3 passa fora do Steam mas falha nele | Comparar |

## 5. Próximos passos

1. **Enviar a resposta da §3** e aguardar os 3 comandos.
2. **Se for H4 (launcher):** capturar o traceback; provavelmente um widget/compat do fork sob
   XWayland. Mitigação rápida: `bbport_compat` já shima widgets — verificar `GSK_RENDERER=gl`
   (nosso default) vs cairo; e isolar a inicialização do gamepad SDL (`bbport_gamepad.py`) se ela
   rodar antes da janela.
3. **Se for H1/H2 (jogo):** aplicar o fix do plano v1 (adiar grab/capture para depois do primeiro
   present; fallback automático para `wayland` quando a janela X11 não nasce).
4. Launcher: linha "Backend da janela" (Auto/X11/Wayland) + i18n (etapa 4 do plano v1).
5. Release prebuilt e re-teste A/B com o usuário; fechar o relato com a seção "Wayland
   (Hyprland/KDE)" no README.

## 6. Critérios de aceitação

- Launcher abre e funciona no Hyprland **com e sem** os options do usuário.
- Jogo abre sem crash com `BB_VIDEO_BACKEND=x11` **e** `BB_VIDEO_BACKEND=wayland`.
- Todo relato futuro de "não abre" tem no log a linha de decisão do backend + criação da
  janela/surface (0x0 declarado).

## 7. Riscos

- O launcher do fork roda SDL3 (gamepad) dentro do mesmo processo do GTK; se a inicialização
  SDL do gamepad interagir com `GDK_BACKEND=x11`, pode ser a causa do crash de launcher — checar
  primeiro.
- Se o usuário usa o AppImage dentro do container do Steam, somar a variável `LD_LIBRARY_PATH`
  do fork (H5); o comando fora do Steam já separa isso.
