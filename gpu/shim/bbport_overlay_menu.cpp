// SPDX-FileCopyrightText: Copyright 2026 bbport contributors
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_overlay.h"
#include "bbport_overlay_internal.h"
#include "bbport_overlay_sections.h"
#include "bbport_settings.h"
#include "ui_manager.h"
#include "adw_menu.h"
#include "imgui.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

extern "C" void runtime_restart(void); // bb-probe (probe.c)

namespace BbOverlay::detail {

static inline const char* L(int lang, const char* en, const char* pt, const char* ru) {
    return (lang == 1) ? pt : (lang == 2) ? ru : en;
}

static const char* TabTitle(int i, int lang) {
    static const char* const t[][3] = {
        {"Game", "Jogo", "Игра"},
        {"Display", "Tela", "Экран"},
        {"Graphics", "Gráficos", "Графика"},
        {"Effects", "Efeitos", "Эффекты"},
        {"Performance", "Desempenho", "Производительность"},
        {"Controls", "Controles", "Управление"},
        {"Mods & Patches", "Mods e patches", "Моды"},
        {"Developer", "Desenvolvedor", "Разработчик"},
        {"Trophies", "Troféus", "Трофеи"},
        {"Online", "Online", "Онлайн"},
        {"Log", "Registro", "Журнал"},
    };
    return (i >= 0 && i < 11) ? t[i][(lang == 1) ? 1 : (lang == 2) ? 2 : 0] : "";
}

static const char* TabSubtitle(int i, int lang) {
    static const char* const s[][3] = {
        {"Game files, language, and process actions.", "Configurações de idioma e gerenciamento do processo.", "Язык и управление процессом."},
        {"Output resolution, aspect ratio, and display mode.", "Resolução de saída, formato de tela e modo de exibição.", "Разрешение и режим экрана."},
        {"Temporal upscaler, internal resolution, and sharpness.", "Apskeler temporal, resolução interna e nitidez.", "Временной апскейлер и резкость."},
        {"Visual post-processing, lighting, and photo mode.", "Pós-processamento visual, iluminação e modo foto.", "Постобработка и фоторежим."},
        {"Frame rate limit, frame generation, and metrics.", "Taxa de quadros, geração de quadros e métricas de desempenho.", "Частота кадров и оверлей."},
        {"Controller, keyboard, and mouse settings.", "Controle, teclado e câmera do mouse.", "Управление, клавиатура e мышь."},
        {"Loaded game modifications and patches.", "Modificações carregadas e patches da comunidade.", "Загруженные модификации."},
        {"Memory inspection and debugging tools.", "Ferramentas de análise de memória e depuração.", "Инструменты отладки."},
        {"Earned achievements and campaign progress.", "Conquistas obtidas e progresso da campanha.", "Трофеи и прогресс."},
        {"Discord Rich Presence and network connectivity.", "Discord Rich Presence e conexões de rede.", "Discord Rich Presence."},
        {"Shader compilation and runtime diagnostic history.", "Compilação de shaders e histórico do runtime.", "Компиляция шейдеров."},
    };
    return (i >= 0 && i < 11) ? s[i][(lang == 1) ? 1 : (lang == 2) ? 2 : 0] : "";
}

static void DrawTabIcon(ImDrawList* draw, int tab, ImVec2 p, float s, ImU32 col) {
    const float th = std::max(1.4f * s, 1.0f);
    const float x = p.x, y = p.y;
    switch (tab) {
    case 0: // Game (Gamepad)
        draw->AddRect(ImVec2(x + 1.0f * s, y + 3.0f * s), ImVec2(x + 15.0f * s, y + 13.0f * s), col, 4.0f * s, 0, th);
        draw->AddLine(ImVec2(x + 3.5f * s, y + 8.0f * s), ImVec2(x + 7.5f * s, y + 8.0f * s), col, th);
        draw->AddLine(ImVec2(x + 5.5f * s, y + 6.0f * s), ImVec2(x + 5.5f * s, y + 10.0f * s), col, th);
        draw->AddCircleFilled(ImVec2(x + 11.5f * s, y + 6.8f * s), 1.1f * s, col);
        draw->AddCircleFilled(ImVec2(x + 13.0f * s, y + 9.2f * s), 1.1f * s, col);
        break;
    case 1: // Display (Screen)
        draw->AddRect(ImVec2(x + 1.5f * s, y + 2.0f * s), ImVec2(x + 14.5f * s, y + 11.5f * s), col, 1.5f * s, 0, th);
        draw->AddLine(ImVec2(x + 8.0f * s, y + 11.5f * s), ImVec2(x + 8.0f * s, y + 14.5f * s), col, th);
        draw->AddLine(ImVec2(x + 5.0f * s, y + 14.5f * s), ImVec2(x + 11.0f * s, y + 14.5f * s), col, th);
        break;
    case 2: // Graphics (Palette)
        draw->AddCircle(ImVec2(x + 8.0f * s, y + 8.0f * s), 6.5f * s, col, 0, th);
        draw->AddCircleFilled(ImVec2(x + 6.0f * s, y + 5.5f * s), 1.2f * s, col);
        draw->AddCircleFilled(ImVec2(x + 9.5f * s, y + 5.5f * s), 1.2f * s, col);
        draw->AddCircleFilled(ImVec2(x + 11.5f * s, y + 8.5f * s), 1.2f * s, col);
        draw->AddCircleFilled(ImVec2(x + 7.0f * s, y + 11.0f * s), 1.3f * s, col);
        break;
    case 3: // Effects (Moon)
        draw->PathArcTo(ImVec2(x + 7.5f * s, y + 8.0f * s), 6.0f * s, -1.2f, 2.2f);
        draw->PathArcTo(ImVec2(x + 10.0f * s, y + 8.0f * s), 4.6f * s, 2.0f, -1.0f);
        draw->PathStroke(col, ImDrawFlags_Closed, th);
        break;
    case 4: // Performance (Gauge / Speedometer)
        draw->PathArcTo(ImVec2(x + 8.0f * s, y + 9.5f * s), 6.0f * s, 3.14159f * 0.85f, 3.14159f * 2.15f);
        draw->PathStroke(col, 0, th);
        draw->AddLine(ImVec2(x + 8.0f * s, y + 9.5f * s), ImVec2(x + 11.5f * s, y + 6.0f * s), col, th);
        draw->AddCircleFilled(ImVec2(x + 8.0f * s, y + 9.5f * s), 1.5f * s, col);
        break;
    case 5: // Controls (Keyboard)
        draw->AddRect(ImVec2(x + 1.5f * s, y + 3.0f * s), ImVec2(x + 14.5f * s, y + 13.0f * s), col, 2.0f * s, 0, th);
        draw->AddLine(ImVec2(x + 3.5f * s, y + 6.0f * s), ImVec2(x + 5.5f * s, y + 6.0f * s), col, th);
        draw->AddLine(ImVec2(x + 7.0f * s, y + 6.0f * s), ImVec2(x + 9.0f * s, y + 6.0f * s), col, th);
        draw->AddLine(ImVec2(x + 10.5f * s, y + 6.0f * s), ImVec2(x + 12.5f * s, y + 6.0f * s), col, th);
        draw->AddLine(ImVec2(x + 4.5f * s, y + 9.5f * s), ImVec2(x + 11.5f * s, y + 9.5f * s), col, th);
        break;
    case 6: // Mods (Folder)
        draw->AddLine(ImVec2(x + 2.0f * s, y + 4.0f * s), ImVec2(x + 6.0f * s, y + 4.0f * s), col, th);
        draw->AddLine(ImVec2(x + 6.0f * s, y + 4.0f * s), ImVec2(x + 8.0f * s, y + 6.0f * s), col, th);
        draw->AddRect(ImVec2(x + 2.0f * s, y + 6.0f * s), ImVec2(x + 14.5f * s, y + 13.5f * s), col, 1.5f * s, 0, th);
        break;
    case 7: // Developer (Terminal)
        draw->AddRect(ImVec2(x + 1.5f * s, y + 2.5f * s), ImVec2(x + 14.5f * s, y + 13.5f * s), col, 2.0f * s, 0, th);
        draw->AddLine(ImVec2(x + 4.0f * s, y + 5.5f * s), ImVec2(x + 6.5f * s, y + 8.0f * s), col, th);
        draw->AddLine(ImVec2(x + 6.5f * s, y + 8.0f * s), ImVec2(x + 4.0f * s, y + 10.5f * s), col, th);
        draw->AddLine(ImVec2(x + 8.5f * s, y + 10.5f * s), ImVec2(x + 11.5f * s, y + 10.5f * s), col, th);
        break;
    case 8: { // Trophies (Star)
        const ImVec2 center(x + 8.0f * s, y + 8.0f * s);
        ImVec2 pts[10];
        for (int k = 0; k < 10; ++k) {
            const float a = -1.570796f + k * (3.14159265f / 5.0f);
            const float r = (k % 2 == 0) ? (6.2f * s) : (2.8f * s);
            pts[k] = ImVec2(center.x + std::cos(a) * r, center.y + std::sin(a) * r);
        }
        draw->AddPolyline(pts, 10, col, ImDrawFlags_Closed, th);
        break;
    }
    case 9: // Online (Globe)
        draw->AddCircle(ImVec2(x + 8.0f * s, y + 8.0f * s), 6.5f * s, col, 0, th);
        draw->AddLine(ImVec2(x + 1.5f * s, y + 8.0f * s), ImVec2(x + 14.5f * s, y + 8.0f * s), col, th);
        draw->AddLine(ImVec2(x + 8.0f * s, y + 1.5f * s), ImVec2(x + 8.0f * s, y + 14.5f * s), col, th);
        draw->AddEllipse(ImVec2(x + 8.0f * s, y + 8.0f * s), ImVec2(3.2f * s, 6.5f * s), col, 0.0f, 0, th);
        break;
    case 10: // Log (Document)
        draw->AddLine(ImVec2(x + 3.0f * s, y + 2.0f * s), ImVec2(x + 10.0f * s, y + 2.0f * s), col, th);
        draw->AddLine(ImVec2(x + 10.0f * s, y + 2.0f * s), ImVec2(x + 13.0f * s, y + 5.0f * s), col, th);
        draw->AddLine(ImVec2(x + 13.0f * s, y + 5.0f * s), ImVec2(x + 13.0f * s, y + 14.0f * s), col, th);
        draw->AddLine(ImVec2(x + 13.0f * s, y + 14.0f * s), ImVec2(x + 3.0f * s, y + 14.0f * s), col, th);
        draw->AddLine(ImVec2(x + 3.0f * s, y + 14.0f * s), ImVec2(x + 3.0f * s, y + 2.0f * s), col, th);
        draw->AddLine(ImVec2(x + 5.5f * s, y + 7.0f * s), ImVec2(x + 10.5f * s, y + 7.0f * s), col, th);
        draw->AddLine(ImVec2(x + 5.5f * s, y + 9.5f * s), ImVec2(x + 10.5f * s, y + 9.5f * s), col, th);
        draw->AddLine(ImVec2(x + 5.5f * s, y + 12.0f * s), ImVec2(x + 8.5f * s, y + 12.0f * s), col, th);
        break;
    }
}

static void RenderTabContent(int tab, BbSettings::Values& s, int lang, float scale) {
    switch (tab) {
    case 0: { // Jogo
        static const char* languages[] = {"English", "Português (Brasil)", "Русский"};
        int cur_lang = s.menu_language.load();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(L(lang, "Language:", "Idioma:", "Язык:"));
        ImGui::SameLine(90.0f * scale);
        ImGui::SetNextItemWidth(240.0f * scale);
        if (ImGui::Combo("##menu_language", &cur_lang, languages, 3)) {
            s.menu_language = cur_lang;
            s.menu_language_chosen = true;
            BbSettings::Save();
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
        Checkbox(L(lang, "FPS counter in corner", "Contador de FPS no canto da tela", "Счётчик FPS в углу"), s.show_fps);

        ImGui::Dummy(ImVec2(0.0f, 16.0f * scale));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 10.0f * scale));

        ImGui::TextDisabled("bbport 0.7 - Bloodborne (CUSA03173)");
        ImGui::TextDisabled("%s", L(lang, "Settings are saved to bbport.ini", "As configurações são salvas em bbport.ini", "Настройки сохраняются в bbport.ini"));

        ImGui::Dummy(ImVec2(0.0f, 14.0f * scale));
        const float btn_w = 200.0f * scale, btn_h = 36.0f * scale;
        if (ImGui::Button(L(lang, "Restart runtime", "Reiniciar runtime", "Перезапуск рантайма"), ImVec2(btn_w, btn_h))) {
            runtime_restart();
        }
        ImGui::SameLine(0.0f, 14.0f * scale);
        static bool quit_confirm = false;
        const char* quit_txt = quit_confirm ?
            L(lang, "Confirm Quit", "Confirmar saída", "Подтвердить") :
            L(lang, "Quit Game", "Sair do jogo", "Выйти из игры");
        if (ImGui::Button(quit_txt, ImVec2(btn_w, btn_h))) {
            if (quit_confirm) {
                SDL_Event quit{};
                quit.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quit);
            }
            quit_confirm = !quit_confirm;
        }
        break;
    }
    case 1: // Tela
        OutputSection(s);
        break;
    case 2: { // Gráficos
        const bool taa = UpscalerSection(s);
        ReactiveMaskSection(s, taa);
        BbOverlaySections::RenderLightGridCheckbox(s, lang, dirty);
        break;
    }
    case 3: // Efeitos
        BbOverlaySections::RenderPhotoModeSection(s, lang, dirty);
        BbOverlaySections::RenderPostProcessingAndLightingSection(s, lang, dirty);
        GameEffectsSection(s);
        break;
    case 4: // Desempenho
        FrameRateLimitSection(s);
        BbOverlaySections::RenderFpsOverlaySection(s, lang, dirty);
        BbOverlaySections::RenderInfoHudSection(s, lang, dirty);
        break;
    case 5: { // Controles
        ImGui::SeparatorText(L(lang, "Mouse Look", "Câmera com o Mouse", "Камера мышью"));
        Checkbox(L(lang, "Mouse capture (F1 releases)", "Captura do mouse (F1 libera)", "Захват мыши (F1 отпускает)"), s.mouse_capture);
        Slider(L(lang, "Mouse sensitivity", "Sensibilidade do mouse", "Чувствительность мыши"), s.mouse_sensitivity, 0.10f, 10.0f);
        Checkbox(L(lang, "Invert mouse Y", "Inverter o eixo Y do mouse", "Инвертировать мышь по вертикали"), s.mouse_invert_y);
        Checkbox(L(lang, "Invert mouse X", "Inverter eixo X do mouse", "Инвертировать мышь по горизонтали"), s.mouse_invert_x);
        Checkbox(L(lang, "Raw 1:1 mouse camera", "Câmera raw 1:1 do mouse", "Прямой ввод мыши 1:1"), s.raw_mouse_camera);
        Checkbox(L(lang, "Slope auto-rotation", "Giro automático em declives", "Автоповорот на склонах"), s.mouse_hold_auto_rotate);

        ImGui::Spacing();
        ImGui::SeparatorText(L(lang, "Right Stick (Camera)", "Velocidade da Câmera (Analógico)", "Скорость камеры (стик)"));
        Slider(L(lang, "Horizontal speed", "Velocidade horizontal", "Скорость по горизонтали"), s.camera_speed[1], 0.25f, 2.0f);
        Slider(L(lang, "Vertical speed", "Velocidade vertical", "Скорость по вертикали"), s.camera_speed[0], 0.25f, 2.0f);

        ImGui::Spacing();
        BbOverlaySections::RenderPromptLayoutSection(s, lang, dirty);

        ImGui::Spacing();
        BbOverlaySections::RenderCameraSection(s, lang, dirty);
        break;
    }
    case 6: // Mods e patches
        ImGui::TextUnformatted(L(lang, "Mods and textures loaded from mods/ directory.",
                                      "Mods e pacotes de texturas carregados da pasta mods/.",
                                      "Моды и текстуры из папки mods/."));
        ImGui::Spacing();
        if (ImGui::Button(L(lang, "Open mods folder", "Abrir pasta de mods", "Открыть папку модов"), ImVec2(200.0f * scale, 34.0f * scale))) {
            (void)std::system("xdg-open \"mods\" &");
        }
        break;
    case 7: // Desenvolvedor
        BbOverlaySections::RenderMemoryToolsSection(scale);
        break;
    case 8: // Troféus
        ImGui::TextUnformatted(L(lang, "Bloodborne trophies are tracked and displayed via in-game toast notifications.",
                                      "Os troféus do Bloodborne são monitorados e exibidos através de notificações na tela.",
                                      "Трофеи отслеживаются и отображаются во всплывающих уведомлениях."));
        break;
    case 9: // Online
        BbOverlaySections::RenderDiscordRpcSection(s, lang, dirty);
        break;
    case 10: // Registro
        ShadersSection();
        break;
    }
}

