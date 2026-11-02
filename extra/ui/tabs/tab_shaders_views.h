// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: sub-views for the shader console (Overview, Live Events, Inventory, Diagnostics).
#pragma once

#include <filesystem>

namespace UI {

class TabShadersViews {
public:
    static void RenderOverview(const std::filesystem::path& cache_dir);
    static void RenderLiveEvents();
    static void RenderInventory();
    static void RenderDiagnostics(const std::filesystem::path& cache_dir);
};

} // namespace UI
