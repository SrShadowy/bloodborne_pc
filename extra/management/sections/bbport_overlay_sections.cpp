#include "bbport_overlay_sections.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

#include "bbport_discord_rpc.h"
#include "bbport_strings.h"
#include "common/path_util.h"
#include "rpc/bbport_game_data.h"
#include "ui_manager.h"
#include "ui_strings.h"

#include <imgui.h>

namespace BbOverlaySections {
namespace {

void Checkbox(const char* label, std::atomic<bool>& value, bool& dirty) {
    bool v = value.load(std::memory_order_relaxed);
    if (ImGui::Checkbox(label, &v)) {
        value.store(v, std::memory_order_relaxed);
        dirty = true;
    }
}

void Slider(const char* label, std::atomic<float>& value, float lo, float hi, bool& dirty) {
    float v = value.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat(label, &v, lo, hi, "%.2f")) {
        value.store(v, std::memory_order_relaxed);
        dirty = true;
    }
}

void Hint(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace

#define S(id) BbStrings::Get(BbStrings::StringId::id, lang)

void RenderPhotoModeSection(BbSettings::Values& s, int lang, bool& dirty) {
    const char* sec_title = (lang == 1) ? "Modo Foto (Photo Mode)" :
                            (lang == 2) ? "Фоторежим (Photo Mode)" :
                                          "Photo Mode";
    ImGui::SeparatorText(sec_title);

    // 1. Freeze Simulation / World
    const char* freeze_label = (lang == 1) ? "Pausar Jogo (Congelar IA / Mundo) (F10)" :
                               (lang == 2) ? "Пауза (Заморозить мир / ИИ) (F10)" :
                                             "Pause Game (Freeze World / AI) (F10)";
    const char* freeze_hint = (lang == 1) ? "Congela IA, animacoes, fisica e particulas mantendo a camera e renderizacao ativas." :
                              (lang == 2) ? "Останавливает анимации, физику и таймеры, сохраняя управление камерой." :
                                            "Freezes AI, animations, physics, and world timers while keeping camera and rendering active.";
    bool pause_changed = false;
    Checkbox(freeze_label, s.photo_pause, pause_changed);
    Hint(freeze_hint);

    // 2. Hide Game HUD
    const char* hud_label = (lang == 1) ? "Ocultar HUD do Jogo (F11)" :
                            (lang == 2) ? "Скрыть интерфейс игры (F11)" :
                                          "Hide Game HUD (F11)";
    const char* hud_hint = (lang == 1) ? "Oculta barras de vida, estamina, reticulo e legendas para enquadramento limpo." :
                           (lang == 2) ? "Скрывает полоски здоровья, прицел и диалоги для чистого снимка." :
                                         "Hides health bars, stamina, reticle, and subtitles for clean framing.";
    bool hud_changed = false;
    Checkbox(hud_label, s.hide_hud, hud_changed);
    Hint(hud_hint);

    ImGui::Spacing();

    // 3. Take Screenshot button
    const char* btn_shoot = (lang == 1) ? "  Tirar Foto (F12)  " :
                            (lang == 2) ? "  Сделать снимок (F12)  " :
                                          "  Take Photo (F12)  ";
    if (ImGui::Button(btn_shoot)) {
        s.request_screenshot.store(true, std::memory_order_relaxed);
    }
    ImGui::SameLine();

    // 4. Open Screenshots Folder button
    const char* btn_folder = (lang == 1) ? "Abrir Pasta de Fotos" :
                             (lang == 2) ? "Открыть папку со снимками" :
                                           "Open Screenshots Folder";
    if (ImGui::Button(btn_folder)) {
        const auto path = Common::FS::GetUserPath(Common::FS::PathType::ScreenshotsDir).string();
        const std::string cmd = "xdg-open \"" + path + "\" &";
        (void)std::system(cmd.c_str());
    }

    ImGui::Spacing();

    // 5. Quick Reset
    const char* btn_reset = (lang == 1) ? "Restaurar Padroes de Camera e Efeitos" :
                            (lang == 2) ? "Сбросить камеру и эффекты" :
                                          "Reset Camera and Effects";
    if (ImGui::SmallButton(btn_reset)) {
        s.fov_scale = 1.0f;
        s.cam_distance = 1.0f;
        s.cam_height = 1.0f;
        s.bloom_scale = 1.0f;
        s.color_saturation = 1.0f;
        s.vignette = true;
        s.ao_strength = 1.0f;
        s.fog_opacity = 1.0f;
        dirty = true;
    }
}

void RenderPostProcessingAndLightingSection(BbSettings::Values& s, int lang, bool& dirty) {
    ImGui::SeparatorText(S(SectionPostProcess));
    Slider(S(BloomIntensity), s.bloom_scale, 0.0f, 2.0f, dirty);
    Hint(S(HintBloom));
    Slider(S(ColorSaturation), s.color_saturation, 0.0f, 2.0f, dirty);
    Hint(S(HintSaturation));
    Checkbox(S(VignetteEnable), s.vignette, dirty);
    Hint(S(HintVignette));
    Slider(S(SsaoStrength), s.ao_strength, 0.0f, 2.0f, dirty);
    Hint(S(HintSsaoStrength));
    Slider(S(ShadowDistance), s.shadow_distance, 0.5f, 3.0f, dirty);
    Hint(S(HintShadowDistance));
    Slider(S(FogOpacity), s.fog_opacity, 0.0f, 2.0f, dirty);
    Hint(S(HintFogOpacity));
}

void RenderCameraSection(BbSettings::Values& s, int lang, bool& dirty) {
    ImGui::SeparatorText(S(SectionCamera));
    Slider(S(CameraFov), s.fov_scale, 0.80f, 1.40f, dirty);
    Hint(S(HintCameraFov));
    Slider(S(CameraDistance), s.cam_distance, 0.50f, 2.50f, dirty);
    Hint(S(HintCameraDistance));
    Slider(S(CameraHeight), s.cam_height, 0.50f, 2.00f, dirty);
    Hint(S(HintCameraHeight));
}

void RenderLightGridCheckbox(BbSettings::Values& s, int lang, bool& dirty) {
    Checkbox(S(LightGridOpt), s.light_grid_opt, dirty);
    Hint(S(HintLightGridOpt));
}

void RenderRawMouseCameraControls(BbSettings::Values& s, int lang, bool& dirty) {
    Checkbox(S(RawMouseCamera), s.raw_mouse_camera, dirty);
    Hint(S(HintRawMouseCamera));
    const bool raw_on = s.raw_mouse_camera.load(std::memory_order_relaxed);
    ImGui::BeginDisabled(!raw_on);
    Checkbox(S(MouseHoldAutoRotate), s.mouse_hold_auto_rotate, dirty);
    Hint(S(HintMouseHoldAutoRotate));
    ImGui::EndDisabled();
}

void RenderInfoHudSection(BbSettings::Values& s, int lang, bool& dirty) {
    const char* sec_title = (lang == 1) ? "HUD de Informações em Tela (Info OSD)" :
                            (lang == 2) ? "Информационный оверлей (Info OSD)" :
                                          "On-Screen Info HUD (OSD)";
    ImGui::SeparatorText(sec_title);

    const char* main_label = (lang == 1) ? "Ativar HUD de Informações" :
                             (lang == 2) ? "Включить оверлей информации" :
                                           "Enable Info HUD Overlay";
    const char* main_hint = (lang == 1) ? "Exibe informações do jogo em tempo real (horas de jogo, mapa atual, nome do caçador e co-op)." :
                            (lang == 2) ? "Показывает время игры, название локации, имя охотника и статус коопа." :
                                          "Displays real-time game information (playtime, current location, character name, and co-op status).";
    Checkbox(main_label, s.show_info_hud, dirty);
    Hint(main_hint);

    const bool hud_active = s.show_info_hud.load(std::memory_order_relaxed);
    ImGui::BeginDisabled(!hud_active);

    const char* pos_label = (lang == 1) ? "Posição na Tela" :
                            (lang == 2) ? "Позиция на экране" :
                                          "Screen Position";
    const char* pos_options_pt[] = {
        "Superior Esquerdo (Padrão)",
        "Superior Direito (Abaixo do FPS)",
        "Inferior Esquerdo",
        "Inferior Direito"
    };
    const char* pos_options_en[] = {
        "Top-Left (Default)",
        "Top-Right (Below FPS)",
        "Bottom-Left",
        "Bottom-Right"
    };
    const char* pos_options_ru[] = {
        "Сверху слева (По умолчанию)",
        "Сверху справа (Под FPS)",
        "Снизу слева",
        "Снизу справа"
    };

    const char** pos_options = (lang == 1) ? pos_options_pt : (lang == 2) ? pos_options_ru : pos_options_en;
    int current_pos = std::clamp(s.info_hud_pos.load(std::memory_order_relaxed), 0, 3);
    if (ImGui::BeginCombo(pos_label, pos_options[current_pos])) {
        for (int i = 0; i < 4; ++i) {
            if (ImGui::Selectable(pos_options[i], i == current_pos)) {
                s.info_hud_pos.store(i, std::memory_order_relaxed);
                dirty = true;
            }
        }
        ImGui::EndCombo();
    }

    const char* lbl_name = (lang == 1) ? "Nome e Nível do Personagem" :
                           (lang == 2) ? "Имя и уровень охотника" :
                                         "Character Name & Level";
    Checkbox(lbl_name, s.info_hud_show_name, dirty);

    const char* lbl_map = (lang == 1) ? "Nome do Mapa / Área" :
                          (lang == 2) ? "Название локации" :
                                        "Map / Area Name";
    Checkbox(lbl_map, s.info_hud_show_map, dirty);

    const char* lbl_time = (lang == 1) ? "Horas de Jogo (Total e Sessão)" :
                           (lang == 2) ? "Время игры (Всего и за сессию)" :
                                         "Playtime (Total & Session)";
    Checkbox(lbl_time, s.info_hud_show_playtime, dirty);

    const char* lbl_coop = (lang == 1) ? "Status de Co-op (Online)" :
                           (lang == 2) ? "Статус коопа (Сеть)" :
                                         "Co-op Status (Online)";
    Checkbox(lbl_coop, s.info_hud_show_coop, dirty);

    const char* lbl_deaths = (lang == 1) ? "Contador de Mortes (Sessão)" :
                             (lang == 2) ? "Счётчик смертей (Сессия)" :
                                           "Death Counter (Session)";
    Checkbox(lbl_deaths, s.info_hud_show_deaths, dirty);

    ImGui::Spacing();
    const char* btn_reset_deaths = (lang == 1) ? "Zerar Contador de Mortes" :
                                   (lang == 2) ? "Сбросить счётчик смертей" :
                                                 "Reset Death Counter";
    if (ImGui::Button(btn_reset_deaths)) {
        BbGameData::ResetDeathCounter();
    }
    const char* hint_reset = (lang == 1) ? "Zera o contador de mortes da sessão atual." :
                             (lang == 2) ? "Сбрасывает счётчик смертей текущей сессии." :
                                           "Resets current session deaths.";
    Hint(hint_reset);

    ImGui::EndDisabled();
}

void RenderDiscordRpcSection(BbSettings::Values& s, int lang, bool& dirty) {
    const char* sec_title = "Discord Rich Presence (RPC)";
    ImGui::SeparatorText(sec_title);

    // Connection status indicator
    const bool is_connected = BbDiscordRpc::IsConnected();
    const bool rpc_enabled = s.discord_rpc.load(std::memory_order_relaxed);
    const char* status_str = BbDiscordRpc::GetStatusText(lang);

    ImVec4 status_col;
    if (!rpc_enabled) {
        status_col = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); // Gray
    } else if (is_connected) {
        status_col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f); // Bright Green
    } else {
        status_col = ImVec4(1.0f, 0.75f, 0.2f, 1.0f); // Warm Yellow/Orange
    }

