// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the post-processing, lighting and camera sections (bbport_strings.cpp looks these up by StringId).
#include "bbport_strings_internal.h"

namespace BbStrings::detail {
namespace {

constexpr Entry kEntries[] = {
    {StringId::SectionPostProcess,
     {"Post-Processing & Lighting (Live)", "Pós-Processamento e Iluminação (Tempo Real)", "Постобработка и освещение (на лету)"}},
    {StringId::BloomIntensity,
     {"Bloom / Glare Intensity", "Intensidade de Bloom / Brilho", "Интенсивность свечения (Bloom)"}},
    {StringId::HintBloom,
     {"Scales YEBIS glare luminance in real time. Setting to 0.0 skips the glare passes completely for higher GPU performance.", "Ajusta a luminância do brilho YEBIS em tempo real. Definir como 0.0 pula completamente os passes de glare, economizando GPU.", "Масштабирует яркость свечения YEBIS в реальном времени. Значение 0.0 полностью пропускает проходы свечения, ускоряя GPU."}},
    {StringId::ColorSaturation,
     {"Color Saturation", "Saturação de Cor", "Насыщенность цвета"}},
    {StringId::HintSaturation,
     {"Scales the tone-mapper's color saturation in real time (0.0 = grayscale, 1.0 = game default).", "Ajusta a saturação de cores do tone-mapper em tempo real (0.0 = preto e branco, 1.0 = padrão do jogo).", "Масштабирует насыщенность цвета тональной компрессии в реальном времени (0.0 = Ч/Б, 1.0 = по умолчанию)."}},
    {StringId::VignetteEnable,
     {"Screen Edge Vignette", "Vinheta nas Bordas da Tela", "Виньетирование по краям экрана"}},
    {StringId::HintVignette,
     {"Toggles the area's YEBIS screen-edge darkening in real time.", "Ativa ou desativa o escurecimento nas bordas da tela (YEBIS Vignette) em tempo real.", "Включает или выключает затемнение по краям экрана (YEBIS Vignette) в реальном времени."}},
    {StringId::SsaoStrength,
     {"SSAO Occlusion Strength", "Intensidade da Oclusão SSAO", "Сила затенения SSAO"}},
    {StringId::HintSsaoStrength,
     {"Scales ambient occlusion contrast in recesses (1.0 = game default, 2.0 = twice as deep).", "Ajusta a profundidade da oclusão ambiental nos cantos e frestas (1.0 = padrão do jogo, 2.0 = dobro de contraste).", "Масштабирует глубину затенения SSAO в углублениях (1.0 = по умолчанию, 2.0 = вдвое глубже)."}},
    {StringId::ShadowDistance,
     {"Shadow Cascade Distance", "Distância das Cascatas de Sombra", "Дальность каскадов теней"}},
    {StringId::HintShadowDistance,
     {"Scales directional shadow cascade splits and fade distance in real time (1.0 = game default).", "Escala o alcance das cascatas de sombras direcionais em tempo real (1.0 = padrão do jogo).", "Масштабирует дальность каскадов направленных теней в реальном времени (1.0 = по умолчанию)."}},
    {StringId::FogOpacity,
     {"Atmospheric Fog Opacity", "Opacidade da Neblina Atmosférica", "Плотность атмосферного тумана"}},
    {StringId::HintFogOpacity,
     {"Scales distant street haze and height fog opacity in real time (0.0 = clear distant view, 1.0 = game default).", "Ajusta a densidade da neblina distante e de altura em tempo real (0.0 = horizonte limpo, 1.0 = padrão do jogo).", "Масштабирует плотность дальнего и высотного тумана в реальном времени (0.0 = чистый горизонт, 1.0 = по умолчанию)."}},
    {StringId::SectionCamera,
     {"Camera & Field of View (Live)", "Câmera e Campo de Visão (Tempo Real)", "Камера и угол обзора (на лету)"}},
    {StringId::CameraFov,
     {"Field of View (FOV Scale)", "Campo de Visão (Escala de FOV)", "Угол обзора (масштаб FOV)"}},
    {StringId::HintCameraFov,
     {"Uncaps the 48-degree limit and scales LockCamParam vertical FOV in real time (1.0 = 43° default).", "Desbloqueia o limite de 48° e escala o FOV vertical da câmera em tempo real (1.0 = 43° padrão).", "Снимает ограничение в 48° и масштабирует вертикальный угол обзора камеры в реальном времени (1.0 = 43°)."}},
    {StringId::CameraDistance,
     {"Camera Distance", "Distância da Câmera", "Дистанция камеры"}},
    {StringId::HintCameraDistance,
     {"Scales follow camera target distance from the character in real time (1.0 = 4 m default).", "Ajusta a distância da câmera em relação ao personagem em tempo real (1.0 = 4 m padrão).", "Масштабирует дистанцию камеры от персонажа в реальном времени (1.0 = 4 м по умолчанию)."}},
    {StringId::CameraHeight,
     {"Camera Height", "Altura da Câmera", "Высота камеры"}},
    {StringId::HintCameraHeight,
     {"Scales follow camera vertical pivot offset above the character in real time (1.0 = default).", "Ajusta a altura do ponto de foco da câmera sobre o personagem em tempo real (1.0 = padrão).", "Масштабирует высоту точки фокуса камеры над персонажем в реальном времени (1.0 = по умолчанию)."}},
    {StringId::LightGridOpt,
     {"4x4 Quarter-Window Light Grid", "Grade de Luzes 4x4 (Otimização)", "Сетка освещения 4x4 (оптимизация)"}},
    {StringId::HintLightGridOpt,
     {"Replaces 3x3 jobs of 128x128 tiles with 1 job of quarter-window 4x4 tiles, eliminating ~80k redundant light draw calls per 300 frames.", "Substitui 3x3 jobs de tiles 128x128 por 1 job de grade 4x4 proporcional à janela, eliminando ~80 mil draw calls redundantes de luzes.", "Заменяет девять задач с тайлами 128×128 одной задачей с сеткой 4×4 по размеру окна, убирая ~80 тыс. лишних вызовов отрисовки света."}},
    {StringId::RawMouseCamera,
     {"Raw 1:1 Mouse Camera (DS3 Mode)", "Câmera Mouse Raw 1:1 (Modo DS3)", "Прямой ввод мыши 1:1 (режим DS3)"}},
    {StringId::HintRawMouseCamera,
     {"Turns the camera directly by raw mouse counts without stick deadzone or speed cap, and switches lock-on targets with a mouse flick.", "Gira a câmera diretamente pelos counts brutos do mouse sem zona morta ou limite de analógico, e troca o alvo travado (Lock-On) com flick do mouse.", "Поворачивает камеру напрямую по счёту мыши без мёртвой зоны и потолка скорости стика; рывок мыши переключает цель захвата."}},
    {StringId::MouseHoldAutoRotate,
     {"Suppress Camera Auto-Rotation on Mouse", "Suprimir Auto-Rotação da Câmera com Mouse", "Отключать автоповорот камеры для мыши"}},
    {StringId::HintMouseHoldAutoRotate,
     {"Prevents the camera from rotating on its own when walking sideways with keyboard & mouse; restores auto-rotation automatically when a gamepad stick is used.", "Impede que a câmera gire sozinha ao andar de lado com teclado e mouse; restaura a auto-rotação automaticamente ao usar o controle.", "Не даёт камере поворачиваться самой при движении боком на клавиатуре и мыши; автоматически возвращает автоповорот при касании стика геймпада."}},
};

} // namespace

Table CameraTable() {
    return kEntries;
}

} // namespace BbStrings::detail
