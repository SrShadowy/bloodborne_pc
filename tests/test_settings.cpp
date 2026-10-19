// SPDX-License-Identifier: GPL-2.0-or-later
// bbport_settings.cpp: the menu saves its keys into bbport.ini and keeps the launcher's (controls).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include "gpu/shim/bbport_settings.h"

static std::string Read(const char* path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

int main() {
    assert(!BbSettings::IsHotEffect(6)); // SSR resizes resources on restart.
    assert(BbSettings::IsHotEffect(7));  // Bloom still switches live.
    assert(!BbSettings::IsHotEffect(8)); // Startup intros.
    char path[] = "/tmp/bbport-settings-test-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0);
    const char ini[] = "# launcher\nupscaler=fsr3\nkey.cross=X, Space\npad.circle=a\nshow_fps=0\n"
                       "fullscreen_hint=1\n";
    assert(write(fd, ini, sizeof(ini) - 1) == ssize_t(sizeof(ini) - 1));
    close(fd);
    setenv("BB_CONFIG", path, 1);

    BbSettings::Load();
    auto& s = BbSettings::Get();
    assert(s.upscaler == BbSettings::UpscalerFsr3 && !s.show_fps && s.menu_x < 0.0f);
    s.show_fps = true;
    s.upscaler = BbSettings::UpscalerFsr411;
    s.menu_x = 0.625f;
    s.menu_y = 0.125f;
    BbSettings::Save();

    const std::string saved = Read(path);
    // The launcher's controls, its other keys and comments stay; the menu's keys are replaced
    // in place, new ones appended.
    assert(saved.find("# launcher\nupscaler=fsr411\nkey.cross=X, Space\npad.circle=a\nshow_fps=1\n"
                      "fullscreen_hint=1\n") == 0);
    assert(saved.find("menu_pos=0.6250,0.1250\n") != std::string::npos);
    assert(saved.find("upscaler=fsr3") == std::string::npos);

    s.menu_x = -1.0f;
    s.menu_y = -1.0f;
    s.raw_mouse_camera = false;
    s.mouse_hold_auto_rotate = false;
    s.bloom_scale = 1.35f;
    s.color_saturation = 1.15f;
    s.vignette = false;
    s.ao_strength = 1.50f;
    s.shadow_distance = 2.00f;
    s.fog_opacity = 0.75f;
    s.fov_scale = 1.20f;
    s.cam_distance = 1.40f;
    s.cam_height = 1.10f;
    s.light_grid_opt = false;
    s.hide_hud = true;
    BbSettings::Save();

    s.raw_mouse_camera = true;
    s.mouse_hold_auto_rotate = true;
    s.bloom_scale = 1.0f;
    s.vignette = true;
    s.fov_scale = 1.0f;
    s.light_grid_opt = true;
    s.hide_hud = false;
    BbSettings::Load();
    assert(s.menu_x == 0.625f && s.menu_y == 0.125f && s.upscaler == BbSettings::UpscalerFsr411);
    assert(!s.raw_mouse_camera && !s.mouse_hold_auto_rotate && !s.light_grid_opt && !s.vignette && s.hide_hud);
    assert(s.bloom_scale == 1.35f && s.color_saturation == 1.15f);
    assert(s.ao_strength == 1.50f && s.shadow_distance == 2.00f && s.fog_opacity == 0.75f);
    assert(s.fov_scale == 1.20f && s.cam_distance == 1.40f && s.cam_height == 1.10f);
    assert(saved.find("menu_language=") == std::string::npos); // only started with it (#134)

    // A language chosen in the menu is saved and wins over the launcher's from then on.
    s.menu_language = static_cast<int>(BbSettings::MenuLanguage::Russian);
    s.menu_language_chosen = true;
    BbSettings::Save();
    assert(Read(path).find("menu_language=ru\n") != std::string::npos);
    setenv("BB_MENU_LANGUAGE", "en", 1);
    s.menu_language_chosen = false;
    BbSettings::Load();
    assert(s.menu_language.load() == static_cast<int>(BbSettings::MenuLanguage::Russian) && s.menu_language_chosen);
    unlink(path);
    std::puts("PASS: settings save keeps other keys, menu position, live graphics/camera/mouse settings and menu language");
}