    ImGui::TextColored(status_col, "%s", status_str);

    // Main Toggle
    const char* main_label = (lang == 1) ? "Ativar Discord Rich Presence" :
                             (lang == 2) ? "Включить Discord Rich Presence" :
                                           "Enable Discord Rich Presence";
    const char* main_hint = (lang == 1) ? "Mostra no seu perfil do Discord o mapa atual, cacador, nivel, tempo de jogo e modo co-op/solo." :
                            (lang == 2) ? "Показывает в Discord текущую локацию, уровень, имя охотника и статус игры." :
                                          "Displays real-time map location, character name, level, playtime, and co-op status on your Discord profile.";
    Checkbox(main_label, s.discord_rpc, dirty);
    Hint(main_hint);

    ImGui::BeginDisabled(!rpc_enabled);

    const char* btn_label = (lang == 1) ? "Configurar Presença do Discord###btn_open_drpc_win" :
                            (lang == 2) ? "Настроить статус Discord###btn_open_drpc_win" :
                                          "Configure Discord Presence###btn_open_drpc_win";
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.36f, 0.68f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.46f, 0.82f, 1.0f));
    if (ImGui::Button(btn_label, ImVec2(-1, 0))) {
        s.discord_rpc_window_open = true;
        dirty = true;
    }
    ImGui::PopStyleColor(2);
    Hint((lang == 1) ? "Abre janela com personalização granular (Nome, Nível, Mortes, Chefe, Grupo, Mapa, Tempo) e prévia em tempo real." :
         (lang == 2) ? "Открывает окно с тонкими настройками и живым предпросмотром статуса." :
                       "Opens dedicated window with granular options (Name, Level, Deaths, Boss, Party, Map, Time) and live preview.");

    ImGui::EndDisabled();
}

