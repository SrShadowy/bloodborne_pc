# Frame Generation & Vetores de Movimento (BBPort)

> **Documento de Referência Técnica**  
> **Data:** 16 de Outubro de 2026  
> **Escopo:** Pipeline de Interpolação de Quadros (FSR 3.1 Frame Generation), Vetores de Movimento de Câmera (`CameraMotion`), Vetores de Movimento de Objetos (`ObjectMotion`), Histórico de Vértices (`History`) e Separação de HUD (`CaptureHudless`).

---

## 1. Visão Geral da Arquitetura

O sistema de geração de quadros intercala um quadro sintético gerado a cada dois quadros reais renderizados pelo jogo (ex: 30 FPS nativos $\to$ 60 FPS exibidos; 60 FPS $\to$ 120 FPS).

```
[G-Buffer Passes] ──> [Object Motion] (Vértices passados vs atuais no VS)
                             │
[Pós-Processamento] ──> [Camera Motion] (Reprojeção de profundidade + mescla com objeto)
                             │
[Upscaler / FSR]   ──> [ui_image] (Cena final pós-processada e upscaled)
                             │
                ┌────────────┴────────────────────────┐
                │                                     │
       (1º draw de HUD)                       (Display Pass)
  CaptureHudless(ui_image)                    Interpolate(ui_image)
  [Cena 3D pronta SEM UI]                     [Cena 3D pronta COM UI]
                │                                     │
                └───────────────┬─────────────────────┘
                                ▼
                   [FSR 3.1 Frame Generation]
                 (Subtrai UI ──> Interpola Cena 3D ──> Compõe UI estática)
                                ▼
                       [Quadro Interpolar]
```

### Componentes Principais

| Componente | Arquivo(s) | Papel no Pipeline |
| :--- | :--- | :--- |
| **`ObjectMotion`** | `gpu/upscale/vk_object_motion.cpp/.h` | Aloca buffers de histórico de vértices na GPU (128 MiB ping-pong) e grava posições do frame anterior. |
| **`CameraMotion`** | `gpu/upscale/vk_camera_motion.cpp/.h`, `camera_motion.comp` | Calcula vetores de câmera a partir da matriz de visão inversa e profundidade; mescla vetores de objetos válidos. |
| **`History` & `GateKey`** | `gpu/upscale/motion_history.h` | Bookkeeping de CPU: mapeamento de chamadas de draw (`Draw`), gating de paletas e alocação de slots. |
| **`FrameGeneration`** | `gpu/upscale/vk_frame_generation.cpp/.h` | Interface portátil do FSR 3.1: captura profundidade, vetores de movimento, cena limpa (`hudless`) e despacha interpolação. |
| **`Rasterizer`** | `gpu/render/rasterizer.cpp` | Intercepta draws do G-Buffer, classifica papéis de buffer (`BufferRole`) e ativa vetores por shader. |
| **`TemporalUpscaler`** | `gpu/upscale/vk_temporal_upscaler.cpp` | Coordena a cadeia temporal, resolução de cena e captura do momento exato sem HUD (`RedirectColor`). |

---

## 2. Histórico de Problemas, Causas Raízes e Soluções

### Problema 1: Grades e Cenário Voando / Deslocados (Ghosting de Cenário)
* **Sintoma:** Ao girar a câmera, grades de ferro, cercas e peças repetidas de cenário se duplicavam, tremiam ou eram arremessadas através da tela.
* **Causa Raiz 1 (Classificação de Buffers):** `ClassifyBuffer` considerava qualquer buffer $\ge 640$ bytes como `Skeleton` (esqueleto dinâmico). Buffers uniformes de materiais de cenário (1024, 2048, 4096 bytes) eram classificados erroneamente como esqueletos vivos.
* **Causa Raiz 2 (Ausência de Teto Físico):** O shader `camera_motion.comp` aceitava qualquer vetor de objeto válido sem checar se a velocidade relativa à câmera era fisicamente plausível para um intervalo de 16/33 ms.
* **Solução:**
  1. Em `gpu/upscale/motion_history.h` (`ClassifyBuffer`): implementada validação estrutural de matrizes de ossos $3 \times 4$ (48 bytes por osso):
     $$\text{is\_skeleton} = (\text{size} \pmod{48} == 0) \lor ((\text{size} - 32) \pmod{48} == 0)$$
     Buffers de cenário de 512, 1024, 2048, 4096 bytes retornam `BufferRole::Other` e usam vetores de câmera puros.
  2. Em `gpu/upscale/shaders/camera_motion.comp`: adicionada rejeição de divergência física:
     $$\text{rel\_motion} = \text{object\_motion.rg} - \text{motion}; \quad \text{if } (\text{dot}(\text{rel\_motion}, \text{rel\_motion}) < 16384.0) \implies \text{válido} \; (\le 128\text{ px/frame}).$$

---

