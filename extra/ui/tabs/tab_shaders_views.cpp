// SPDX-License-Identifier: GPL-2.0-or-later
#include "tab_shaders_views.h"
#include "ui_manager.h"
#include "ui_strings.h"
#include "extra/core/shader_precompile/bbport_shader_discovery.h"
#include "extra/core/shader_precompile/bbport_shader_pack.h"
#include "extra/core/shader_precompile/bbport_static_catalog.h"
#include "extra/core/shader_precompile/bbport_vk_pipeline_cache.h"

#include <imgui.h>
#include <cstdio>
#include <string>
#include <vector>

namespace UI {

namespace {

static char s_search_hash[32] = "";
static bool s_auto_scroll = true;
static int s_class_filter = 0; // 0 = All, 1 = Match, 2 = Runtime, 3 = Static

const char* StageName(uint32_t s) {
    switch (s) {
    case 0: return "FS/PS";
    case 1: return "VS";
    case 2: return "GS";
    case 3: return "TCS";
    case 4: return "TES";
    case 5:
    case 6: return "CS";
    default: return "UNK";
    }
}

ImVec4 StageColor(uint32_t s) {
    switch (s) {
    case 0: return ImVec4(0.95f, 0.55f, 0.25f, 1.0f);
    case 1: return ImVec4(0.35f, 0.70f, 1.0f, 1.0f);
    case 5:
    case 6: return ImVec4(0.85f, 0.35f, 0.95f, 1.0f);
    default: return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
    }
}

} // namespace

void TabShadersViews::RenderOverview(const std::filesystem::path& cache_dir) {
    const auto pstats = BbShaderPack::GetArchiveStats(cache_dir);
    const auto dstatus = BbVkPipelineCache::GetDriverCacheStatus();
    uint32_t preloaded = 0, total_keys = 0;
    BbShaderDiscovery::GetProgress(preloaded, total_keys);
    const uint32_t stale = (total_keys > preloaded) ? (total_keys - preloaded) : 0;
    const float rebuild_pct = total_keys > 0 ? (100.0f * preloaded / total_keys) : 0.0f;

    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "[1] %s",
                       L("Portable Grimoire Assets (Cross-Platform)", "Artefatos Portaveis do Grimorio"));
    if (pstats.valid) {
        ImGui::BulletText("%s: %zu KB | %s: %zu KB | %s: %zu KB",
                          BbShaderPack::ARCHIVE_INDEX_NAME, pstats.index_file_size / 1024,
                          BbShaderPack::ARCHIVE_SHADERS_NAME, pstats.shaders_file_size / 1024,
                          BbShaderPack::ARCHIVE_METADATA_NAME, pstats.meta_file_size / 1024);
        ImGui::TextDisabled("  -> %u shaders, %u pipelines (%s)",
                            pstats.shader_count, pstats.pipeline_count,
                            L("Agnostic to GPU/driver", "Agnostico de GPU e driver"));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.30f, 1.0f), "  [!] %s",
                           L("Grimoire not packed yet.", "Grimorio ainda nao empacotado."));
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "[2] %s",
                       L("Local Driver Binary Cache (Hardware Specific)", "Cache Binaria de Driver Local"));
    ImGui::BulletText("%s: %s (%zu KB)",
                      BbVkPipelineCache::DRIVER_CACHE_NAME,
                      dstatus.loaded ? "LOADED [HIT]" : "NOT LOADED",
                      dstatus.size_bytes / 1024);
    ImGui::TextDisabled("  -> GPU: %s (0x%04x:0x%04x)",
                        dstatus.device_name.empty() ? "Vulkan Device" : dstatus.device_name.c_str(),
                        dstatus.vendor_id, dstatus.device_id);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.95f, 1.0f), "[3] %s",
                       L("Rebuild Health & Reused Rates", "Saude da Reconstrucao e Taxa de Reuso"));
    ImGui::Text("%s: %u / %u (%.1f%%) | %s: %u",
                L("Valid Rebuilt", "Reconstruidos"), preloaded, total_keys, rebuild_pct,
                L("Stale/Rejected", "Obsoletos/Rejeitados"), stale);
    ImGui::ProgressBar(rebuild_pct / 100.0f, ImVec2(-1.0f, 0.0f));
}