enum MenuTabId {
    TabGame = 0,
    TabDisplay,
    TabGraphics,
    TabEffects,
    TabPerformance,
    TabControls,
    TabMods,
    TabDeveloper,
    TabTrophies,
    TabOnline,
    TabLog,
    TabCount
};

void Menu() {
    auto& s = BbSettings::Get();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float scale = base_scale;
    const int lang = s.menu_language.load();

    const float win_w = std::min(1000.0f * scale, vp->WorkSize.x - 40.0f * scale);
    const float win_h = std::min(680.0f * scale, vp->WorkSize.y - 40.0f * scale);
    const float header_h = 48.0f * scale;
    const float sidebar_w = 215.0f * scale;
    const float body_h = win_h - header_h;
    const float content_w = win_w - sidebar_w;

    static int active_tab = TabControls;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) active_tab = (active_tab + TabCount - 1) % TabCount;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) active_tab = (active_tab + 1) % TabCount;

    bool keep_open = true;
    {
        UI::Adw::ScopedTheme theme(scale);
        if (UI::Adw::ModalWindow::Begin("##bbport_overlay_main", &keep_open, win_w, win_h, scale)) {
            const UI::Adw::TopBar::Config top_cfg{
                .brand_prefix = "bbport 0.7",
                .brand_title = "Bloodborne",
                .active_tab_title = TabTitle(active_tab, lang),
                .sidebar_width = sidebar_w,
                .height = header_h,
                .scale = scale,
                .show_close_button = true
            };
            UI::Adw::TopBar::Render(top_cfg, &keep_open);

            if (UI::Adw::Sidebar::Begin(sidebar_w, header_h, body_h, scale)) {
                for (int i = 0; i < TabCount; ++i) {
                    if (UI::Adw::Sidebar::TabItem(i, TabTitle(i, lang), active_tab == i, sidebar_w, scale, DrawTabIcon)) {
                        active_tab = i;
                    }
                }
                UI::Adw::Sidebar::End();
            }

            if (UI::Adw::ContentArea::Begin(sidebar_w, header_h, content_w, body_h, scale)) {
                UI::Adw::ContentArea::Subtitle(TabSubtitle(active_tab, lang), scale);
                RenderTabContent(active_tab, s, lang, scale);
                UI::Adw::ContentArea::End();
            }

            UI::Adw::ModalWindow::End();
        }
    }

    if (!keep_open) {
        BbOverlay::SetOpen(false);
    }

    // Secondary tool windows
    UI::UiManager::Render();
    BbOverlaySections::RenderDiscordRpcWindow(s, s.menu_language, dirty);
    BbOverlaySections::RenderFpsOverlayWindow(s, s.menu_language, dirty);
}

} // namespace BbOverlay::detail
