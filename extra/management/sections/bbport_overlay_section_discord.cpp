// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the Discord Rich Presence setting -- its row in the settings menu and the window with the
// individual presence options, the client ID and a live preview of the card.
#include "bbport_overlay_sections.h"

#include <cstring>
#include <string>

#include "bbport_discord_rpc.h"
#include "bbport_game_data.h"
#include "bbport_overlay_ui.h"

#include <imgui.h>

namespace BbOverlaySections {
namespace {

/// The colour of the connection line: grey when off, green when connected, amber while waiting.
ImVec4 StatusColor(bool enabled, bool connected) {
    if (!enabled) {
        return Theme::kStatusOff;
    }
    return connected ? Theme::kStatusOnline : Theme::kStatusPending;
}

/// Heading inside the window, one per group of options.
void Heading(const char* text) {
    ImGui::TextColored(Theme::kTextHeading, "%s", text);
}

/// A checkbox with its explanation as the "(?)" tooltip, the window's usual pair.
void Option(const char* label, const char* hint, std::atomic<bool>& value, bool& dirty) {
    Checkbox(label, value, dirty);
    Hint(hint);
}

/// The client ID field with the two preset buttons under it.
void ClientIdField(BbSettings::Values& s, int lang, bool& dirty) {
    char buffer[64];
    std::strncpy(buffer, s.discord_client_id.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    ImGui::SetNextItemWidth(Size::kClientIdFieldWidth);
    if (ImGui::InputText("##discord_cid", buffer, sizeof(buffer))) {
        s.discord_client_id = buffer;
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr(lang, "Default (BBPort)", "Padrão (BBPort)", "Сброс (BBPort)"))) {
        s.discord_client_id = BbDiscordRpc::kBbportClientId;
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("shadPS4")) {
        s.discord_client_id = BbDiscordRpc::kShadps4ClientId;
        dirty = true;
    }
    Hint(Tr(lang, "Discord App ID. '1560890616811102219' displays BloodBorne BBPort with map art. "
                  "'1139939140494971051' displays shadPS4.",
             "ID da aplicação Discord. '1560890616811102219' exibe BloodBorne BBPort com imagens das áreas. "
             "'1139939140494971051' exibe shadPS4.",
             "ID приложения Discord. '1560890616811102219' показывает BloodBorne BBPort с артами локаций. "
             "'1139939140494971051' показывает shadPS4."));
}

/// The "playing a game" card, drawn the way the Discord client draws it.
void ActivityPreviewCard(const BbSettings::Values& s, int lang) {
    const BbDiscordRpc::ActivityDisplay activity = BbDiscordRpc::GetCurrentActivityDisplay(lang);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::kCardBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));

