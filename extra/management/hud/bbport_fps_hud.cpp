// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_fps_hud.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "bbport_perf_metrics.h"
#include "bbport_settings.h"
#include "bbport_shader_tracker.h"
#include "bbport_strings.h"
#include "ui_strings.h"
#include <imgui.h>

namespace BbFpsHud {

struct MetricEntry {
    const char* tag = "";
    ImU32 tag_col = IM_COL32_WHITE;
    std::string value;
    bool is_standalone = false;
};

void Render(const Stats& stats) {
    const auto& s = BbSettings::Get();
    if (s.hide_hud) {
        return;
    }

    const int lang = s.menu_language.load();
    const bool show_fps = s.show_fps;
    const bool is_compiling = BbShaderTracker::IsCompiling();
    const bool show_shaders = s.fps_hud_show_shaders.load(std::memory_order_relaxed);

    if (!show_fps && (!is_compiling || !show_shaders)) {
        return;
    }

    const float user_scale = std::clamp(s.fps_hud_scale.load(std::memory_order_relaxed), 0.5f, 3.0f);
    const float base_scale = stats.base_scale * user_scale;
    const float orig_ms_avg = stats.orig_ms_avg;
    const float total_ms_avg = stats.total_ms_avg;
    const auto last_generated_time = stats.last_generated_time;

    // Alpha decay calculation for smooth shader compilation fade out
    float compile_alpha = 1.0f;
    if (is_compiling) {
        const uint32_t decay_ms = BbShaderTracker::GetDecayElapsedMs();
        if (decay_ms > 400 && decay_ms < 800) {
            compile_alpha = 1.0f - (float(decay_ms - 400) / 400.0f);
        } else if (decay_ms >= 800) {
            compile_alpha = 0.0f;
        }
    }

    const float orig_fps = orig_ms_avg > 0.0f ? 1000.0f / orig_ms_avg : 0.0f;
    const float total_fps = total_ms_avg > 0.0f ? 1000.0f / total_ms_avg : orig_fps;
    const float display_ms = total_ms_avg > 0.0f ? total_ms_avg : orig_ms_avg;

    // Sample/update background metrics (throttled to 500ms) and ring buffer
    BbPerfMetrics::Update(display_ms);
    const auto metrics = BbPerfMetrics::GetSnapshot();

    std::string upscaler_label;
    if (s.upscaler == BbSettings::UpscalerFsr3) {
        upscaler_label = "FSR 3.1";
    } else if (s.upscaler == BbSettings::UpscalerFsr4) {
        upscaler_label = "FSR 4";
    } else if (s.upscaler == BbSettings::UpscalerFsr411) {
        upscaler_label = s.fsr411_fp8.load() ? "FSR 4.1.1 (FP8)" : "FSR 4.1.1";
    } else if (s.upscaler == BbSettings::UpscalerTaa) {
        upscaler_label = "TAA";
    } else if (s.upscaler == BbSettings::UpscalerDlss) {
        upscaler_label = "DLSS";
    }

    const bool fg_setting_on = s.frame_generation.load();
    const auto now = std::chrono::steady_clock::now();
    const bool fg_active = fg_setting_on ||
        (last_generated_time.time_since_epoch().count() != 0 &&
         (now - last_generated_time) < std::chrono::milliseconds(1500));

    if (fg_active) {
        if (!upscaler_label.empty()) {
            upscaler_label += " + FG";
        } else {
            upscaler_label = "FG";
        }
    }

    // Build metric entries
    std::vector<MetricEntry> entries;
    if (show_fps) {
        const bool show_fps_val = s.fps_hud_show_fps.load();
        const bool show_ft_val = s.fps_hud_show_frametime.load();
        const char* const ms_unit = BbStrings::Get(BbStrings::StringId::FpsMs, lang);

        if (show_fps_val || show_ft_val) {
            char fps_buf[128] = {};
            if (fg_active) {
                if (show_fps_val && show_ft_val) {
                    std::snprintf(fps_buf, sizeof(fps_buf), "%.0f \\ %.0f FPS (%.1f %s)",
                                  orig_fps, total_fps, display_ms, ms_unit);
                } else if (show_fps_val) {
                    std::snprintf(fps_buf, sizeof(fps_buf), "%.0f \\ %.0f FPS", orig_fps, total_fps);
                } else {
                    std::snprintf(fps_buf, sizeof(fps_buf), "%.1f %s", display_ms, ms_unit);
                }
            } else {
                const float fps = total_fps > 0.0f ? total_fps : orig_fps;
                if (show_fps_val && show_ft_val) {
                    std::snprintf(fps_buf, sizeof(fps_buf), "%.0f FPS (%.1f %s)", fps, display_ms, ms_unit);
                } else if (show_fps_val) {
                    std::snprintf(fps_buf, sizeof(fps_buf), "%.0f FPS", fps);
                } else {
                    std::snprintf(fps_buf, sizeof(fps_buf), "%.1f %s", display_ms, ms_unit);
                }
            }
            entries.push_back(MetricEntry{"FPS", IM_COL32(250, 250, 250, 255), fps_buf, true});
        }

        if (s.fps_hud_show_gpu.load()) {
            char gpu_buf[64] = {};
            std::snprintf(gpu_buf, sizeof(gpu_buf), "%.0f%%", metrics.gpu_percent);
            entries.push_back(MetricEntry{"GPU", IM_COL32(80, 225, 120, 255), gpu_buf, false});
        }

        if (s.fps_hud_show_cpu.load()) {
            char cpu_buf[64] = {};
            std::snprintf(cpu_buf, sizeof(cpu_buf), "%.0f%%", metrics.cpu_percent);
            entries.push_back(MetricEntry{"CPU", IM_COL32(80, 190, 255, 255), cpu_buf, false});
        }

        if (s.fps_hud_show_vram.load()) {
            char vram_buf[64] = {};
            if (metrics.vram_bytes >= (1024ULL * 1024 * 1024)) {
                std::snprintf(vram_buf, sizeof(vram_buf), "%.2f GB",
                              double(metrics.vram_bytes) / (1024.0 * 1024.0 * 1024.0));
            } else {
                std::snprintf(vram_buf, sizeof(vram_buf), "%llu MB",
                              static_cast<unsigned long long>(metrics.vram_bytes >> 20));
            }
            entries.push_back(MetricEntry{"VRAM", IM_COL32(230, 130, 255, 255), vram_buf, false});
        }

        if (s.fps_hud_show_ram.load()) {
            char ram_buf[64] = {};
            if (metrics.ram_rss_bytes >= (1024ULL * 1024 * 1024)) {
                std::snprintf(ram_buf, sizeof(ram_buf), "%.2f GB",
                              double(metrics.ram_rss_bytes) / (1024.0 * 1024.0 * 1024.0));
            } else {
                std::snprintf(ram_buf, sizeof(ram_buf), "%llu MB",
                              static_cast<unsigned long long>(metrics.ram_rss_bytes >> 20));
            }
            entries.push_back(MetricEntry{"RAM", IM_COL32(255, 185, 75, 255), ram_buf, false});
        }

        if (!upscaler_label.empty()) {
            entries.push_back(MetricEntry{"UP", IM_COL32(180, 235, 255, 255), upscaler_label, true});
        }
    }

    const float spinner_radius = 5.0f * base_scale;
    const float spinner_spacing = 6.0f * base_scale;
    const uint32_t active_compiles = BbShaderTracker::GetActiveCount();
    const char* const shader_tag = UI::L("Shaders", "Shaders", "Шейдеры");

    // Determine window placement
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * stats.base_scale;
    const int pos_mode = std::clamp(s.fps_hud_position.load(), 0, 3);
    const int layout_mode = std::clamp(s.fps_hud_layout.load(), 0, 1); // 0=Horizontal (Linha), 1=Vertical (Coluna)
    const bool show_graph = show_fps && s.fps_hud_show_graph.load();

    ImVec2 win_pos, win_pivot;
    switch (pos_mode) {
    case 1: // Top-Left
        win_pos = ImVec2(viewport->WorkPos.x + pad, viewport->WorkPos.y + pad);
        win_pivot = ImVec2(0.0f, 0.0f);
        break;
    case 2: // Bottom-Right
        win_pos = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad,
                         viewport->WorkPos.y + viewport->WorkSize.y - pad);
        win_pivot = ImVec2(1.0f, 1.0f);
        break;
    case 3: // Bottom-Left
        win_pos = ImVec2(viewport->WorkPos.x + pad, viewport->WorkPos.y + viewport->WorkSize.y - pad);
        win_pivot = ImVec2(0.0f, 1.0f);
        break;
    case 0: // Top-Right (Default)
    default:
        win_pos = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad, viewport->WorkPos.y + pad);
        win_pivot = ImVec2(1.0f, 0.0f);
        break;
    }

    ImGui::SetNextWindowPos(win_pos, ImGuiCond_Always, win_pivot);
    const float bg_alpha = std::clamp(s.fps_hud_bg_opacity.load(), 0.0f, 1.0f);
    ImGui::SetNextWindowBgAlpha(bg_alpha);

    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    auto DrawSpinner = [&](ImVec2 pos, float alpha) {
        const float line_height = ImGui::GetTextLineHeight();
        const ImVec2 center = ImVec2(pos.x + spinner_radius, pos.y + line_height * 0.5f);
        draw_list->AddCircle(center, spinner_radius, IM_COL32(255, 255, 255, static_cast<int>(40 * alpha)), 16, 1.5f * base_scale);
        const float t = static_cast<float>(ImGui::GetTime());
        const float spin_angle = t * 7.5f;
        constexpr int kSegments = 16;
        constexpr float kArcLen = 3.14159265f * 1.35f;
        draw_list->PathClear();
        for (int i = 0; i <= kSegments; ++i) {
            const float a = spin_angle + (float(i) / float(kSegments)) * kArcLen;
            draw_list->PathLineTo(ImVec2(center.x + std::cos(a) * spinner_radius, center.y + std::sin(a) * spinner_radius));
        }
        draw_list->PathStroke(IM_COL32(0, 220, 255, static_cast<int>(240 * alpha)), 0, 2.0f * base_scale);
    };

    if (show_fps && !entries.empty()) {
        if (layout_mode == 0) {
            // Modo Linha (Horizontal) - no overlap guaranteed
            for (size_t i = 0; i < entries.size(); ++i) {
                if (i > 0) {
                    ImGui::SameLine(0.0f, 6.0f * user_scale);
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 160, 160, 140));
                    ImGui::TextUnformatted("|");
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0.0f, 6.0f * user_scale);
                }

                if (entries[i].is_standalone) {
                    if (std::string(entries[i].tag) == "UP") {
                        ImGui::PushStyleColor(ImGuiCol_Text, entries[i].tag_col);
                        ImGui::TextUnformatted(entries[i].value.c_str());
                        ImGui::PopStyleColor();
                    } else {
                        ImGui::TextUnformatted(entries[i].value.c_str());
                    }
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, entries[i].tag_col);
                    ImGui::TextUnformatted(entries[i].tag);
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0.0f, 4.0f * user_scale);
                    ImGui::TextUnformatted(entries[i].value.c_str());
                }
            }

            // Shader Compilation Indicator in Horizontal Mode
            if (is_compiling && show_shaders && compile_alpha > 0.0f) {
                ImGui::SameLine(0.0f, 6.0f * user_scale);
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 160, 160, static_cast<int>(140 * compile_alpha)));
                ImGui::TextUnformatted("|");
                ImGui::PopStyleColor();
                ImGui::SameLine(0.0f, 6.0f * user_scale);

                const ImVec2 spinner_pos = ImGui::GetCursorScreenPos();
                DrawSpinner(spinner_pos, compile_alpha);
                ImGui::Dummy(ImVec2(spinner_radius * 2.0f, ImGui::GetTextLineHeight()));
                ImGui::SameLine(0.0f, spinner_spacing);

                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 235, 255, static_cast<int>(240 * compile_alpha)));
                if (active_compiles > 0) {
                    ImGui::Text("%s (%u)", shader_tag, active_compiles);
                } else {
                    ImGui::TextUnformatted(shader_tag);
                }
                ImGui::PopStyleColor();
            }
        } else {
            // Modo Coluna (Vertical) - clean 2-column table, zero overlap
            if (ImGui::BeginTable("##hud_metrics_tbl", 2, ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, 48.0f * user_scale);
                ImGui::TableSetupColumn("Val", ImGuiTableColumnFlags_WidthStretch);

                for (const auto& entry : entries) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushStyleColor(ImGuiCol_Text, entry.tag_col);
                    ImGui::TextUnformatted(entry.tag);
                    ImGui::PopStyleColor();

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(entry.value.c_str());
                }

                // Shader Compilation row in Column Mode
                if (is_compiling && show_shaders && compile_alpha > 0.0f) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    const ImVec2 spinner_pos = ImGui::GetCursorScreenPos();
                    DrawSpinner(spinner_pos, compile_alpha);
                    ImGui::Dummy(ImVec2(spinner_radius * 2.0f, ImGui::GetTextLineHeight()));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 235, 255, static_cast<int>(240 * compile_alpha)));
                    if (active_compiles > 0) {
                        ImGui::Text("%s (%u)", shader_tag, active_compiles);
                    } else {
                        ImGui::TextUnformatted(shader_tag);
                    }
                    ImGui::PopStyleColor();
                }

                ImGui::EndTable();
            }
        }
    } else if (is_compiling && show_shaders && compile_alpha > 0.0f) {
        // Only compiling shaders is active
        const ImVec2 spinner_pos = ImGui::GetCursorScreenPos();
        DrawSpinner(spinner_pos, compile_alpha);
        ImGui::Dummy(ImVec2(spinner_radius * 2.0f, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0.0f, spinner_spacing);

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 235, 255, static_cast<int>(240 * compile_alpha)));
        if (active_compiles > 0) {
            ImGui::Text("%s (%u)...", UI::L("Compiling shaders", "Compilando shaders", "Компиляция шейдеров"), active_compiles);
        } else {
            ImGui::TextUnformatted(UI::L("Compiling shaders...", "Compilando shaders...", "Компиляция шейдеров..."));
        }
        ImGui::PopStyleColor();
    }

    // Customizable Frametime Graph (holds 240 samples = 1 to 4 full seconds)
    if (show_graph) {
        const float* samples = nullptr;
        size_t count = 0;
        size_t offset = 0;
        BbPerfMetrics::GetFrameHistory(samples, count, offset);

        if (count > 1) {
            const float user_gw = std::clamp(s.fps_hud_graph_width.load(std::memory_order_relaxed), 100.0f, 600.0f);
            const float user_gh = std::clamp(s.fps_hud_graph_height.load(std::memory_order_relaxed), 16.0f, 120.0f);
            const float user_th = std::clamp(s.fps_hud_graph_thickness.load(std::memory_order_relaxed), 0.5f, 5.0f);

            const float graph_w = std::max(user_gw * user_scale, ImGui::GetContentRegionAvail().x);
            const float graph_h = user_gh * user_scale;
            const float line_thick = user_th * user_scale;

            // Explicit separation to prevent any overlap with text
            ImGui::Dummy(ImVec2(0.0f, 4.0f * user_scale));

            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const ImVec2 p1 = ImVec2(p0.x + graph_w, p0.y + graph_h);

            // Subtle dark background & thin border
            draw_list->AddRectFilled(p0, p1, IM_COL32(16, 18, 22, 210), 3.0f);
            draw_list->AddRect(p0, p1, IM_COL32(255, 255, 255, 25), 3.0f);

            // Target reference lines: 16.6ms (60 FPS) and 33.3ms (30 FPS)
            constexpr float kMaxMs = 50.0f;
            auto MsToY = [&](float ms) -> float {
                const float clamped = std::clamp(ms, 0.0f, kMaxMs);
                return p1.y - 2.0f - (clamped / kMaxMs) * (graph_h - 4.0f);
            };

            const float y60 = MsToY(16.67f);
            draw_list->AddLine(ImVec2(p0.x, y60), ImVec2(p1.x, y60), IM_COL32(0, 220, 120, 60), 1.0f);

            const float y30 = MsToY(33.33f);
            draw_list->AddLine(ImVec2(p0.x, y30), ImVec2(p1.x, y30), IM_COL32(240, 190, 40, 60), 1.0f);

            // Polyline connecting the chronological frametimes (240 samples)
            const float step_x = graph_w / float(BbPerfMetrics::kFrameHistoryCapacity - 1);
            for (size_t i = 1; i < count; ++i) {
                const size_t idx_prev = (count < BbPerfMetrics::kFrameHistoryCapacity) ?
                    (i - 1) : ((offset + i - 1) % BbPerfMetrics::kFrameHistoryCapacity);
                const size_t idx_curr = (count < BbPerfMetrics::kFrameHistoryCapacity) ?
                    i : ((offset + i) % BbPerfMetrics::kFrameHistoryCapacity);

                const float v_prev = samples[idx_prev];
                const float v_curr = samples[idx_curr];

                const ImVec2 pt0(p0.x + float(i - 1) * step_x, MsToY(v_prev));
                const ImVec2 pt1(p0.x + float(i) * step_x, MsToY(v_curr));

                ImU32 col = IM_COL32(0, 225, 255, 240);
                if (v_curr > 33.4f) {
                    col = IM_COL32(255, 75, 75, 240); // Warning Red
                } else if (v_curr > 16.8f) {
                    col = IM_COL32(240, 200, 50, 230); // Yellow
                }

                draw_list->AddLine(pt0, pt1, col, line_thick);
            }

            ImGui::Dummy(ImVec2(graph_w, graph_h));
        }
    }

    // Animated loading line along bottom border when compiling shaders
    if (is_compiling && show_shaders && compile_alpha > 0.0f) {
        const ImVec2 win_pos_curr = ImGui::GetWindowPos();
        const ImVec2 win_size = ImGui::GetWindowSize();
        const float line_y = win_pos_curr.y + win_size.y - 2.5f * base_scale;
        const float x0 = win_pos_curr.x + 8.0f * base_scale;
        const float x1 = win_pos_curr.x + win_size.x - 8.0f * base_scale;
        const float span = x1 - x0;

        if (span > 10.0f) {
            draw_list->AddLine(ImVec2(x0, line_y), ImVec2(x1, line_y),
                               IM_COL32(255, 255, 255, static_cast<int>(25 * compile_alpha)),
                               2.0f * base_scale);

            const float t = static_cast<float>(ImGui::GetTime());
            const float beam_len = span * 0.35f;
            const float travel = span + beam_len;
            const float cycle = std::fmod(t * 1.3f, 1.0f);
            const float beam_start = (x0 - beam_len) + travel * cycle;
            const float bx0 = std::max(x0, beam_start);
            const float bx1 = std::min(x1, beam_start + beam_len);

            if (bx1 > bx0) {
                draw_list->AddLine(ImVec2(bx0, line_y), ImVec2(bx1, line_y),
                                   IM_COL32(0, 180, 255, static_cast<int>(60 * compile_alpha)),
                                   4.5f * base_scale);
                draw_list->AddLine(ImVec2(bx0, line_y), ImVec2(bx1, line_y),
                                   IM_COL32(0, 225, 255, static_cast<int>(240 * compile_alpha)),
                                   2.0f * base_scale);
            }
        }
    }

    if (fg_active && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s: %.0f FPS | %s: %.0f FPS",
                          UI::L("Original Frames", "Quadros Originais", "Исходные кадры"),
                          orig_fps,
                          UI::L("Total Frames", "Quadros Totais", "Всего кадров"),
                          total_fps);
    }
    ImGui::End();
}

} // namespace BbFpsHud
