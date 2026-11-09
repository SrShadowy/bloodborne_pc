// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Output Resolution and Frame Generation sections (bbport_strings.cpp looks these up by StringId).
#include "bbport_strings_internal.h"

namespace BbStrings::detail {
namespace {

constexpr Entry kEntries[] = {
    {StringId::SectionOutputResolution,
     {"Output Resolution", "Resolução de Saída", "Разрешение вывода"}},
    {StringId::OutputResolutionCombo,
     {"Output Resolution", "Resolução de Saída", "Разрешение вывода"}},
    {StringId::HintOutputResolutionFixed,
     {"Final frame and UI size. Preset sets scene size relative to output: 4K Performance = 1920x1080. Applies after restart.", "Tamanho final do quadro e interface. O preset define o tamanho da cena relativo à saída: 4K Performance = 1920x1080. Aplica-se após reiniciar.", "Размер готового кадра и интерфейса. Пресет задаёт размер сцены относительно вывода: 4K Performance = 1920x1080. Применяется после перезапуска игры."}},
    {StringId::HintOutputResolutionLive,
     {"Final frame and UI size changes on next frame boundary. Preset sets scene size relative to output: 4K Performance = 1920x1080. Size change resets FSR history and may cause a brief pause.", "Tamanho do quadro final e interface muda no próximo quadro. O preset define a resolução da cena em relação à saída. Mudar o tamanho reseta o histórico do FSR.", "Размер готового кадра и интерфейса меняется на границе следующего кадра. Пресет задаёт размер сцены относительно вывода: 4K Performance = 1920x1080. Смена размера сбрасывает историю FSR и может вызвать короткую паузу."}},
    {StringId::DynamicResolutionChange,
     {"Dynamic Resolution Change", "Mudança de Resolução em Tempo Real", "Смена разрешения на лету"}},
    {StringId::LiveModeAuto,
     {"Auto (by GPU)", "Automático (pela GPU)", "Авто (по видеокарте)"}},
    {StringId::LiveModeDisabled,
     {"Disabled (faster)", "Desativado (mais rápido)", "Выключена (быстрее)"}},
    {StringId::LiveModeEnabled,
     {"Enabled", "Ativado", "Включена"}},
    {StringId::HintLiveResolution,
     {"Enabled: output resolution and preset change without restarting, but postprocessing remains 1080p (noticeably slower on Steam Deck and older GPUs). Disabled: everything renders at preset resolution, changes via restart. Auto enables it on powerful discrete GPUs. Applies after restart.", "Ativado: resolução de saída e presets mudam sem reiniciar, mas o pós-processamento permanece em 1080p (mais lento em hardware modesto). Desativado: renderização na resolução do preset, mudanças exigem reinício. Automático ativa em GPUs potentes.", "Включена: разрешение вывода и пресет меняются без перезапуска, но постобработка игры остаётся в 1080p — на Steam Deck и старых видеокартах это заметно медленнее. Выключена: всё рисуется в разрешении пресета, смена — через перезапуск. Авто включает её на мощных дискретных видеокартах. Применяется после перезапуска игры."}},
    {StringId::SectionFrameGeneration,
     {"Frame Generation", "Geração de Quadros (Frame Generation)", "Генерация кадров (Frame Generation)"}},
    {StringId::FrameGenerationEnable,
     {"Enable FSR 3.1 Frame Generation", "Ativar FSR 3.1 Frame Generation", "Включить FSR 3.1 Frame Generation"}},
    {StringId::HintFrameGeneration,
     {"Generates synthetic interpolated frames between real frames. Requires minImageCount + 2 swapchain support. Independent from the selected upscaler.", "Gera quadros interpolados entre quadros reais. Requer suporte a minImageCount + 2 na swapchain. Opera de forma independente do upscaler selecionado.", "Генерирует интерполированные кадры между реальными. Требует поддержки minImageCount + 2 в swapchain. Независимо от выбранного апскейлера."}},
    {StringId::FrameGenerationNotSupported,
     {"Frame Generation is not supported on this GPU or swapchain configuration.", "Frame Generation não é suportado nesta GPU ou configuração de swapchain.", "Генерация кадров не поддерживается этой видеокартой или конфигурацией swapchain."}},
};

} // namespace

Table OutputTable() {
    return kEntries;
}

} // namespace BbStrings::detail
