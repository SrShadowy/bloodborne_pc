// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Temporal Upscaler section: FSR 3/4/4.1.1, TAA, presets, sharpening
// and jitter (bbport_strings.cpp looks these up by StringId).
#include "bbport_strings_internal.h"

namespace BbStrings::detail {
namespace {

constexpr Entry kEntries[] = {
    {StringId::WindowTitle,
     {"Bloodborne — Settings", "Bloodborne — Configurações", "Bloodborne — настройки"}},
    {StringId::FpsMs,
     {"ms", "ms", "мс"}},
    {StringId::SectionLanguage,
     {"Language", "Idioma", "Язык"}},
    {StringId::LanguageInterface,
     {"Interface Language", "Idioma da Interface", "Язык интерфейса"}},
    {StringId::SectionUpscaler,
     {"Temporal Upscaler", "Upscaler Temporal", "Временной апскейлер"}},
    {StringId::UpscalerCombo,
     {"Upscaler", "Upscaler", "Апскейлер"}},
    {StringId::UpscalerOff,
     {"Off", "Desligado", "Выкл"}},
    {StringId::UpscalerTaa,
     {"TAA (Native AA)", "TAA (Anti-aliasing Nativo)", "TAA (нативное сглаживание)"}},
    {StringId::UpscalerNotSupported,
     {"— not supported by GPU", "— não suportado pela GPU", "— не поддерживается видеокартой"}},
    {StringId::UpscalerWorkInProgress,
     {"— work in progress", "— em desenvolvimento", "— в работе"}},
    {StringId::Fsr4Unavailable,
     {"FSR 4 unavailable", "FSR 4 indisponível", "FSR 4 недоступен"}},
    {StringId::Fsr4ActiveModeSelected,
     {"Active mode selected above. FSR 4 can be chosen again.", "Modo ativo selecionado acima. O FSR 4 pode ser selecionado novamente.", "Активен режим, выбранный выше. FSR 4 можно выбрать снова."}},
    {StringId::HintFsr411,
     {"FSR 4.1.1 in INT8 mode: model from AMD 4.1.1 DLL, replayed in Vulkan (bit-exact result). One model for Native..Performance and separate for Ultra Performance. Assets: tools/fsr4cap/build_assets.sh (needs DLL and Proton).", "FSR 4.1.1 em modo INT8: modelo da DLL AMD 4.1.1, reproduzido em Vulkan (resultado idêntico à DLL). Um modelo para Native..Performance e outro para Ultra Performance. Recursos: tools/fsr4cap/build_assets.sh (requer DLL e Proton).", "FSR 4.1.1 в режиме INT8: модель из DLL AMD 4.1.1, воспроизведённая в Vulkan (результат совпадает с DLL). Одна модель для Native..Performance и отдельная для Ultra Performance. Ассеты: tools/fsr4cap/build_assets.sh (нужны DLL и Proton)."}},
    {StringId::HintFsr411Fp8,
     {"FSR 4.1.1 in FP8 mode (Float8 cooperative matrices on RDNA4): model from AMD 4.1.1 DLL, replayed in Vulkan with native float precision.", "FSR 4.1.1 em modo FP8 (Float8 / matrizes cooperativas no RDNA4): modelo da DLL AMD 4.1.1, reproduzido em Vulkan com precisão float nativa.", "FSR 4.1.1 в режиме FP8 (Float8 / кооперативные матрицы RDNA4): модель из DLL AMD 4.1.1, воспроизведённая в Vulkan с точностью float."}},
    {StringId::HintFsr4,
     {"FSR 4 in INT8 mode (v07 model from AMD FidelityFX SDK source). Higher quality than FSR 3.1, heavier pass. Preset change rebuilds model (brief pause). Assets: tools/fetch_fsr4_assets.sh.", "FSR 4 em modo INT8 (modelo v07 do código-fonte AMD FidelityFX SDK). Qualidade superior ao FSR 3.1, porém mais pesado. Mudar o preset recompila o modelo (breve pausa). Recursos: tools/fetch_fsr4_assets.sh.", "FSR 4 в режиме INT8 (модель v07 из исходников AMD FidelityFX SDK). Качество выше, чем у FSR 3.1, но проход тяжелее. Смена пресета пересобирает модель (короткая пауза). Ассеты: tools/fetch_fsr4_assets.sh."}},
    {StringId::Fsr4AutoExposure,
     {"FSR 4: auto-exposure", "FSR 4: auto-exposição", "FSR 4: автоэкспозиция"}},
    {StringId::Fsr4InvertJitter,
     {"FSR 4: invert jitter sign", "FSR 4: inverter sinal de jitter", "FSR 4: инвертировать знак jitter"}},
    {StringId::HintFsr4Ghosting,
     {"Ghosting test: FSR 4 normalizes color by exposure and decides when to discard previous frames. Applies immediately.", "Teste de ghosting: o FSR 4 normaliza a cor pela exposição e decide quando descartar quadros anteriores. Aplica-se imediatamente.", "Тест на шлейфы (ghosting): FSR 4 нормирует цвет по экспозиции и по ней решает, когда отбросить прошлые кадры. Применяется сразу, без перезапуска."}},
    {StringId::PresetCombo,
     {"Preset", "Predefinição", "Пресет"}},
    {StringId::PresetRenderWord,
     {"render", "render", "рендер"}},
    {StringId::HintTaa,
     {"TAA antialiases scene at output resolution without FSR model or upscaling. Saved FSR preset restores upon selecting FSR.", "O TAA suaviza a cena na resolução de saída, sem modelo FSR ou upscaling. O preset do FSR é restaurado ao selecioná-lo.", "TAA сглаживает сцену в разрешении вывода, без модели FSR и апскейлинга. Сохранённый пресет FSR восстановится при выборе FSR."}},
    {StringId::ActiveSceneRender,
     {"Active scene render", "Renderização ativa da cena", "Активный рендер сцены"}},
    {StringId::StartupPreset,
     {"Startup preset", "Predefinição inicial", "Пресет при запуске"}},
    {StringId::HintRenderResAuto,
     {"Non-1080p output renders entire game at preset resolution (startup patch): fastest on Steam Deck/weak GPUs. Preset/output change requires restart. 'Dynamic Resolution Change' below enables change without restart (postprocessing stays 1080p, slower).", "Em saídas diferentes de 1080p o jogo renderiza na resolução do preset (patch inicial): mais rápido no Steam Deck e GPUs modestas. Alterações exigem reinício. A opção 'Mudança de Resolução em Tempo Real' abaixo permite trocar sem reiniciar (o pós-processamento permanece em 1080p, mais lento).", "При выводе не 1080p вся игра рисуется в разрешении пресета (патч при запуске): это быстрее всего на Steam Deck и слабых GPU. Смена пресета или разрешения вывода — после перезапуска. Пункт «Смена разрешения на лету» ниже включает смену без перезапуска (постобработка тогда остаётся в 1080p, медленнее)."}},
    {StringId::HintRenderResManual,
     {"BB_RENDER_RES fixes scene size at start. Remove this variable to change resolution and presets without restarting.", "BB_RENDER_RES fixa o tamanho da cena na inicialização. Remova esta variável para alterar resoluções sem reiniciar.", "BB_RENDER_RES фиксирует размер сцены при запуске. Уберите эту переменную, чтобы менять разрешение и пресеты без перезапуска игры."}},
    {StringId::HintRenderResLive,
     {"Native AA: FSR acts as antialiasing. Other presets reduce render resolution relative to output. UI renders at output resolution. Preset applies next frame without restart.", "Native AA: o FSR age como anti-aliasing. Outros presets reduzem a resolução da cena em relação à saída. A interface renderiza na resolução de saída. Preset aplica-se no próximo quadro.", "Native AA: FSR работает как сглаживание. Остальные пресеты уменьшают разрешение отрисовки сцены относительно вывода. Интерфейс рисуется в разрешении вывода. Пресет применяется со следующего кадра без перезапуска игры."}},
    {StringId::SharpeningRcas,
     {"Sharpening (RCAS)", "Nitidez (RCAS)", "Резкость (RCAS)"}},
    {StringId::SharpnessIntensity,
     {"Sharpness intensity", "Intensidade de nitidez", "Сила резкости"}},
    {StringId::HintSharpness,
     {"Up to 1.0 is upscaler's native RCAS. Above 1.0 adds an extra RCAS pass. Ctrl+Click to type exact value.", "Até 1.0 é o RCAS nativo do upscaler. Acima de 1.0 adiciona um passe extra de RCAS. Ctrl+Clique para digitar o valor exato.", "До 1 — резкость самого апскейлера (RCAS). Выше 1 добавляется ещё один проход RCAS. Ctrl+клик по ползунку — ввести точное значение."}},
    {StringId::SubpixelJitter,
     {"Subpixel jitter", "Deslocamento subpixel (jitter)", "Субпиксельный сдвиг (jitter)"}},
    {StringId::HintJitter,
     {"Each frame the scene shifts by a fraction of a pixel, allowing the upscaler to reconstruct more details. Without it, only history smoothing occurs.", "A cada quadro a cena é deslocada por uma fração de pixel, permitindo ao upscaler reconstruir mais detalhes. Sem isso, ocorre apenas suavização temporal.", "Каждый кадр сцена сдвигается на долю пикселя, и апскейлер собирает из нескольких кадров больше деталей. Без него получается только сглаживание по истории."}},
};

} // namespace

Table UpscalerTable() {
    return kEntries;
}

} // namespace BbStrings::detail