    if (ImGui::BeginChild("##discord_preview_card", ImVec2(0, Size::kActivityCardHeight),
                          ImGuiWindowFlags_NoScrollbar)) {
        ImGui::TextColored(Theme::kTextMuted, "%s",
                           Tr(lang, "PLAYING A GAME", "JOGANDO UM JOGO", "ИГРАЕТ В ИГРУ"));

        const ImVec2 icon_pos = ImGui::GetCursorScreenPos();
        const ImVec2 icon_end(icon_pos.x + Size::kActivityIconSize, icon_pos.y + Size::kActivityIconSize);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(icon_pos, icon_end, Theme::kCardIconBackground, Size::kActivityIconRounding);
        draw_list->AddRect(icon_pos, icon_end, Theme::kCardIconBorder, Size::kActivityIconRounding, 0, 1.5f);
        draw_list->AddText(ImVec2(icon_pos.x + Size::kActivityIconTextOffset,
                                  icon_pos.y + Size::kActivityIconTextTop),
                           Theme::kCardIconText, "BB");

        ImGui::Dummy(ImVec2(58.0f, 52.0f));
        ImGui::SameLine();

        ImGui::BeginGroup();
        const bool is_shadps4 = (s.discord_client_id == BbDiscordRpc::kShadps4ClientId);
        ImGui::TextColored(Theme::kTextTitle, "%s", is_shadps4 ? "shadPS4" : "Bloodborne (BBPort)");

        if (!activity.details.empty()) {
            ImGui::TextColored(Theme::kTextPrimary, "%s", activity.details.c_str());
        }

        if (!activity.state.empty() || activity.has_party) {
            std::string state = activity.state;
            if (activity.has_party && !activity.party_text.empty()) {
                state += " " + activity.party_text;
            }
            ImGui::TextColored(Theme::kTextSecondary, "%s", state.c_str());
        }

        if (!activity.time_text.empty()) {
            ImGui::TextColored(Theme::kTextSecondary, "%s", activity.time_text.c_str());
        }
        ImGui::EndGroup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace

void RenderDiscordRpcSection(BbSettings::Values& s, int lang, bool& dirty) {
    BbDiscordRpc::Init();
    ImGui::SeparatorText("Discord Rich Presence (RPC)");

    const bool enabled = s.discord_rpc.load(std::memory_order_relaxed);
    ImGui::TextColored(StatusColor(enabled, BbDiscordRpc::IsConnected()), "%s",
                       BbDiscordRpc::GetStatusText(lang));

    Checkbox(Tr(lang, "Enable Discord Rich Presence", "Ativar Discord Rich Presence",
                 "Включить Discord Rich Presence"),
             s.discord_rpc, dirty);
    Hint(Tr(lang, "Displays real-time map location, character name, level, playtime, and co-op status on your Discord profile.",
             "Mostra no seu perfil do Discord o mapa atual, cacador, nivel, tempo de jogo e modo co-op/solo.",
             "Показывает в Discord текущую локацию, уровень, имя охотника и статус игры."));

    ImGui::BeginDisabled(!enabled);

    ImGui::PushStyleColor(ImGuiCol_Button, Theme::kButtonPrimary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::kButtonPrimaryHovered);
    if (ImGui::Button(Tr(lang, "Configure Discord Presence###btn_open_drpc_win",
                         "Configurar Presença do Discord###btn_open_drpc_win",
                         "Настроить статус Discord###btn_open_drpc_win"),
                      ImVec2(-Size::kFullRowRightPad, 0))) {
        s.discord_rpc_window_open = true;
        dirty = true;
    }
    ImGui::PopStyleColor(2);
    Hint(Tr(lang, "Opens dedicated window with granular options (Name, Level, Deaths, Boss, Party, Map, Time) and live preview.",
             "Abre janela com personalização granular (Nome, Nível, Mortes, Chefe, Grupo, Mapa, Tempo) e prévia em tempo real.",
             "Открывает окно с тонкими настройками и живым предпросмотром статуса."));

    ImGui::EndDisabled();
}

void RenderDiscordRpcWindow(BbSettings::Values& s, int lang, bool& dirty) {
    if (!s.discord_rpc_window_open.load(std::memory_order_relaxed)) {
        return;
    }

    bool open = true;
    ImGui::SetNextWindowSize(ImVec2(Size::kDiscordWindowWidth, Size::kDiscordWindowHeight),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(Tr(lang, "Discord Rich Presence Settings###discord_rpc_window",
                         "Configuração do Discord Rich Presence###discord_rpc_window",
                         "Настройки Discord Rich Presence###discord_rpc_window"),
                      &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!open) {
            s.discord_rpc_window_open.store(false, std::memory_order_relaxed);
            dirty = true;
        }
        return;
    }
    if (!open) {
        s.discord_rpc_window_open.store(false, std::memory_order_relaxed);
        dirty = true;
        ImGui::End();
        return;
    }

    const bool enabled = s.discord_rpc.load(std::memory_order_relaxed);
    ImGui::TextColored(StatusColor(enabled, BbDiscordRpc::IsConnected()), "%s",
                       BbDiscordRpc::GetStatusText(lang));
    ImGui::SameLine(0.0f, Size::kSameLineGap);
    Checkbox(Tr(lang, "Enable Presence", "Ativar Presença", "Включить статус"), s.discord_rpc, dirty);

    ImGui::Separator();
    ImGui::BeginDisabled(!enabled);

    Heading(Tr(lang, "Hunter & Character", "Caçador & Personagem", "Охотник и персонаж"));
    Option(Tr(lang, "Show Hunter Name", "Exibir Nome do Caçador", "Показывать имя охотника"),
           Tr(lang, "Displays your character's name from active save slot in details line.",
                  "Exibe o nome do seu caçador do slot de save atual na linha de detalhes.",
                  "Показывает имя вашего охотника из текущего слота."),
           s.discord_rpc_show_name, dirty);
    Option(Tr(lang, "Show Hunter Level", "Exibir Nível do Caçador", "Показывать уровень охотника"),
           Tr(lang, "Displays hunter level (e.g. Lv. 120).", "Exibe o nível atual do seu caçador (ex: Nv. 120).",
                  "Показывает уровень вашего охотника (напр. Ур. 120)."),
           s.discord_rpc_show_level, dirty);

    Option(Tr(lang, "Show Death Counter", "Exibir Contador de Mortes", "Показывать счётчик смертей"),
           Tr(lang, "Appends session death counter to the details line in Discord.",
                  "Inclui a contagem de mortes da sessão atual na linha de detalhes do Discord.",
                  "Добавляет счетчик смертей текущей сессии в строку статуса Discord."),
           s.discord_rpc_show_deaths, dirty);

    ImGui::Spacing();
    Heading(Tr(lang, "World & Combat", "Mundo & Combate", "Мир и сражения"));
    Option(Tr(lang, "Show Current Map / Area", "Exibir Mapa / Área Atual", "Показывать текущую локацию"),
           Tr(lang, "Displays map area name and corresponding art in Discord.",
                  "Exibe o nome da área e imagem correspondente no Discord.",
                  "Показывает название области и изображение локации."),
           s.discord_rpc_show_area, dirty);
    Option(Tr(lang, "Show Boss Encounters", "Exibir Combate contra Chefe", "Показывать бой с боссом"),
           Tr(lang, "Shows 'Fighting: <Boss Name>' during active boss battles.",
                  "Mostra 'Lutando: <Nome do Chefe>' na linha de estado quando em combate de chefe.",
                  "Показывает 'Бой: <Имя босса>' во время битвы с боссом."),
           s.discord_rpc_show_boss, dirty);

    ImGui::Spacing();
    Heading(Tr(lang, "Session & Multiplayer", "Sessão & Multijogador", "Сессия и мультиплеер"));
    Option(Tr(lang, "Show Elapsed Session Time", "Exibir Tempo de Sessão", "Показывать время сессии"),
           Tr(lang, "Displays timestamp counter since game launch.",
                  "Mostra o cronômetro com tempo decorrido desde o início da sessão.",
                  "Показывает таймер с начала игровой сессии."),
           s.discord_rpc_show_time, dirty);
    Option(Tr(lang, "Show Party & Co-op", "Exibir Grupo & Co-op (NPCs / Jogadores)", "Показывать группу и кооп"),
           Tr(lang, "Displays party size (1 of 3), summoned NPC helpers, and invasions.",
                  "Mostra tamanho do grupo (1 de 3), ajudantes NPC invocados e invasões.",
                  "Показывает размер группы (1 из 3), призванных NPC и вторжения."),
           s.discord_rpc_show_party, dirty);

    ImGui::Spacing();
    Heading(Tr(lang, "Discord Application (Client ID)", "Aplicação Discord (Client ID)",
                "Приложение Discord (Client ID)"));
    ClientIdField(s, lang, dirty);

    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::Separator();

    Heading(Tr(lang, "Real-time Discord Profile Preview", "Prévia em Tempo Real no Discord",
                "Предпросмотр профиля Discord в реальном времени"));
    ActivityPreviewCard(s, lang);

    ImGui::Spacing();
    ImGui::TextDisabled(Tr(lang, "Current Session Deaths: %u", "Mortes na Sessão Atual: %u",
                           "Смертей в текущей сессии: %u"),
                        BbGameData::GetSnapshot().session_deaths);

    ImGui::Spacing();
    if (ImGui::Button(Tr(lang, "Close Window", "Fechar Janela", "Закрыть окно"),
                      ImVec2(Size::kCloseButtonWidth, 0))) {
        s.discord_rpc_window_open.store(false, std::memory_order_relaxed);
        dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr(lang, "Reset Death Counter", "Resetar Contador de Mortes",
                         "Сбросить счётчик смертей"))) {
        BbGameData::ResetDeathCounter();
    }
    Hint(Tr(lang, "Resets the death counter for the current gaming session.",
             "Zera o contador de mortes da sessão atual.",
             "Сбрасывает счётчик смертей текущей сессии."));

    ImGui::End();
}

} // namespace BbOverlaySections
