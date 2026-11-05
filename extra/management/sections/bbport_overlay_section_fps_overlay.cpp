// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the FPS & performance overlay -- its row in the settings menu and the window where the
// metrics, the layout (row/column), the position and the graph are picked.
#include "bbport_overlay_sections.h"

#include "bbport_overlay_ui.h"

#include <imgui.h>

namespace BbOverlaySections {
namespace {

/// Screen positions of the overlay, indexed by fps_hud_position.
const char* const kPositionsEn[] = {"Top-Right (Default)", "Top-Left", "Bottom-Right", "Bottom-Left"};
const char* const kPositionsPt[] = {"Superior Direito (Padrão)", "Superior Esquerdo",
                                    "Inferior Direito", "Inferior Esquerdo"};
const char* const kPositionsRu[] = {"Сверху справа (По умолчанию)", "Сверху слева",
                                    "Снизу справа", "Снизу слева"};
constexpr int kPositionCount = int(std::size(kPositionsEn));

enum Layout : int { LayoutRow = 0, LayoutColumn = 1 };

enum Preset : int { PresetMinimal = 1, PresetRow = 2, PresetStandard = 3, PresetFull = 4 };

/// One of the four Steam-Deck-style presets: which metrics are on and how they are laid out.
struct PresetInfo {
    int id;
    const char* label_en;
    const char* label_pt;
    const char* label_ru;
    bool frametime;
    bool cpu;
    bool gpu;
    bool ram;
    bool vram;
    bool graph;
    int layout;
};

constexpr PresetInfo kPresets[] = {
    {PresetMinimal, "Level 1\n(Minimal)", "Nível 1\n(Mínimo)", "Уровень 1\n(Мин.)",
     false, false, false, false, false, false, LayoutRow},
    {PresetRow, "Level 2\n(Row)", "Nível 2\n(Linha)", "Уровень 2\n(Строка)",
     true, false, false, false, false, false, LayoutRow},
    {PresetStandard, "Level 3\n(Standard)", "Nível 3\n(Padrão)", "Уровень 3\n(Базовый)",
     true, true, true, false, false, false, LayoutRow},
    {PresetFull, "Level 4\n(Full)", "Nível 4\n(Completo)", "Уровень 4\n(Полный)",
     true, true, true, true, true, true, LayoutColumn},
};
constexpr int kPresetCount = int(std::size(kPresets));

/// Applies a preset: FPS is always shown, the rest follows the table above.
void ApplyPreset(BbSettings::Values& s, const PresetInfo& preset, bool& dirty) {
    s.fps_hud_show_fps = true;
    s.fps_hud_show_frametime = preset.frametime;
    s.fps_hud_show_cpu = preset.cpu;
    s.fps_hud_show_gpu = preset.gpu;
    s.fps_hud_show_ram = preset.ram;
    s.fps_hud_show_vram = preset.vram;
    s.fps_hud_show_graph = preset.graph;
    s.fps_hud_layout = preset.layout;
    s.fps_hud_preset = preset.id;
    dirty = true;
}

/// The four preset buttons, one row.
void PresetButtons(BbSettings::Values& s, int lang, bool& dirty) {
    const float content_width = ImGui::GetContentRegionAvail().x;
    const float button_width =
        (content_width - Size::kPresetGap) / Size::kPresetCount;
    for (int i = 0; i < kPresetCount; ++i) {
        const PresetInfo& preset = kPresets[i];
        if (i > 0) {
            ImGui::SameLine();
        }
        if (ImGui::Button(Tr(lang, preset.label_en, preset.label_pt, preset.label_ru),
                          ImVec2(button_width, Size::kPresetButtonHeight))) {
            ApplyPreset(s, preset, dirty);
        }
    }
}

/// The graph-only sliders, hidden while the graph itself is off.
void GraphControls(BbSettings::Values& s, int lang, bool& dirty) {
    if (!s.fps_hud_show_graph.load(std::memory_order_relaxed)) {
        return;
    }
    ImGui::Spacing();
    ImGui::SeparatorText(Tr(lang, "Frametime Graph Customization", "Customização do Gráfico de Frametime",
                            "Настройка графика времени кадра"));
    Slider(Tr(lang, "Graph Width", "Largura do Gráfico", "Ширина графика"),
           s.fps_hud_graph_width, 100.0f, 500.0f, dirty);
    Slider(Tr(lang, "Graph Height", "Altura do Gráfico", "Высота графика"),
           s.fps_hud_graph_height, 20.0f, 100.0f, dirty);
    Slider(Tr(lang, "Line Thickness", "Espessura da Linha", "Толщина линии"),
           s.fps_hud_graph_thickness, 1.0f, 5.0f, dirty);
}

/// Closes the window when its title bar X was clicked, keeping the setting in sync.
void CloseWindow(BbSettings::Values& s, bool& dirty) {
    s.fps_hud_window_open.store(false, std::memory_order_relaxed);
    dirty = true;
}

} // namespace

void RenderFpsOverlaySection(BbSettings::Values& s, int lang, bool& dirty) {
    ImGui::SeparatorText(Tr(lang, "FPS & Performance Overlay", "Overlay de FPS e Desempenho",
                            "Оверлей FPS и производительности"));

    Checkbox(Tr(lang, "Show FPS & Metrics Overlay", "Exibir Overlay de FPS / Métricas",
                 "Показывать оверлей FPS и метрик"),
             s.show_fps, dirty);
    Hint(Tr(lang, "Displays real-time FPS, frame time, shader compilation, and optional process metrics.",
             "Exibe taxa de quadros, tempo de quadro, compilação de shaders e métricas de processo opcionais.",
             "Показывает FPS, время кадра, компиляцию шейдеров и дополнительные метрики."));

    const bool window_open = s.fps_hud_window_open.load(std::memory_order_relaxed);
    const char* button_text =
        window_open ? Tr(lang, "Close Overlay Configuration Window###btn_fps_win",
                         "Fechar Janela de Configuração do Overlay###btn_fps_win",
                         "Закрыть настройки оверлея###btn_fps_win")
                    : Tr(lang, "Overlay Configuration Window###btn_fps_win",
                         "Janela de Configuração do Overlay###btn_fps_win",
                         "Открыть настройки оверлея###btn_fps_win");

    ImGui::PushStyleColor(ImGuiCol_Button,
                          window_open ? Theme::kButtonOn : Theme::kButtonAction);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          window_open ? Theme::kButtonOnHovered : Theme::kButtonActionHovered);
    if (ImGui::Button(button_text, ImVec2(-Size::kFullRowRightPad, Size::kActionButtonHeight))) {
        s.fps_hud_window_open.store(!window_open, std::memory_order_relaxed);
        dirty = true;
    }
    ImGui::PopStyleColor(2);
    Hint(Tr(lang, "Opens dedicated window, Row/Column modes, HUD scale and live preview.",
             "Abre a janela exclusiva com presets, modos Linha/Coluna, escala do HUD e prévia ao vivo.",
             "Открывает окно, содержащее пресеты, режимы «строки/столбцы», HUD и предварительный просмотр в реальном времени."));
}