### Problema 2: Personagem "Tremido" / Jitter ao Andar (~15 FPS Visual)
* **Sintoma:** O cenário era suave, mas o Hunter parecia rodar a 15 FPS com vibração/tremor violento ao se movimentar.
* **Causa Raiz 1 (Animações a 30 Hz vs Frame Generation):**
  - Bloodborne roda animações a 30 Hz. Em 60 FPS ou Frame Generation, quadros intermediários possuem **a mesma pose de animação** (mesmo hash `palette`).
  - Quando esqueletos de personagens passavam por `Moving(palette)`: no quadro repetido, `Moving()` retornava `false` $\implies$ `motion_param = 0` (desativava vetor de objeto e caía em câmera).
  - No quadro seguinte (novo tick), `Moving()` retornava `true`, mas o quadro anterior não gravou histórico (`load == 0`).
  - Essa alternância (**liga / desliga / liga / desliga**) a cada frame quebrava a interpolação do personagem.
* **Causa Raiz 2 (Endereço Dinâmico na Chave):** A PS4 aloca matrizes de ossos via ring buffer dinâmico do GNM. Adicionar o ponteiro de memória (`address`) na chave do `Draw` fazia a chave mudar todo frame, garantindo `load == 0` perpétuo.
* **Solução:**
  1. Em `gpu/render/rasterizer.cpp`: restaurado `gated = false; break;` para `BufferRole::Skeleton`. O personagem é rastreado **ininterruptamente em 100% dos quadros**.
  2. Em `gpu/upscale/motion_history.h`: removido `address` de `Draw` e `GateKey`. A identidade do draw apoia-se em atributos estáticos da malha (shader hash, layout de vértices, ponteiro estável do buffer de índices, contagem de índices e topologia).

---

### Problema 3: Fantasma / Imagem Dupla do Personagem (~50% Transparente)
* **Sintoma:** O personagem ficava com uma silhueta semitransparente idêntica duplicada ao redor/atrás do corpo ao correr.
* **Causa Raiz:**
  - Uma chamada prematura `CaptureHudless(in.color)` foi adicionada em `CaptureScene`.
  - `in.color` capturava a imagem **antes** de o jogo rodar o tone mapping, iluminação final e pós-processamento.
  - O FSR 3.1 compara `currentColor` com `hudlessColor` para detectar o que é interface (HUD).
  - Devido à diferença de brilho e tone mapping na pele e roupas do Hunter, o FSR considerou o corpo do personagem como "interface estática" e **colou uma cópia não-interpolada com 50% de transparência por cima de cada quadro gerado**.
* **Solução:**
  - Removido `CaptureHudless(in.color)` de `CaptureScene`.
  - A captura sem HUD ocorre **exclusivamente** no ponto nativo em `TemporalUpscaler::RedirectColor` (após todo o pós-processamento 3D terminar, no exato primeiro draw da interface).

---

### Problema 4: Distorção / Rasgo no HUD e Legendas ao Girar a Câmera
* **Sintoma:** Ao girar a câmera rapidamente, os textos das legendas, barras de vida e ícones do HUD entortavam ou rasgavam.
* **Causa Raiz:** Ao anular completamente o `hudlessColor`, o FSR 3.1 caía no modo de fluxo óptico cego para a tela toda. Como o texto é bidimensional e não possui vetores 3D de profundidade, a rotação rápida da câmera fazia o fluxo óptico tentar "mover" as letras das legendas.
* **Solução:**
  - Reativado `dispatch.hudlessColor = Describe(vk::Image(hudless), ...)` quando `hudless_ready` for verdadeiro.
  - Como `hudless` foi capturado no momento exato (pós-processamento concluído, logo antes do 1º draw de UI), a cena 3D é **bit-a-bit idêntica** entre `currentColor` e `hudlessColor`.
  - A **única** diferença detectada pelo FSR são as legendas e o HUD. O FSR as isola e cola estáticas e perfeitas sobre o quadro interpolado, sem qualquer distorção de fluxo óptico.

---

## 3. Resumo de Parâmetros e Convenções Técnicas

| Parâmetro | Valor / Convenção | Justificativa |
| :--- | :--- | :--- |
| **Estrutura de Osso GNM** | 48 bytes ($3 \times 4$ matriz de floats) | Stride 16 (3 registros `vec4`). Hunter: 656 bytes ($32\text{b header} + 13 \times 48$). |
| **Buffers de Cenário** | 512, 1024, 2048, 4096 bytes | Materiais e constantes gerais; descartados como `BufferRole::Other`. |
| **Teto de Velocidade Relativa** | `dot(rel, rel) < 16384.0` | Tolerância de até 128 px de velocidade relativa por frame no espaço de tela. |
| **`motionVectorScale`** | `{1.0f, 1.0f}` | Vetores de movimento no BBPort já são gravados em espaço de pixel (render size). |
| **Ponto de Captura HUDless** | `TemporalUpscaler::RedirectColor` | `ui_phase && color == ui_color && !hudless_taken`. Garante tone mapping idêntico ao frame final. |

---

## 4. Comandos de Verificação e Build

```bash
# Executar teste de regressão de histórico de vetores (CPU standalone)
ninja -C out/gpu motion-history-test && ./out/gpu/motion-history-test

# Compilar biblioteca da GPU e binários nativos
bash build.sh

# Gerar pacote de distribuição portátil (Docker / GCC 13)
python3 package.py --docker
```
