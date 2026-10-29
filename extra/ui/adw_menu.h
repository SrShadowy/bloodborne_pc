// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <imgui.h>

namespace UI::Adw {

/// Function pointer for custom icon drawing inside tab items.
using IconDrawer = void (*)(ImDrawList* draw, int id, ImVec2 pos, float scale, ImU32 col);

/// Adwaita Dark palette and window/frame styling.
class Theme {
public:
    static void Push(float scale = 1.0f);
    static void Pop();
};

/// RAII scope for Adwaita theme.
struct ScopedTheme {
    explicit ScopedTheme(float scale = 1.0f) { Theme::Push(scale); }
    ~ScopedTheme() { Theme::Pop(); }
    ScopedTheme(const ScopedTheme&) = delete;
    ScopedTheme& operator=(const ScopedTheme&) = delete;
};

/// Centered modal window with full-viewport backdrop dimmer.
class ModalWindow {
public:
    static bool Begin(const char* name, bool* p_open, float width, float height, float scale = 1.0f);
    static void End();
};

/// Dual-pane top header bar with brand title, centered active tab title, and close button.
class TopBar {
public:
    struct Config {
        const char* brand_prefix = "";
        const char* brand_title = "";
        const char* active_tab_title = "";
        float sidebar_width = 215.0f;
        float height = 48.0f;
        float scale = 1.0f;
        bool show_close_button = true;
    };

    static void Render(const Config& config, bool* p_open = nullptr);
};

/// Sidebar navigation pane with 5px margins and pill-style tab buttons.
class Sidebar {
public:
    static bool Begin(float sidebar_w, float header_h, float height, float scale = 1.0f);
    static void End();

    /// Renders a tab button with 5px margin from both edges. Returns true when clicked.
    static bool TabItem(int id, const char* label, bool selected, float sidebar_w, float scale = 1.0f,
                        IconDrawer draw_icon = nullptr);
};

/// Main content pane with generous horizontal padding, regulated item width, and auto-hiding scrollbar.
class ContentArea {
public:
    static bool Begin(float sidebar_w, float header_h, float width, float height,
                      float scale = 1.0f, float pad_x = -1.0f, float item_w = -1.0f);
    static void End();

    /// Renders a category subtitle followed by a separator.
    static void Subtitle(const char* subtitle, float scale = 1.0f);
};

} // namespace UI::Adw
