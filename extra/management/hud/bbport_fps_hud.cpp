// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_fps_hud.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "bbport_settings.h"
#include "bbport_shader_tracker.h"
#include "bbport_strings.h"
#include "ui_strings.h"
#include <imgui.h>

namespace BbFpsHud {

void Render(const Stats& stats) {
    const auto& s = BbSettings::Get();
    const int lang = s.menu_language.load();
    const bool show_fps = s.show_fps;
    const bool is_compiling = BbShaderTracker::IsCompiling();

    if (!show_fps && !is_compiling) {
        return;
    }

    const float base_scale = stats.base_scale;
    const float orig_ms_avg = stats.orig_ms_avg;
    const float total_ms_avg = stats.total_ms_avg;
    const auto last_generated_time = stats.last_generated_time;

    // Alpha decay calculation for smooth fade out
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

    char text_buf[256] = {};
    if (show_fps) {
        if (fg_active) {
            if (!upscaler_label.empty()) {
                upscaler_label += " + FG";
            } else {
                upscaler_label = "FG";
            }
            std::snprintf(text_buf, sizeof(text_buf), "%.0f \\ %.0f FPS  %.1f %s  %s",
                           orig_fps, total_fps,
                           display_ms,
                           BbStrings::Get(BbStrings::StringId::FpsMs, lang),
                           upscaler_label.c_str());
        } else {
            std::snprintf(text_buf, sizeof(text_buf), "%.0f FPS  %.1f %s  %s",
                           total_fps > 0.0f ? total_fps : orig_fps,
                           display_ms,
                           BbStrings::Get(BbStrings::StringId::FpsMs, lang),
                           upscaler_label.c_str());
        }
    }

    const float spinner_radius = 5.0f * base_scale;
    const float spinner_spacing = 6.0f * base_scale;
    const char* const shader_tag = UI::L("Shaders", "Shaders", "Шейдеры");
    const char* const compiling_msg = UI::L("Compiling shaders...", "Compilando shaders...", "Компиляция шейдеров...");

    float total_content_w = 0.0f;
    float fps_text_w = 0.0f;
    if (show_fps) {
        fps_text_w = ImGui::CalcTextSize(text_buf).x;
        total_content_w = fps_text_w;
        if (is_compiling) {
            const float sep_w = ImGui::CalcTextSize(" | ").x;
            const float tag_w = ImGui::CalcTextSize(shader_tag).x;
            total_content_w += sep_w + (spinner_radius * 2.0f) + spinner_spacing + tag_w;
        }
    } else if (is_compiling) {
        const float msg_w = ImGui::CalcTextSize(compiling_msg).x;
        total_content_w = (spinner_radius * 2.0f) + spinner_spacing + msg_w;
    }

    const float h_padding = ImGui::GetStyle().WindowPadding.x * 2.0f + 20.0f * base_scale;
    const float needed_w = total_content_w + h_padding;

    static float max_counter_width = 0.0f;
    static int last_mode_key = -1;
    const int current_mode_key = (s.upscaler.load() & 0xFF) |
                                 (fg_active ? 0x100 : 0) |
                                 (is_compiling ? 0x200 : 0) |
                                 (show_fps ? 0x400 : 0) |
                                 (lang << 16) |
                                 (int(base_scale * 100.0f) << 24);
    if (current_mode_key != last_mode_key) {
        last_mode_key = current_mode_key;
        max_counter_width = 0.0f;
    }
    if (needed_w > max_counter_width) {
        max_counter_width = needed_w;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad,
                                   viewport->WorkPos.y + pad),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(max_counter_width, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);

    const float avail_w = ImGui::GetContentRegionAvail().x;
    if (avail_w > total_content_w) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - total_content_w) * 0.5f);
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    auto DrawSpinner = [&](ImVec2 pos, float alpha) {
        const float line_height = ImGui::GetTextLineHeight();
        const ImVec2 center = ImVec2(pos.x + spinner_radius, pos.y + line_height * 0.5f);
        // Track
        draw_list->AddCircle(center, spinner_radius, IM_COL32(255, 255, 255, static_cast<int>(40 * alpha)), 16, 1.5f * base_scale);
        // Animated arc
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

    if (show_fps) {
        ImGui::TextUnformatted(text_buf);
        if (is_compiling) {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(160, 160, 160, static_cast<int>(180 * compile_alpha)));
            ImGui::TextUnformatted(" | ");
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 2.0f * base_scale);

            const ImVec2 spinner_pos = ImGui::GetCursorScreenPos();
            DrawSpinner(spinner_pos, compile_alpha);
            ImGui::Dummy(ImVec2(spinner_radius * 2.0f, ImGui::GetTextLineHeight()));
            ImGui::SameLine(0.0f, spinner_spacing);

            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 235, 255, static_cast<int>(240 * compile_alpha)));
            ImGui::TextUnformatted(shader_tag);
            ImGui::PopStyleColor();
        }
    } else if (is_compiling) {
        const ImVec2 spinner_pos = ImGui::GetCursorScreenPos();
        DrawSpinner(spinner_pos, compile_alpha);
        ImGui::Dummy(ImVec2(spinner_radius * 2.0f, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0.0f, spinner_spacing);

        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 235, 255, static_cast<int>(240 * compile_alpha)));
        ImGui::TextUnformatted(compiling_msg);
        ImGui::PopStyleColor();
    }

    // Animated loading line along bottom border
    if (is_compiling && compile_alpha > 0.0f) {
        const ImVec2 win_pos = ImGui::GetWindowPos();
        const ImVec2 win_size = ImGui::GetWindowSize();
        const float line_y = win_pos.y + win_size.y - 2.5f * base_scale;
        const float x0 = win_pos.x + 8.0f * base_scale;
        const float x1 = win_pos.x + win_size.x - 8.0f * base_scale;
        const float span = x1 - x0;

        if (span > 10.0f) {
            // Subtle track
            draw_list->AddLine(ImVec2(x0, line_y), ImVec2(x1, line_y),
                               IM_COL32(255, 255, 255, static_cast<int>(25 * compile_alpha)),
                               2.0f * base_scale);

            // Scanning beam
            const float t = static_cast<float>(ImGui::GetTime());
            const float beam_len = span * 0.35f;
            const float travel = span + beam_len;
            const float cycle = std::fmod(t * 1.3f, 1.0f);
            const float beam_start = (x0 - beam_len) + travel * cycle;
            const float bx0 = std::max(x0, beam_start);
            const float bx1 = std::min(x1, beam_start + beam_len);

            if (bx1 > bx0) {
                // Glow
                draw_list->AddLine(ImVec2(bx0, line_y), ImVec2(bx1, line_y),
                                   IM_COL32(0, 180, 255, static_cast<int>(60 * compile_alpha)),
                                   4.5f * base_scale);
                // Cyan core
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
