// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the little software rasterizer the button prompts are drawn with -- a premultiplied
// float RGBA canvas, signed-distance shapes with one pixel of smoothing, and text set in the
// embedded DejaVu Sans. bbport_prompt_draw.cpp composes the icons out of these.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "bbport_button_prompts.h"

namespace BbPrompts::detail {

/// The side of an icon, in pixels (the game's are 32x32; the port's are drawn larger).
constexpr int kCanvasSide = int(IconSize);

/// Premultiplied float RGBA.
struct Color {
    float r, g, b, a = 1.0f;
};

/// The palette: the plate the icons sit on, its frame, the labels and the lit input.
constexpr Color kFill{0.075f, 0.07f, 0.065f, 0.93f};
constexpr Color kFrame{0.78f, 0.74f, 0.64f};
constexpr Color kInk{0.95f, 0.92f, 0.85f};
constexpr Color kAccent{0.82f, 0.64f, 0.36f};

struct Canvas {
    std::vector<float> px = std::vector<float>(std::size_t(kCanvasSide) * kCanvasSide * 4, 0.0f);

    /// Paints `c` where `coverage(x, y)` (pixel centres) is above 0.
    template <typename F>
    void Paint(Color c, F&& coverage) {
        for (int y = 0; y < kCanvasSide; ++y) {
            for (int x = 0; x < kCanvasSide; ++x) {
                float k = coverage(float(x) + 0.5f, float(y) + 0.5f);
                if (k <= 0.0f) {
                    continue;
                }
                k = std::min(k, 1.0f) * c.a;
                float* p = &px[(std::size_t(y) * kCanvasSide + x) * 4];
                p[0] = c.r * k + p[0] * (1.0f - k);
                p[1] = c.g * k + p[1] * (1.0f - k);
                p[2] = c.b * k + p[2] * (1.0f - k);
                p[3] = k + p[3] * (1.0f - k);
            }
        }
    }

    /// Straight-alpha RGBA8, as the game's own (TGA-made) icons.
    void Store(std::uint32_t* out) const;
};

/// Coverage of a pixel whose shape is `distance` away from its edge.
float Cover(float distance);
float RoundRect(float x, float y, float cx, float cy, float hw, float hh, float r);
float Circle(float x, float y, float cx, float cy, float r);
float Segment(float x, float y, float ax, float ay, float bx, float by, float half_width);

/// The stroke width the icons' plates use.
constexpr float kPlateStroke = 5.0f;

/// A filled shape with a frame of `stroke` pixels on its edge.
template <typename D>
void Plate(Canvas& c, float stroke, D&& distance, Color fill = kFill, Color frame = kFrame) {
    c.Paint(fill, [&](float x, float y) { return Cover(distance(x, y)); });
    c.Paint(frame, [&](float x, float y) {
        return Cover(std::abs(distance(x, y) + stroke * 0.5f) - stroke * 0.5f);
    });
}

/// Loads the embedded font the labels are set in; false: no labels.
bool InitFont();

/// How many code points `utf8` holds (one per glyph the label draws).
std::size_t GlyphCount(std::string_view utf8);

/// `text` centred on (cx, cy), at most `max_w` wide and `max_h` tall (its ink, not the font's
/// line height: key caps hold capitals and digits).
void Text(Canvas& c, std::string_view text, float cx, float cy, float max_w, float max_h, Color ink);

} // namespace BbPrompts::detail
