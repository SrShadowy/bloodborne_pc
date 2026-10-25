// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: sub-views for shader debugger (Overview, Inventory, Diagnostics).
#pragma once

#include <filesystem>

namespace UI {

class TabShadersViews {
public:
    static void RenderOverview(const std::filesystem::path& cache_dir);
    static void RenderLiveEvents();
    static void RenderInventory();
    static void RenderStaticCatalog();
    static void RenderDiagnostics(const std::filesystem::path& cache_dir);
};

} // namespace UI