void TabShadersViews::RenderLiveEvents() {
    ImGui::Checkbox(L("Auto-scroll", "Auto-rolagem"), &s_auto_scroll);
    const auto events = BbShaderDiscovery::GetRecentEvents();
    if (ImGui::BeginTable("EventsTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 320))) {
        ImGui::TableSetupColumn(L("Type", "Tipo"), ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn(L("Stage", "Estagio"), ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn(L("Hash", "Hash"), ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn(L("Duration", "Duracao"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn(L("Location / Details", "Localidade / Detalhes"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        int evt_idx = 0;
        for (const auto& evt : events) {
            ImGui::PushID(evt_idx++);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (evt.is_new) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.25f, 1.0f), "[NEW %s]", evt.is_pipeline ? "PIPE" : "SHDR");
            } else {
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "[CACHE]");
            }
            ImGui::TableSetColumnIndex(1);
            if (!evt.is_pipeline) {
                ImGui::TextColored(StageColor(evt.stage), "%s", StageName(evt.stage));
            } else {
                ImGui::Text("%s", evt.is_compute ? "COMP" : "GFX");
            }
            ImGui::TableSetColumnIndex(2);
            char h_str[32];
            std::snprintf(h_str, sizeof(h_str), "0x%016llx", static_cast<unsigned long long>(evt.hash));
            if (ImGui::Selectable(h_str, false)) {
                ImGui::SetClipboardText(h_str);
                UiManager::SetStatus(std::string("Copied: ") + h_str);
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.2f ms", evt.duration_ns / 1000000.0);
            ImGui::TableSetColumnIndex(4);
            if (!evt.is_pipeline) {
                ImGui::Text("%s (%u B, perm %u)", evt.location.c_str(), evt.size_bytes, evt.perm_idx);
            } else {
                ImGui::Text("%s (%s pipeline)", evt.location.c_str(), evt.is_compute ? "Compute" : "Graphics");
            }
            ImGui::PopID();
        }
        if (s_auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndTable();
    }
}

void TabShadersViews::RenderInventory() {
    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##SearchHash", L("Filter hash...", "Filtrar hash..."), s_search_hash, sizeof(s_search_hash));
    ImGui::SameLine();
    ImGui::RadioButton(L("All", "Todos"), &s_class_filter, 0); ImGui::SameLine();
    ImGui::RadioButton(L("Match", "Coincidentes"), &s_class_filter, 1); ImGui::SameLine();
    ImGui::RadioButton(L("Runtime Only", "Apenas Runtime"), &s_class_filter, 2);

    auto shaders = BbShaderDiscovery::GetShaders();
    if (ImGui::BeginTable("ShadersTable", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable, ImVec2(0, 320))) {
        ImGui::TableSetupColumn(L("Class", "Classe"), ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn(L("Stage", "Estagio"), ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn(L("Program Hash", "Hash"), ImGuiTableColumnFlags_WidthFixed, 145.0f);
        ImGui::TableSetupColumn(L("Perm", "Perm"), ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn(L("SPIR-V", "SPIR-V"), ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn(L("Time", "Tempo"), ImGuiTableColumnFlags_WidthFixed, 75.0f);
        ImGui::TableSetupColumn(L("Hits | Location", "Hits | Local"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        int sh_idx = 0;
        for (const auto& sh : shaders) {
            char h_str[32];
            std::snprintf(h_str, sizeof(h_str), "0x%016llx", static_cast<unsigned long long>(sh.pgm_hash));
            if (s_search_hash[0] != '\0' && std::string(h_str).find(s_search_hash) == std::string::npos) {
                continue;
            }
            const auto c = BbStaticCatalog::ClassifyShader(sh.pgm_hash);
            if (s_class_filter == 1 && c != BbStaticCatalog::ShaderClassification::ConfirmedMatch) continue;
            if (s_class_filter == 2 && c != BbStaticCatalog::ShaderClassification::RuntimeOnly) continue;

            ImGui::PushID(sh_idx++);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (c == BbStaticCatalog::ShaderClassification::ConfirmedMatch) {
                ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.65f, 1.0f), "[MATCH]");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.30f, 1.0f), "[RUN]");
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(StageColor(sh.hw_stage), "%s", StageName(sh.hw_stage));
            ImGui::TableSetColumnIndex(2);
            if (ImGui::Selectable(h_str, false)) {
                ImGui::SetClipboardText(h_str);
                UiManager::SetStatus(std::string("Copied: ") + h_str);
            }
            ImGui::TableSetColumnIndex(3); ImGui::Text("%u", sh.perm_idx);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%u W", sh.spv_size_words);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%.2f ms", sh.compile_time_ns / 1000000.0);
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%llu | %s", static_cast<unsigned long long>(sh.hit_count), sh.location.empty() ? "-" : sh.location.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void TabShadersViews::RenderStaticCatalog() {
    if (BbStaticCatalog::IsLoaded()) {
        const auto cstats = BbStaticCatalog::GetClassificationStats();
        const float pct = BbStaticCatalog::GetStaticCoveragePercentage();
        ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.65f, 1.0f), "[OK] %s",
                           L("Game Shaders Catalog Loaded (4,376 Programs)", "Catalogo de Shaders do Jogo Carregado"));
        ImGui::Text("%s: %u | %s: %u (%.1f%%) | %s: %u",
                    L("Total Static", "Total Estatico"), cstats.total_static,
                    L("Confirmed Matches", "Coincidentes"), cstats.confirmed_match, pct,
                    L("Runtime Only", "Apenas Runtime"), cstats.runtime_only);
        ImGui::ProgressBar(pct / 100.0f, ImVec2(-1.0f, 0.0f));

        ImGui::Separator();
        ImGui::TextDisabled("%s:", L("Game Bundles Breakdown", "Detalhamento por Pacote do Jogo"));
        for (const auto& b : BbStaticCatalog::GetBundleStats()) {
            ImGui::BulletText("%s: %u shaders (%llu KB decomp)",
                              b.name.c_str(), b.count, static_cast<unsigned long long>(b.decompressed_size / 1024));
        }
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.30f, 1.0f), "[!] %s",
                           L("Static catalog not loaded.", "Catalogo estatico nao carregado."));
        if (ImGui::Button(L("Load Static Catalog", "Carregar Catalogo Estatico"))) {
            BbStaticCatalog::AutoLoad("dist/user/cache");
        }
    }
}

void TabShadersViews::RenderDiagnostics(const std::filesystem::path& cache_dir) {
    uint32_t preloaded = 0, total_keys = 0;
    BbShaderDiscovery::GetProgress(preloaded, total_keys);
    const uint32_t stale = (total_keys > preloaded) ? (total_keys - preloaded) : 0;

    ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.35f, 1.0f), "%s",
                       L("Shader System Diagnostics & Verification", "Diagnostico & Verificacao de Shaders"));
    ImGui::BulletText("%s: %u", L("Total Pipeline Keys on Disk", "Total de Chaves no Disco"), total_keys);
    ImGui::BulletText("%s: %u", L("Successfully Rebuilt", "Reconstruidos com Sucesso"), preloaded);
    ImGui::BulletText("%s: %u (%s)", L("Stale/Rejected Keys", "Chaves Rejeitadas"), stale,
                      L("Permutation index collisions or outdated hashes", "Conflitos de permutacao ou hashes antigos"));

    ImGui::Spacing();
    const auto report_path = cache_dir / "shader_diagnostic_report.json";
    if (ImGui::Button(L("Export Diagnostic Report (JSON)", "Exportar Relatorio de Diagnostico (JSON)"))) {
        if (BbStaticCatalog::ExportDiagnosticReport(report_path)) {
            UiManager::SetStatus(std::string("Report exported: ") + report_path.string());
        } else {
            UiManager::SetStatus(L("Failed to export report.", "Falha ao exportar relatorio."));
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", report_path.filename().string().c_str());
}

} // namespace UI