void RenderDiscordRpcWindow(BbSettings::Values& s, int lang, bool& dirty) {
    if (!s.discord_rpc_window_open.load(std::memory_order_relaxed)) {
        return;
    }

    bool open = true;
    const char* win_title = (lang == 1) ? "Configuração do Discord Rich Presence###discord_rpc_window" :
                            (lang == 2) ? "Настройки Discord Rich Presence###discord_rpc_window" :
                                          "Discord Rich Presence Settings###discord_rpc_window";

    ImGui::SetNextWindowSize(ImVec2(520.0f, 580.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(win_title, &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open) {
            s.discord_rpc_window_open = false;
            dirty = true;
        }
        return;
    }
    if (!open) {
        s.discord_rpc_window_open = false;
        dirty = true;
        ImGui::End();
        return;
    }

    // Status & Master Toggle
    const bool is_connected = BbDiscordRpc::IsConnected();
    const bool rpc_enabled = s.discord_rpc.load(std::memory_order_relaxed);
    const char* status_str = BbDiscordRpc::GetStatusText(lang);

    ImVec4 status_col;
    if (!rpc_enabled) {
        status_col = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    } else if (is_connected) {
        status_col = ImVec4(0.3f, 1.0f, 0.4f, 1.0f);
    } else {
        status_col = ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
    }

    ImGui::TextColored(status_col, "%s", status_str);
    ImGui::SameLine(0.0f, 20.0f);
    Checkbox((lang == 1) ? "Ativar Presença" : (lang == 2) ? "Включить статус" : "Enable Presence", s.discord_rpc, dirty);

    ImGui::Separator();

    ImGui::BeginDisabled(!rpc_enabled);

    // Section 1: Caçador & Personagem
    ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "%s",
                       (lang == 1) ? "Caçador & Personagem" : (lang == 2) ? "Охотник и персонаж" : "Hunter & Character");

    Checkbox((lang == 1) ? "Exibir Nome do Caçador" : (lang == 2) ? "Показывать имя охотника" : "Show Hunter Name",
             s.discord_rpc_show_name, dirty);
    Hint((lang == 1) ? "Exibe o nome do seu caçador do slot de save atual na linha de detalhes." :
         (lang == 2) ? "Показывает имя вашего охотника из текущего слота." :
                       "Displays your character's name from active save slot in details line.");

    Checkbox((lang == 1) ? "Exibir Nível do Caçador" : (lang == 2) ? "Показывать уровень охотника" : "Show Hunter Level",
             s.discord_rpc_show_level, dirty);
    Hint((lang == 1) ? "Exibe o nível atual do seu caçador (ex: Nv. 120)." :
         (lang == 2) ? "Показывает уровень вашего охотника (напр. Ур. 120)." :
                       "Displays hunter level (e.g. Lv. 120).");

    // Section 2: Contador de Mortes
    Checkbox((lang == 1) ? "Exibir Contador de Mortes" : (lang == 2) ? "Показывать счётчик смертей" : "Show Death Counter",
             s.discord_rpc_show_deaths, dirty);
    Hint((lang == 1) ? "Inclui a contagem de mortes da sessão atual na linha de detalhes do Discord." :
         (lang == 2) ? "Добавляет счетчик смертей текущей сессии в строку статуса Discord." :
                       "Appends session death counter to the details line in Discord.");

    ImGui::Spacing();

    // Section 3: Mundo & Combate
    ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "%s",
                       (lang == 1) ? "Mundo & Combate" : (lang == 2) ? "Мир и сражения" : "World & Combat");

    Checkbox((lang == 1) ? "Exibir Mapa / Área Atual" : (lang == 2) ? "Показывать текущую локацию" : "Show Current Map / Area",
             s.discord_rpc_show_area, dirty);
    Hint((lang == 1) ? "Exibe o nome da área e imagem correspondente no Discord." :
         (lang == 2) ? "Показывает название области и изображение локации." :
                       "Displays map area name and corresponding art in Discord.");

    Checkbox((lang == 1) ? "Exibir Combate contra Chefe" : (lang == 2) ? "Показывать бой с боссом" : "Show Boss Encounters",
             s.discord_rpc_show_boss, dirty);
    Hint((lang == 1) ? "Mostra 'Lutando: <Nome do Chefe>' na linha de estado quando em combate de chefe." :
         (lang == 2) ? "Показывает 'Бой: <Имя босса>' во время битвы с боссом." :
                       "Shows 'Fighting: <Boss Name>' during active boss battles.");

    ImGui::Spacing();

    // Section 4: Sessão & Multijogador
    ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "%s",
                       (lang == 1) ? "Sessão & Multijogador" : (lang == 2) ? "Сессия и мультиплеер" : "Session & Multiplayer");

    Checkbox((lang == 1) ? "Exibir Tempo de Sessão" : (lang == 2) ? "Показывать время сессии" : "Show Elapsed Session Time",
             s.discord_rpc_show_time, dirty);
    Hint((lang == 1) ? "Mostra o cronômetro com tempo decorrido desde o início da sessão." :
         (lang == 2) ? "Показывает таймер с начала игровой сессии." :
                       "Displays timestamp counter since game launch.");

    Checkbox((lang == 1) ? "Exibir Grupo & Co-op (NPCs / Jogadores)" : (lang == 2) ? "Показывать группу и кооп" : "Show Party & Co-op",
             s.discord_rpc_show_party, dirty);
    Hint((lang == 1) ? "Mostra tamanho do grupo (1 de 3), ajudantes NPC invocados e invasões." :
         (lang == 2) ? "Показывает размер группы (1 из 3), призванных NPC и вторжения." :
                       "Displays party size (1 of 3), summoned NPC helpers, and invasions.");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "%s",
                       (lang == 1) ? "Aplicação Discord (Client ID)" :
                       (lang == 2) ? "Приложение Discord (Client ID)" :
                                     "Discord Application (Client ID)");

    char cid_buf[64];
    std::strncpy(cid_buf, s.discord_client_id.c_str(), sizeof(cid_buf) - 1);
    cid_buf[sizeof(cid_buf) - 1] = '\0';
    ImGui::SetNextItemWidth(190.0f);
    if (ImGui::InputText("##discord_cid", cid_buf, sizeof(cid_buf))) {
        s.discord_client_id = cid_buf;
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button((lang == 1) ? "Padrão (BBPort)" : (lang == 2) ? "Сброс (BBPort)" : "Default (BBPort)")) {
        s.discord_client_id = "1560890616811102219";
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("shadPS4")) {
        s.discord_client_id = "1139939140494971051";
        dirty = true;
    }
    Hint((lang == 1) ? "ID da aplicação Discord. '1560890616811102219' exibe BloodBorne BBPort com imagens das áreas. '1139939140494971051' exibe shadPS4." :
         (lang == 2) ? "ID приложения Discord. '1560890616811102219' показывает BloodBorne BBPort с артами локаций. '1139939140494971051' показывает shadPS4." :
                       "Discord App ID. '1560890616811102219' displays BloodBorne BBPort with map art. '1139939140494971051' displays shadPS4.");

    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();

    // Section 6: Live Discord Profile Preview Card!
    ImGui::TextColored(ImVec4(0.4f, 0.75f, 1.0f, 1.0f), "%s",
                       (lang == 1) ? "Prévia em Tempo Real no Discord" :
                       (lang == 2) ? "Предпросмотр профиля Discord в реальном времени" :
                                     "Real-time Discord Profile Preview");

    BbDiscordRpc::ActivityDisplay act = BbDiscordRpc::GetCurrentActivityDisplay(lang);

    // Styled box resembling Discord's activity widget
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.13f, 0.15f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));

    if (ImGui::BeginChild("##discord_preview_card", ImVec2(0, 120.0f), ImGuiWindowFlags_NoScrollbar)) {
        ImGui::TextColored(ImVec4(0.70f, 0.72f, 0.75f, 1.0f), "%s",
                           (lang == 1) ? "JOGANDO UM JOGO" : (lang == 2) ? "ИГРАЕТ В ИГРУ" : "PLAYING A GAME");

        ImVec2 icon_pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(icon_pos, ImVec2(icon_pos.x + 50.0f, icon_pos.y + 50.0f), IM_COL32(35, 38, 44, 255), 8.0f);
        dl->AddRect(icon_pos, ImVec2(icon_pos.x + 50.0f, icon_pos.y + 50.0f), IM_COL32(88, 101, 242, 220), 8.0f, 0, 1.5f);
        dl->AddText(ImVec2(icon_pos.x + 14.0f, icon_pos.y + 16.0f), IM_COL32(235, 237, 239, 255), "BB");

        ImGui::Dummy(ImVec2(58.0f, 52.0f));
        ImGui::SameLine();

        ImGui::BeginGroup();
        // Game title
        const bool is_shad = (s.discord_client_id == "1139939140494971051");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", is_shad ? "shadPS4" : "Bloodborne (BBPort)");

        // Details line
        if (!act.details.empty()) {
            ImGui::TextColored(ImVec4(0.88f, 0.90f, 0.92f, 1.0f), "%s", act.details.c_str());
        }

        // State + Party line
        if (!act.state.empty() || act.has_party) {
            std::string st = act.state;
            if (act.has_party && !act.party_text.empty()) {
                st += " " + act.party_text;
            }
            ImGui::TextColored(ImVec4(0.72f, 0.74f, 0.78f, 1.0f), "%s", st.c_str());
        }

        // Timestamps line
        if (!act.time_text.empty()) {
            ImGui::TextColored(ImVec4(0.72f, 0.74f, 0.78f, 1.0f), "%s", act.time_text.c_str());
        }
        ImGui::EndGroup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    ImGui::Spacing();

    // Section 6: Current Stats & Quick Reset Button
    BbGameData::PresenceSnapshot snap = BbGameData::GetSnapshot();
    ImGui::TextDisabled((lang == 1) ? "Mortes na Sessão Atual: %u" :
                        (lang == 2) ? "Смертей в текущей сессии: %u" :
                                      "Current Session Deaths: %u",
                        snap.session_deaths);

    ImGui::Spacing();
    if (ImGui::Button((lang == 1) ? "Fechar Janela" : (lang == 2) ? "Закрыть окно" : "Close Window", ImVec2(120.0f, 0))) {
        s.discord_rpc_window_open = false;
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button((lang == 1) ? "Resetar Contador de Mortes" : (lang == 2) ? "Сбросить счётчик смертей" : "Reset Death Counter")) {
        BbGameData::ResetDeathCounter();
    }
    Hint((lang == 1) ? "Zera o contador de mortes da sessão atual." :
         (lang == 2) ? "Сбрасывает счётчик смертей текущей сессии." :
                       "Resets the death counter for the current gaming session.");

    ImGui::End();
}

