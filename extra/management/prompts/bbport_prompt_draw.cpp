// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: drawing the port's own button icons -- a key cap with its label, a mouse with the bound
// button lit, another gamepad's face and shoulder buttons, and the key clusters of the d-pad. The
// rasterizer and the text are bbport_prompt_canvas.cpp.
#include "bbport_prompt_internal.h"

#include <algorithm>
#include <cstdint>
#include <string_view>

#include "bbport_prompt_canvas.h"

extern "C" {
// runtime_pad.c
int runtime_pad_prompt(const char* name, int prefer_mouse, char* label, int size);
int runtime_pad_prompt_device(void);
int runtime_pad_mouse_look(void);
__attribute__((weak)) int runtime_pad_is_nintendo(void);
}

namespace BbPrompts::detail {
namespace {

using u32 = std::uint32_t;

/// A label of at most four inputs, each with a short name.
constexpr int kMaxInputs = 4;
constexpr std::size_t kMaxLabelBytes = 32;

/// A key cap with its label.
void Key(Canvas& c, std::string_view label, float cx, float cy, float hw, float hh) {
    const float stroke = std::max(3.0f, hw * 0.09f);
    const float radius = std::min(hw, hh) * 0.32f;
    Plate(c, stroke, [&](float x, float y) { return RoundRect(x, y, cx, cy, hw, hh, radius); });
    const std::size_t glyphs = GlyphCount(label);
    // One character fills the cap; longer names get smaller.
    const float max_h = (hh - stroke) * (glyphs <= 1 ? 1.15f : glyphs <= 3 ? 0.9f : 0.7f);
    Text(c, label, cx, cy, (hw - stroke) * 1.55f, max_h, kInk);
}

/// A mouse, the bound button lit: Left, Right, Middle, X1, X2, Wheel Up, Wheel Down; Move: none
/// lit, arrows at its sides (the camera).
void Mouse(Canvas& c, std::string_view button) {
    constexpr float cx = 64, cy = 66, hw = 33, hh = 50, r = 31, stroke = 5;
    constexpr float split = 58;      // the buttons' lower edge
    constexpr float wheel_top = 28;  // the wheel's ends
    constexpr float wheel_bottom = 46;
    constexpr float arrow_y = 90;    // where the wheel arrows sit

    const auto body = [&](float x, float y) { return RoundRect(x, y, cx, cy, hw, hh, r); };
    c.Paint(kFill, [&](float x, float y) { return Cover(body(x, y)); });

    const auto lit = [&](auto&& region) {
        c.Paint(kAccent, [&](float x, float y) {
            return std::min(Cover(body(x, y) + stroke), region(x, y));
        });
    };
    if (button == "Left") {
        lit([&](float x, float y) { return std::min(Cover(x - cx), Cover(y - split)); });
    } else if (button == "Right") {
        lit([&](float x, float y) { return std::min(Cover(cx - x), Cover(y - split)); });
    }

    // The buttons' lines.
    c.Paint(kFrame, [&](float x, float y) {
        return Cover(Segment(x, y, cx, cy - hh + 2, cx, split, 2.0f));
    });
    c.Paint(kFrame, [&](float x, float y) {
        return Cover(Segment(x, y, cx - hw, split, cx + hw, split, 2.0f));
    });

    // The wheel.
    const bool wheel = button == "Middle" || button == "Wheel Up" || button == "Wheel Down";
    c.Paint(kFill, [&](float x, float y) {
        return Cover(Segment(x, y, cx, wheel_top, cx, wheel_bottom, 7.5f));
    });
    c.Paint(wheel ? kAccent : kFrame, [&](float x, float y) {
        const float d = Segment(x, y, cx, wheel_top, cx, wheel_bottom, 5.5f);
        return wheel ? Cover(d) : Cover(std::abs(d + 1.25f) - 1.25f);
    });
    if (button == "Wheel Up" || button == "Wheel Down") {
        const bool up = button == "Wheel Up";
        Text(c, up ? "\xe2\x96\xb2" : "\xe2\x96\xbc", cx, arrow_y, 26, 20, kAccent);
    }
    if (button == "X1" || button == "X2") {
        const float y = button == "X1" ? 92 : 74; // X1 back (lower), X2 forward
        c.Paint(kAccent, [&](float x, float yy) {
            return Cover(RoundRect(x, yy, cx - hw + 1, y, 6, 8, 4));
        });
    }

    c.Paint(kFrame, [&](float x, float y) {
        return Cover(std::abs(body(x, y) + stroke * 0.5f) - stroke * 0.5f);
    });
    if (button == "Move") {
        Text(c, "\xe2\x86\x90", 14, cy, 24, 22, kInk);
        Text(c, "\xe2\x86\x92", 114, cy, 24, 22, kInk);
    }
}

/// The colour of a face button of an Xbox pad; `kInk` for anything else.
Color FaceInk(std::string_view label) {
    if (label == "A") {
        return Color{0.40f, 0.76f, 0.30f};
    }
    if (label == "B") {
        return Color{0.88f, 0.27f, 0.23f};
    }
    if (label == "X") {
        return Color{0.27f, 0.55f, 0.92f};
    }
    if (label == "Y") {
        return Color{0.95f, 0.78f, 0.18f};
    }
    return kInk;
}

/// The glyphs some pads print instead of a name: +, -, Menu, View.
void RoundGlyph(Canvas& c, std::string_view label) {
    if (label == "+") {
        // Nintendo Plus button: clean horizontal and vertical bars
        c.Paint(kInk, [&](float x, float y) { return Cover(Segment(x, y, 46.0f, 64.0f, 82.0f, 64.0f, 4.5f)); });
        c.Paint(kInk, [&](float x, float y) { return Cover(Segment(x, y, 64.0f, 46.0f, 64.0f, 82.0f, 4.5f)); });
    } else if (label == "-") {
        // Nintendo Minus button: clean horizontal bar
        c.Paint(kInk, [&](float x, float y) { return Cover(Segment(x, y, 48.0f, 64.0f, 80.0f, 64.0f, 4.5f)); });
    } else if (label == "Menu") {
        for (const float y : {48.0f, 64.0f, 80.0f}) {
            c.Paint(kInk, [&](float x, float yy) { return Cover(Segment(x, yy, 44, y, 84, y, 4.0f)); });
        }
    } else if (label == "View") {
        const auto box = [&](float bx, float by) {
            return [=](float x, float y) { return RoundRect(x, y, bx, by, 15, 13, 4); };
        };
        const auto back = box(56, 56), front = box(72, 72);
        c.Paint(kInk, [&](float x, float y) { return Cover(std::abs(back(x, y) + 2.0f) - 2.0f); });
        c.Paint(kFill, [&](float x, float y) { return Cover(front(x, y)); });
        c.Paint(kInk, [&](float x, float y) { return Cover(std::abs(front(x, y) + 2.0f) - 2.0f); });
    }
}

/// A round button of another gamepad: A B X Y in their colours (or monochrome for Nintendo),
/// else its name (LS, RS) or sign (Menu, View, +, -).
void Round(Canvas& c, std::string_view label, bool face) {
    constexpr float cx = 64, cy = 64, radius = 56, stroke = 5;
    Plate(c, stroke, [&](float x, float y) { return Circle(x, y, cx, cy, radius); });
    if (face) {
        const bool is_nintendo = runtime_pad_is_nintendo && runtime_pad_is_nintendo();
        // Nintendo Switch uses clean monochrome white lettering.
        Text(c, label, cx, cy, 70, 58, is_nintendo ? kInk : FaceInk(label));
        return;
    }
    if (label == "+" || label == "-" || label == "Menu" || label == "View") {
        RoundGlyph(c, label);
        return;
    }
    Text(c, label, cx, cy, 78, label.size() <= 2 ? 46 : 34, kInk);
}

/// A shoulder button or a trigger of another gamepad.
void Shoulder(Canvas& c, std::string_view label) {
    const bool trigger = label == "LT" || label == "RT" || label == "ZL" || label == "ZR";
    const float hw = trigger ? 48 : 58, hh = trigger ? 54 : 34, r = trigger ? 26 : 16;
    Plate(c, kPlateStroke, [&](float x, float y) { return RoundRect(x, y, 64, 64, hw, hh, r); });
    Text(c, label, 64, 64, hw * 1.4f, 42, kInk);
}

/// One input of a key cluster: its cap as large as the cluster has room for.
void ClusterKey(Canvas& c, const char (*labels)[kMaxLabelBytes], const int* kinds, int i, float cx,
                float cy, float hw, float hh) {
    Key(c, kinds[i] == 'K' ? labels[i] : "", cx, cy, hw, hh);
}

/// The key clusters (the keyboard only: on a gamepad the d-pad's icons are the game's).
bool DrawCluster(const IconDef& icon, Canvas& c, const char (*labels)[kMaxLabelBytes],
                 const int* kinds) {
    bool any = false;
    for (int i = 0; i < kMaxInputs; ++i) {
        any = any || kinds[i] == 'K';
    }
    if (!any) {
        return false;
    }
    switch (icon.layout) {
    case Layout::Column:
        ClusterKey(c, labels, kinds, 0, 64, 34, 34, 28);
        ClusterKey(c, labels, kinds, 1, 64, 94, 34, 28);
        break;
    case Layout::Row:
        ClusterKey(c, labels, kinds, 0, 34, 64, 28, 34);
        ClusterKey(c, labels, kinds, 1, 94, 64, 28, 34);
        break;
    default: // Cross, Stick: the first over the other three
        ClusterKey(c, labels, kinds, 0, 64, 30, 20, 21);
        ClusterKey(c, labels, kinds, 1, 21, 86, 20, 21);
        ClusterKey(c, labels, kinds, 2, 64, 86, 20, 21);
        ClusterKey(c, labels, kinds, 3, 107, 86, 20, 21);
        break;
    }
    return true;
}

/// One icon for the device in use; false: the game's own stays.
bool Draw(const IconDef& icon, Canvas& c) {
    char labels[kMaxInputs][kMaxLabelBytes] = {};
    int kinds[kMaxInputs] = {};
    const bool single = icon.layout == Layout::Single;
    for (int i = 0; i < kMaxInputs && icon.inputs[i]; ++i) {
        kinds[i] = runtime_pad_prompt(icon.inputs[i], single ? 1 : 0, labels[i], kMaxLabelBytes);
    }
    const int device = runtime_pad_prompt_device();
    if (icon.layout == Layout::Stick && device == 2) {
        if (std::string_view{icon.inputs[0]} == "look_up" && runtime_pad_mouse_look()) {
            Mouse(c, "Move");
            return true;
        }
    } else if (icon.layout == Layout::Stick || single) {
        switch (kinds[0]) {
        case 'K':
            Key(c, labels[0], 64, 64, 56, 48);
            return true;
        case 'M':
            Mouse(c, labels[0]);
            return true;
        case 'F':
            Round(c, labels[0], true);
            return true;
        case 'S':
            Shoulder(c, labels[0]);
            return true;
        case 'C':
            Round(c, labels[0], false);
            return true;
        default:
            return false;
        }
    }
    if (device != 2) {
        return false;
    }
    return DrawCluster(icon, c, labels, kinds);
}

} // namespace

bool DrawIcon(const IconDef& icon, std::uint32_t* out) {
    Canvas canvas;
    if (!Draw(icon, canvas)) {
        return false;
    }
    canvas.Store(out);
    return true;
}

} // namespace BbPrompts::detail
