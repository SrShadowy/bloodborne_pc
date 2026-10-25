// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: in-game debugger shader tracker and diagnostic console tab.
#include "tab_shaders.h"
#include "tab_shaders_views.h"
#include "ui_manager.h"
#include "ui_strings.h"
#include "extra/core/shader_precompile/bbport_shader_discovery.h"
#include "extra/core/shader_precompile/bbport_shader_pack.h"
#include "extra/core/shader_precompile/bbport_static_catalog.h"
#include "common/path_util.h"

#include <imgui.h>
#include <cstdio>
#include <string>

namespace UI {

void TabShaders::RenderSummaryCard() {
    const auto stats = BbShaderDiscovery::GetStats();
    const uint32_t new_shaders = BbShaderDiscovery::GetNewShaderCount();
    const uint32_t new_pipes = BbShaderDiscovery::GetNewPipelineCount();

    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.75f, 1.0f), "%s",
                       L("Shader Pipeline & Discovery Tracker", "Rastreador de Shaders & Pipelines"));
    ImGui::SameLine();
    ImGui::TextDisabled("| %s: %u | %s: %u",
                        L("Shaders", "Shaders"), stats.unique_shaders,
                        L("Pipelines", "Pipelines"), stats.unique_pipelines);

    if (new_shaders > 0 || new_pipes > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.25f, 1.0f),
                           "[*] %s: +%u %s, +%u %s",
                           L("New Runtime Compilations", "Novas Compilacoes ao Vivo"),
                           new_shaders, L("shaders", "shaders"),
                           new_pipes, L("pipelines", "pipelines"));
    } else {
        ImGui::TextDisabled("%s", L("All shaders currently loaded from cache.",
                                   "Todos os shaders carregados do cache."));
    }

    if (ImGui::SmallButton(L("Open Shaders Console", "Abrir Console de Shaders"))) {
        UiManager::RequestTab(TabId::Shaders);
    }
}

void TabShaders::Render() {
    const auto stats = BbShaderDiscovery::GetStats();
    const uint32_t new_shaders = BbShaderDiscovery::GetNewShaderCount();
    const uint32_t new_pipes = BbShaderDiscovery::GetNewPipelineCount();

    // Top metrics header
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.65f, 1.0f), "%s",
                       L("Hunter's Shader & Pipeline Console", "Console de Shaders do Cacador"));
    ImGui::Separator();

    ImGui::Text("%s: %u | %s: %u | %s: %llu",
                L("Unique Shaders", "Shaders Unicos"), stats.unique_shaders,
                L("Unique Pipelines", "Pipelines Unicas"), stats.unique_pipelines,
                L("Cache Hits", "Hits de Cache"), static_cast<unsigned long long>(stats.shader_hits + stats.pipeline_hits));
    ImGui::SameLine();
    if (new_shaders > 0 || new_pipes > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.25f, 1.0f), "| %s: %u (+%u pipes)",
                           L("New Dynamic Compiles", "Novas Compilacoes"), new_shaders, new_pipes);
    } else {
        ImGui::TextDisabled("| %s: 0", L("New Dynamic Compiles", "Novas Compilacoes"));
    }

    bool discovery_enabled = BbShaderDiscovery::IsEnabled();
    if (ImGui::Checkbox(L("Live Draw Call Telemetry (Disable for max FPS)",
                          "Telemetria ao Vivo (Desative para maximo FPS)"), &discovery_enabled)) {
        BbShaderDiscovery::SetEnabled(discovery_enabled);
        UiManager::SetStatus(discovery_enabled ? L("Live shader telemetry enabled.", "Telemetria de shaders ativada.")
                                               : L("Shader telemetry paused (Zero CPU overhead).", "Telemetria pausada (Zero overhead de CPU)."));
    }

    // Action buttons toolbar
    const auto cache_dir = Common::FS::GetUserPath(Common::FS::PathType::CacheDir) / "CUSA03173";
    if (ImGui::Button(L("Sync Master Grimoire", "Sincronizar Grimorio Mestre"))) {
        if (BbShaderPack::RestoreMasterCacheIfEmpty(cache_dir)) {
            UiManager::SetStatus(L("Master Grimoire assets merged successfully!", "Grimorio mestre sincronizado!"));
        } else {
            UiManager::SetStatus(L("Grimoire already up to date.", "Grimorio ja atualizado."));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Pack Grimoire", "Empacotar Grimorio"))) {
        if (BbShaderPack::PackDirectory(cache_dir, "CUSA03173")) {
            UiManager::SetStatus(L("Grimoire 3-file archive packed!", "Grimorio empacotado com sucesso!"));
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Clear Log", "Limpar Log"))) {
        BbShaderDiscovery::ClearEvents();
    }

    ImGui::Spacing();
    if (ImGui::BeginTabBar("ShaderSubTabs", ImGuiTabBarFlags_None)) {
        // Tab 1: Overview & Hardware / Cache layers
        if (ImGui::BeginTabItem(L("Overview & Cache###OverviewTab", "Visao Geral & Cache###OverviewTab"))) {
            TabShadersViews::RenderOverview(cache_dir);
            ImGui::EndTabItem();
        }

        // Tab 2: Live Event Logger
        if (ImGui::BeginTabItem(L("Live Event Log###EventLog", "Log de Eventos###EventLog"))) {
            TabShadersViews::RenderLiveEvents();
            ImGui::EndTabItem();
        }

        // Tab 3: Shader Inventory (Classified)
        if (ImGui::BeginTabItem(L("Shader Inventory###InventoryTab", "Inventario de Shaders###InventoryTab"))) {
            TabShadersViews::RenderInventory();
            ImGui::EndTabItem();
        }

        // Tab 4: Static Catalog & Coverage
        if (ImGui::BeginTabItem(L("Static Catalog###StaticTab", "Catalogo Estatico###StaticTab"))) {
            TabShadersViews::RenderStaticCatalog();
            ImGui::EndTabItem();
        }

        // Tab 5: Diagnostics & Export
        if (ImGui::BeginTabItem(L("Diagnostics & Export###DiagnosticsTab", "Diagnostico & Exportacao###DiagnosticsTab"))) {
            TabShadersViews::RenderDiagnostics(cache_dir);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

} // namespace UI
