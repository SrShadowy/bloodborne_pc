// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the little software rasterizer the button prompts are drawn with -- the canvas' pixel
// store, signed-distance shapes with one pixel of smoothing, and labels set in the embedded
// DejaVu Sans (Latin, Cyrillic, arrows), so a Russian or Portuguese binding still fits the icon.
#include "bbport_prompt_canvas.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

extern "C" {
// bbport_overlay.cpp: DejaVu Sans, embedded.
extern const unsigned char bb_font_ttf[];
}

namespace BbPrompts::detail {
namespace {

/// The reference height the font is measured at before it is scaled to fit.
constexpr float kMeasureHeight = 100.0f;

stbtt_fontinfo g_font;
bool g_font_ready = false;

std::u32string Decode(std::string_view utf8) {
    std::u32string out;
    for (std::size_t i = 0; i < utf8.size();) {
        std::uint32_t c = std::uint8_t(utf8[i]);
        int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        c &= extra == 3 ? 0x07 : extra == 2 ? 0x0f : extra == 1 ? 0x1f : 0x7f;
        ++i;
        for (; extra > 0 && i < utf8.size(); --extra, ++i) {
            c = c << 6 | (std::uint8_t(utf8[i]) & 0x3f);
        }
        out.push_back(char32_t(c));
    }
    return out;
}

/// The ink box of `glyphs` at `scale` (subpixel offsets kept, so the pen's fraction is honoured).
bool Measure(const std::u32string& glyphs, float scale, int& x0, int& y0, int& x1, int& y1) {
    float pen = 0.0f;
    x0 = y0 = 1 << 20;
    x1 = y1 = -(1 << 20);
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        int advance = 0, bearing = 0, gx0, gy0, gx1, gy1;
        stbtt_GetCodepointHMetrics(&g_font, int(glyphs[i]), &advance, &bearing);
        stbtt_GetCodepointBitmapBoxSubpixel(&g_font, int(glyphs[i]), scale, scale,
                                            pen - std::floor(pen), 0.0f, &gx0, &gy0, &gx1, &gy1);
        const int ox = int(std::floor(pen));
        if (gx1 > gx0) {
            x0 = std::min(x0, ox + gx0);
            x1 = std::max(x1, ox + gx1);
            y0 = std::min(y0, gy0);
            y1 = std::max(y1, gy1);
        }
        pen += advance * scale;
        if (i + 1 < glyphs.size()) {
            pen += stbtt_GetCodepointKernAdvance(&g_font, int(glyphs[i]), int(glyphs[i + 1])) * scale;
        }
    }
    return x1 > x0;
}

} // namespace

void Canvas::Store(std::uint32_t* out) const {
    for (std::size_t i = 0; i < std::size_t(kCanvasSide) * kCanvasSide; ++i) {
        const float a = px[i * 4 + 3];
        const auto unit = [&](float v) {
            const float straight = a > 0.0f ? std::clamp(v / a, 0.0f, 1.0f) : 0.0f;
            return std::uint32_t(std::lround(straight * 255.0f));
        };
        out[i] = unit(px[i * 4]) | unit(px[i * 4 + 1]) << 8 | unit(px[i * 4 + 2]) << 16 |
                 std::uint32_t(std::lround(std::clamp(a, 0.0f, 1.0f) * 255.0f)) << 24;
    }
}

float Cover(float distance) {
    return std::clamp(0.5f - distance, 0.0f, 1.0f);
}

float RoundRect(float x, float y, float cx, float cy, float hw, float hh, float r) {
    const float qx = std::abs(x - cx) - (hw - r), qy = std::abs(y - cy) - (hh - r);
    const float outside = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
    return outside + std::min(std::max(qx, qy), 0.0f) - r;
}

float Circle(float x, float y, float cx, float cy, float r) {
    return std::hypot(x - cx, y - cy) - r;
}

float Segment(float x, float y, float ax, float ay, float bx, float by, float half_width) {
    const float px = x - ax, py = y - ay, dx = bx - ax, dy = by - ay;
    const float t =
        std::clamp((px * dx + py * dy) / std::max(dx * dx + dy * dy, 1e-6f), 0.0f, 1.0f);
    return std::hypot(px - dx * t, py - dy * t) - half_width;
}

bool InitFont() {
    g_font_ready =
        stbtt_InitFont(&g_font, bb_font_ttf, stbtt_GetFontOffsetForIndex(bb_font_ttf, 0)) != 0;
    return g_font_ready;
}

std::size_t GlyphCount(std::string_view utf8) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        // A continuation byte (10xxxxxx) belongs to the code point before it.
        if ((std::uint8_t(utf8[i]) & 0xc0) != 0x80) {
            ++count;
        }
    }
    return count;
}

void Text(Canvas& c, std::string_view text, float cx, float cy, float max_w, float max_h, Color ink) {
    if (!g_font_ready || text.empty()) {
        return;
    }
    const std::u32string glyphs = Decode(text);
    float scale = stbtt_ScaleForPixelHeight(&g_font, kMeasureHeight);
    int x0, y0, x1, y1;
    if (!Measure(glyphs, scale, x0, y0, x1, y1)) {
        return;
    }
    scale *= std::min(max_w / float(x1 - x0), max_h / float(y1 - y0));
    Measure(glyphs, scale, x0, y0, x1, y1);

    const float left = std::round(cx - float(x1 - x0) * 0.5f) - float(x0);
    const float base = std::round(cy - float(y1 - y0) * 0.5f) - float(y0);
    float pen = 0.0f;
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        int advance = 0, bearing = 0;
        stbtt_GetCodepointHMetrics(&g_font, int(glyphs[i]), &advance, &bearing);
        const float shift = pen - std::floor(pen);
        int w = 0, h = 0, gx = 0, gy = 0;
        unsigned char* bitmap = stbtt_GetCodepointBitmapSubpixel(&g_font, scale, scale, shift, 0.0f,
                                                                 int(glyphs[i]), &w, &h, &gx, &gy);
        if (bitmap) {
            const int ox = int(left + std::floor(pen)) + gx, oy = int(base) + gy;
            c.Paint(ink, [&](float x, float y) {
                const int bx = int(x) - ox, by = int(y) - oy;
                return bx >= 0 && by >= 0 && bx < w && by < h ? bitmap[by * w + bx] / 255.0f : 0.0f;
            });
            stbtt_FreeBitmap(bitmap, nullptr);
        }
        pen += advance * scale;
        if (i + 1 < glyphs.size()) {
            pen += stbtt_GetCodepointKernAdvance(&g_font, int(glyphs[i]), int(glyphs[i + 1])) * scale;
        }
    }
}

} // namespace BbPrompts::detail
