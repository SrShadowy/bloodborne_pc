// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: Monster Hunter Rise style shader precompilation splash screen.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace BbShaderSplash {

struct SplashOptions {
    std::filesystem::path cache_dir;
    uint32_t target_pipelines = 801;
    bool headless = false;
    std::string splash_image_path = "packaging/splash_bloodborne.png";
};

// Runs the interactive splash screen on the main thread while executing compile_task on a worker thread.
// If headless is true or no graphical display is available, compiles silently with CLI progress output.
bool Run(const SplashOptions& options, std::function<bool()> compile_task);

} // namespace BbShaderSplash
