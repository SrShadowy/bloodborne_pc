// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Reactivity and Object Motion Vector sections (bbport_strings.cpp looks these up by StringId).
#include "bbport_strings_internal.h"

namespace BbStrings::detail {
namespace {

constexpr Entry kEntries[] = {
    {StringId::SectionReactivity,
     {"Reactivity Mask", "Máscara de Reatividade", "Маска реактивности"}},
    {StringId::ReactivityEnable,
     {"Enable mask", "Ativar máscara", "Включить маску"}},
    {StringId::HintReactivity,
     {"Marks transparent effects (particles, fog) so the upscaler relies less on previous frames. Reduces ghosting behind particles, but restores jitter under them.", "Marca efeitos transparentes (partículas, névoa) para que o upscaler dependa menos de quadros anteriores. Reduz ghosting atrás de efeitos, mas devolve o jitter sob eles.", "Помечает прозрачные эффекты (частицы, дымку), чтобы апскейлер меньше опирался на прошлые кадры. Меньше шлейфов за эффектами, но под ними возвращается дрожание."}},
    {StringId::ReactivityScale,
     {"Scale", "Escala", "Масштаб"}},
    {StringId::ReactivityThreshold,
     {"Threshold", "Limiar", "Порог"}},
    {StringId::ReactivityMax,
     {"Maximum", "Máximo", "Максимум"}},
    {StringId::ReactivityShowMask,
     {"Show mask (debug)", "Exibir máscara (depuração)", "Показать маску (отладка)"}},
    {StringId::ObjectMotionVectors,
     {"Character motion vectors", "Vetores de movimento de personagens", "Векторы движения персонажей"}},
    {StringId::HintObjectMotion,
     {"Precise vectors for animated objects: clothes and weapons break apart less during motion. Static scene receives no extra pass. Applies after restart.", "Vetores precisos para objetos animados: roupas e armas quebram menos durante o movimento. A cena estática não recebe passe extra. Aplica-se após reiniciar.", "Точные векторы для анимированных объектов: одежда и оружие меньше рассыпаются при движении. Статичная сцена не получает дополнительный проход. Изменение применяется после перезапуска игры."}},
    {StringId::ObjectMotionShowDebug,
     {"Show motion vectors (debug)", "Exibir vetores de movimento (depuração)", "Показать векторы движения (отладка)"}},
    {StringId::HintObjectMotionColors,
     {"Red/Green: horizontal/vertical motion (8 px = full brightness). Blue: pixel received exact object vector, not just camera motion. Moving objects without blue or red/green are treated as stationary by upscaler, causing trails.", "Vermelho/Verde: movimento horizontal/vertical (8 px = brilho total). Azul: o pixel recebeu vetor exato do objeto, não apenas da câmera. Objetos em movimento sem azul/vermelho/verde causam rastros.", "Красный/зелёный: движение по горизонтали/вертикали (8 пикселей = полная яркость). Синий: пиксель получил точный вектор объекта, а не только движение камеры. Движущийся предмет без синего и без красного/зелёного апскейлер считает неподвижным, отсюда шлейф."}},
};

} // namespace

Table VisualsTable() {
    return kEntries;
}

} // namespace BbStrings::detail