void RenderPromptLayoutSection(BbSettings::Values& s, int lang, bool& dirty) {
    const char* prompt_options_pt[] = {
        "Automático (Detectar Dispositivo)",
        "PlayStation (Padrão Original)",
        "Xbox (A / B / X / Y)",
        "Nintendo Switch (B / A / Y / X)",
        "Teclado & Mouse"
    };
    const char* prompt_options_en[] = {
        "Auto (Detect Active Device)",
        "PlayStation (Original Default)",
        "Xbox (A / B / X / Y)",
        "Nintendo Switch (B / A / Y / X)",
        "Keyboard & Mouse"
    };
    const char* prompt_options_ru[] = {
        "Авто (Определять устройство)",
        "PlayStation (По умолчанию)",
        "Xbox (A / B / X / Y)",
        "Nintendo Switch (B / A / Y / X)",
        "Клавиатура и мышь"
    };
    const char** prompt_options = (lang == 1) ? prompt_options_pt :
                                  (lang == 2) ? prompt_options_ru :
                                                prompt_options_en;
    int cur_layout = s.prompt_layout.load(std::memory_order_relaxed);
    if (cur_layout < 0 || cur_layout >= 5) cur_layout = 0;
    const char* combo_label = (lang == 1) ? "Layout dos Botões (Prompts)" :
                              (lang == 2) ? "Раскладка подсказок кнопок" :
                                            "Button Prompts Layout";
    if (ImGui::BeginCombo(combo_label, prompt_options[cur_layout])) {
        for (int i = 0; i < 5; ++i) {
            const bool selected = (i == cur_layout);
            if (ImGui::Selectable(prompt_options[i], selected)) {
                s.prompt_layout.store(i, std::memory_order_relaxed);
                dirty = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    const char* hint_text = (lang == 1) ? "Escolhe quais ícones de botões aparecem no jogo (Examinar, Esquiva, Ataque). O modo Automático alterna dinamicamente entre seu controle e teclado." :
                            (lang == 2) ? "Выбирает подсказки кнопок в игре. Автоматический режим переключается между активным геймпадом и клавиатурой." :
                                          "Choose which button glyphs appear in-game (Examine, Roll, Attack). Auto dynamically switches between your active gamepad and keyboard.";
    Hint(hint_text);
}

void RenderMemoryToolsSection(float base_scale) {
    ImGui::Spacing();
    ImGui::SeparatorText(UI::L("Memory & Reverse Engineering", "Operações de Memória", "Операции с памятью"));
    const bool mem_open = UI::UiManager::IsMemoryWindowOpen();
    if (mem_open) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.45f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.55f, 0.48f, 1.0f));
        if (ImGui::Button(UI::L("Hide Memory Tools Window###btn_memtools",
                                "Ocultar Janela de Memória###btn_memtools",
                                "Скрыть окно памяти###btn_memtools"), ImVec2(-1, 32.0f * base_scale))) {
            UI::UiManager::SetMemoryWindowOpen(false);
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.32f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.42f, 0.54f, 1.0f));
        if (ImGui::Button(UI::L("Open Memory Tools Window (Scanner, Watchlist, Debugger)###btn_memtools",
                                "Abrir Janela de Memória (Scanner, Watchlist, Debugger)###btn_memtools",
                                "Открыть окно памяти (Сканер, Таблица, Отладчик)###btn_memtools"), ImVec2(-1, 32.0f * base_scale))) {
            UI::UiManager::SetMemoryWindowOpen(true);
        }
        ImGui::PopStyleColor(2);
    }

    const bool mv_open = UI::UiManager::IsMemoryViewerOpen();
    if (mv_open) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.24f, 0.45f, 0.40f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.55f, 0.48f, 1.0f));
        if (ImGui::Button(UI::L("Hide Memory Viewer###btn_memviewer",
                                "Ocultar Memory Viewer###btn_memviewer",
                                "Скрыть Memory Viewer###btn_memviewer"), ImVec2(-1, 28.0f * base_scale))) {
            UI::UiManager::SetMemoryViewerOpen(false);
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.32f, 0.42f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.42f, 0.54f, 1.0f));
        if (ImGui::Button(UI::L("Open Memory Viewer###btn_memviewer",
                                "Abrir Memory Viewer###btn_memviewer",
                                "Открыть Memory Viewer###btn_memviewer"), ImVec2(-1, 28.0f * base_scale))) {
            UI::UiManager::SetMemoryViewerOpen(true);
        }
        ImGui::PopStyleColor(2);
    }
    ImGui::TextDisabled("%s", UI::L("Opens separate floating windows for memory scanning, hex view, and live disassembly",
                                   "Abre janelas flutuantes separadas para varredura, hex view e desassemblagem em tempo real",
                                   "Открывает отдельные плавающие окна для сканирования, hex-инспектора и дизассемблера"));
}

#undef S

} // namespace BbOverlaySections