void RenderFpsOverlayWindow(BbSettings::Values& s, int lang, bool& dirty) {
    if (!s.fps_hud_window_open.load(std::memory_order_relaxed)) {
        return;
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(Size::kFpsWindowWidth, Size::kFpsWindowHeight),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Tr(lang, "Performance Overlay Settings (FPS & Metrics)###fps_overlay_window",
                         "Configuração do Overlay de Desempenho (FPS & Métricas)###fps_overlay_window",
                         "Настройки оверлея производительности (FPS и метрики)###fps_overlay_window"),
                      &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open) {
            CloseWindow(s, dirty);
        }
        return;
    }
    if (!open) {
        CloseWindow(s, dirty);
        ImGui::End();
        return;
    }

    Checkbox(Tr(lang, "Enable Performance Overlay", "Ativar Overlay de Desempenho",
                 "Включить оверлей производительности"),
             s.show_fps, dirty);

    const bool active = s.show_fps.load(std::memory_order_relaxed);
    ImGui::BeginDisabled(!active);

    ImGui::Spacing();
    ImGui::SeparatorText(Tr(lang, "Performance Level (Steam Presets)", "Nível de Detalhe (Presets Estilo Steam)",
                            "Уровень детализации (Пресеты Steam)"));
    PresetButtons(s, lang, dirty);

    ImGui::Spacing();
    ImGui::SeparatorText(Tr(lang, "Format & Orientation", "Formato e Orientação", "Формат и ориентация"));

    const int layout = s.fps_hud_layout.load(std::memory_order_relaxed);
    if (ImGui::RadioButton(Tr(lang, "Row Mode (Horizontal Compact)", "Modo Linha (Horizontal Compacto)",
                              "Режим Строка (Горизонтальный)"),
                           layout == LayoutRow)) {
        s.fps_hud_layout.store(LayoutRow, std::memory_order_relaxed);
        dirty = true;
    }
    ImGui::SameLine(0.0f, Size::kSameLineGap);
    if (ImGui::RadioButton(Tr(lang, "Column Mode (Vertical Detailed)", "Modo Coluna (Vertical Detalhado)",
                              "Режим Колонка (Вертикальный)"),
                           layout == LayoutColumn)) {
        s.fps_hud_layout.store(LayoutColumn, std::memory_order_relaxed);
        dirty = true;
    }

    ComboOptions(Tr(lang, "Screen Position", "Posição na Tela", "Позиция на экране"),
                 TrList(lang, kPositionsEn, kPositionsPt, kPositionsRu), kPositionCount,
                 s.fps_hud_position, dirty, Size::kComboWidth);

    ImGui::Spacing();
    ImGui::SeparatorText(Tr(lang, "Active Metrics", "Métricas Ativas", "Отображаемые показатели"));

    ImGui::Columns(2, "##fps_metrics_grid", false);
    Checkbox(Tr(lang, "Framerate (FPS)", "Taxa de Quadros (FPS)", "Частота кадров (FPS)"),
             s.fps_hud_show_fps, dirty);
    Checkbox(Tr(lang, "Frametime (ms)", "Tempo de Quadro (ms)", "Время кадра (мс)"),
             s.fps_hud_show_frametime, dirty);
    Checkbox(Tr(lang, "Process CPU (%)", "Uso de CPU do Processo (%)", "Нагрузка CPU процесса (%)"),
             s.fps_hud_show_cpu, dirty);
    Checkbox(Tr(lang, "Frametime Graph", "Gráfico de Frametime", "График времени кадра"),
             s.fps_hud_show_graph, dirty);

    ImGui::NextColumn();
    Checkbox(Tr(lang, "GPU Load (%) [Dedicated]", "Uso de GPU (%) [Dedicada / Ativa]",
                 "Нагрузка GPU (%) [Дискретная]"),
             s.fps_hud_show_gpu, dirty);
    Checkbox(Tr(lang, "Process RAM (RSS)", "Memória RAM do Jogo (RSS)", "Оперативная память (RAM)"),
             s.fps_hud_show_ram, dirty);
    Checkbox(Tr(lang, "Video Memory (VRAM)", "Memória de Vídeo (VRAM)", "Видеопамять (VRAM)"),
             s.fps_hud_show_vram, dirty);
    Checkbox(Tr(lang, "Shader Compilation", "Notificação de Shaders", "Компиляция шейдеров"),
             s.fps_hud_show_shaders, dirty);
    ImGui::Columns(1);

    ImGui::Spacing();
    ImGui::SeparatorText(Tr(lang, "Visual Appearance", "Aparência Visual", "Внешний вид"));
    Slider(Tr(lang, "HUD Scale", "Escala do HUD", "Масштаб HUD"),
           s.fps_hud_scale, 0.75f, 2.0f, dirty);
    Slider(Tr(lang, "Background Opacity", "Opacidade do Fundo", "Прозрачность фона"),
           s.fps_hud_bg_opacity, 0.0f, 1.0f, dirty);
    GraphControls(s, lang, dirty);

    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button(Tr(lang, "Close Window", "Fechar Janela", "Закрыть окно"),
                      ImVec2(Size::kCloseButtonWidth, 0))) {
        CloseWindow(s, dirty);
    }
    ImGui::End();
}

} // namespace BbOverlaySections
