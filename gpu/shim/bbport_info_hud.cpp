// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_info_hud.h"

#include <chrono>
#include <cstdio>
#include <string>

#include "bbport_settings.h"
#include "imgui.h"
#include "rpc/bbport_game_data.h"

namespace BbInfoHud {

void Init() {
    BbGameData::Init();
}

void Render(float base_scale) {
    const auto& s = BbSettings::Get();
    if (!s.show_info_hud || s.hide_hud) {
        return;
    }

    const int lang = s.menu_language.load();

    // Prepare text content
    const bool show_name = s.info_hud_show_name.load();
    const bool show_map = s.info_hud_show_map.load();
    const bool show_time = s.info_hud_show_playtime.load();
    const bool show_coop = s.info_hud_show_coop.load();

    if (!show_name && !show_map && !show_time && !show_coop) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    // 250ms cached updates to avoid querying pointers & sigsetjmp every single frame (e.g. 60-120fps)
    static auto last_hud_query = std::chrono::steady_clock::time_point{};
    static BbGameData::PresenceSnapshot cached_snap;
    static int cached_lang = -1;

    if (last_hud_query.time_since_epoch().count() == 0 ||
        (now - last_hud_query) >= std::chrono::milliseconds(250) ||
        cached_lang != lang) {
        last_hud_query = now;
        cached_lang = lang;
        cached_snap = BbGameData::GetSnapshot();
    }

    const uint32_t session_sec = (cached_snap.session_start_unix_s > 0) ?
        static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count() - cached_snap.session_start_unix_s) : 0;
    const uint32_t session_h = session_sec / 3600;
    const uint32_t session_m = (session_sec % 3600) / 60;
    const uint32_t session_s = session_sec % 60;

    const uint32_t total_sec = cached_snap.in_game_seconds;
    const uint32_t total_h = total_sec / 3600;
    const uint32_t total_m = (total_sec % 3600) / 60;
    const uint32_t total_s = total_sec % 60;

    const std::string& map_name = (lang == 1) ? cached_snap.map_name_pt :
                                  (lang == 2) ? cached_snap.map_name_ru :
                                                cached_snap.map_name_en;

    std::string coop_status;
    if (cached_snap.coop_role == 1) {
        int coop = cached_snap.hunters_count - 1;
        if (coop > 0) {
            char buf[64];
            if (lang == 1) std::snprintf(buf, sizeof(buf), "Anfitrião (+%d Caçador%s)", coop, coop > 1 ? "es" : "");
            else if (lang == 2) std::snprintf(buf, sizeof(buf), "Хост (+%d)", coop);
            else std::snprintf(buf, sizeof(buf), "Host (+%d Hunter%s)", coop, coop > 1 ? "s" : "");
            coop_status = buf;
        } else {
            coop_status = (lang == 1) ? "Anfitrião (Host)" : (lang == 2) ? "Хост" : "Host";
        }
    } else if (cached_snap.coop_role == 2) {
        coop_status = (lang == 1) ? "Convidado (Co-op)" : (lang == 2) ? "Гость (Кооп)" : "Guest (Co-op)";
    } else {
        coop_status = (lang == 1) ? "Solo" : (lang == 2) ? "Одиночная" : "Solo";
    }

    // Setup Window Position & Flags
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    const int pos_mode = s.info_hud_pos.load();

    ImVec2 win_pos, win_pivot;
    switch (pos_mode) {
    case 1: // Top-Right (below FPS)
        win_pos = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad,
                         viewport->WorkPos.y + pad + (s.show_fps ? 38.0f * base_scale : 0.0f));
        win_pivot = ImVec2(1.0f, 0.0f);
        break;
    case 2: // Bottom-Left
        win_pos = ImVec2(viewport->WorkPos.x + pad, viewport->WorkPos.y + viewport->WorkSize.y - pad);
        win_pivot = ImVec2(0.0f, 1.0f);
        break;
    case 3: // Bottom-Right
        win_pos = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad,
                         viewport->WorkPos.y + viewport->WorkSize.y - pad);
        win_pivot = ImVec2(1.0f, 1.0f);
        break;
    case 0: // Top-Left (Default)
    default:
        win_pos = ImVec2(viewport->WorkPos.x + pad, viewport->WorkPos.y + pad);
        win_pivot = ImVec2(0.0f, 0.0f);
        break;
    }

    ImGui::SetNextWindowPos(win_pos, ImGuiCond_Always, win_pivot);
    ImGui::SetNextWindowBgAlpha(0.55f);

    if (ImGui::Begin("##info_hud", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {

        const ImVec4 col_lbl = ImVec4(0.95f, 0.82f, 0.45f, 1.0f); // Warm Hunter Gold
        const ImVec4 col_val = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);     // Pure White

        // 1. Character Name & Level
        if (show_name) {
            const char* lbl = (lang == 1) ? "Caçador: " : (lang == 2) ? "Охотник: " : "Hunter: ";
            ImGui::TextColored(col_lbl, "%s", lbl);
            ImGui::SameLine(0.0f, 0.0f);
            if (!cached_snap.hunter_name.empty()) {
                if (cached_snap.level > 0) {
                    ImGui::TextColored(col_val, "%s (Nv. %u)", cached_snap.hunter_name.c_str(), cached_snap.level);
                } else {
                    ImGui::TextColored(col_val, "%s", cached_snap.hunter_name.c_str());
                }
            } else {
                ImGui::TextColored(col_val, "---");
            }
        }

        // 2. Playtime (Session + Total)
        if (show_time) {
            const char* lbl_sess = (lang == 1) ? "Sessão: " : (lang == 2) ? "Сессия: " : "Session: ";
            ImGui::TextColored(col_lbl, "%s", lbl_sess);
            ImGui::SameLine(0.0f, 0.0f);
            if (session_h > 0) {
                ImGui::TextColored(col_val, "%uh %02um %02us", session_h, session_m, session_s);
            } else {
                ImGui::TextColored(col_val, "%02um %02us", session_m, session_s);
            }

            ImGui::SameLine(0.0f, 10.0f * base_scale);
            const char* lbl_tot = (lang == 1) ? "Total: " : (lang == 2) ? "Всего: " : "Total: ";
            ImGui::TextColored(col_lbl, "%s", lbl_tot);
            ImGui::SameLine(0.0f, 0.0f);
            if (total_h > 0) {
                ImGui::TextColored(col_val, "%uh %02um", total_h, total_m);
            } else {
                ImGui::TextColored(col_val, "%02um %02us", total_m, total_s);
            }
        }

        // 3. Current Map / Area
        if (show_map) {
            const char* lbl_map = (lang == 1) ? "Área: " : (lang == 2) ? "Локация: " : "Area: ";
            ImGui::TextColored(col_lbl, "%s", lbl_map);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::TextColored(col_val, "%s", map_name.c_str());
        }

        // 4. Co-op / Multiplayer status
        if (show_coop) {
            const char* lbl_coop = (lang == 1) ? "Rede: " : (lang == 2) ? "Сеть: " : "Net: ";
            ImGui::TextColored(col_lbl, "%s", lbl_coop);
            ImGui::SameLine(0.0f, 0.0f);

            const bool is_coop = (cached_snap.coop_role != 0);
            const ImVec4 coop_col = is_coop ? ImVec4(0.3f, 0.9f, 1.0f, 1.0f) : col_val;
            ImGui::TextColored(coop_col, "%s", coop_status.c_str());
        }
    }
    ImGui::End();
}

} // namespace BbInfoHud
