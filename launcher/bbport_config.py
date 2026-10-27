# SPDX-License-Identifier: GPL-2.0-or-later
"""Configuration and settings manager for the Bloodborne launcher.

Handles bbport.ini and settings.json persistence, default options,
path resolution, and game launch environment generation.
"""

import json
import os
import shutil
import sys
from pathlib import Path

from bbport_i18n import language

if "BB_PORT_DIR" in os.environ:
    PORT_DIR = Path(os.environ["BB_PORT_DIR"]).resolve()
elif getattr(sys, "frozen", False):
    exe_path = Path(sys.executable).resolve()
    PORT_DIR = exe_path.parent.parent if exe_path.parent.name in ("launcher", "bin") else exe_path.parent
else:
    PORT_DIR = Path(__file__).resolve().parent.parent  # native_probe (or the package's copy)
sys.path.insert(0, str(PORT_DIR / "scripts"))
sys.path.insert(0, str(PORT_DIR / "tools"))
import online

import bbport_bbhost as bbhost  # the Hunter's Dream account client (protocol only)

# Packaged (AppImage): generated files, saves and bbport.ini live in BB_DATA_DIR.
PACKAGED = bool(os.environ.get("BB_PREBUILT"))
DATA_DIR = Path(os.environ.get("BB_DATA_DIR", PORT_DIR))
CONFIG_DIR = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "bbport-launcher"
CONFIG_FILE = CONFIG_DIR / "settings.json"
ACCOUNT_FILE = CONFIG_DIR / "account.json"  # the Hunter's Dream sign-in (bbport_bbhost)
MAX_LOG_LINES = 5000

def get_launcher_version():
    """Resolves the build/release version string for display in UI."""
    for cand in [PORT_DIR / "VERSION", DATA_DIR / "VERSION", Path(__file__).resolve().parent / "VERSION"]:
        if cand.is_file():
            try:
                v = cand.read_text(encoding="utf-8").strip()
                if v:
                    return v
            except OSError:
                pass
    if os.environ.get("BB_VERSION"):
        return os.environ["BB_VERSION"].strip()
    try:
        import subprocess
        res = subprocess.run(
            ["git", "describe", "--tags", "--always"],
            cwd=PORT_DIR,
            capture_output=True,
            text=True,
            timeout=2
        )
        if res.returncode == 0:
            ver = res.stdout.strip()
            if ver.startswith("v"):
                ver = ver[1:]
            return ver
    except Exception:
        pass
    return "0.6.5-dev-24"

BB_VERSION = get_launcher_version()


# The memory model (run.sh: BB_PC_MODEL): "auto" leaves it to the GPU (the new one on AMD, the
# 0.3 one elsewhere), or either by hand. The new one goes through the layer's memory module on
# every GPU (no sparse binding of the game's memory; BB_LAYER_MEMORY=0: AMD's sparse arena).
PC_MODEL_SUBTITLE = (
    "Новая: видеокарта работает с памятью игры напрямую, как в игре для ПК, а команды "
    "графики переводятся, а не эмулируются. Старая — модель памяти 0.3 со всеми "
    "исправлениями. Если драйвер не проходит проверку при запуске — старая"
)
MEMORY_MODELS = [("Авто: новая на любой видеокарте", "auto"), ("Новая", "new"),
                 ("Старая (как в 0.3)", "old")]

# The package's own environment must not reach Proton's container
PACKAGE_ONLY_ENV = (
    "LD_LIBRARY_PATH", "VK_DRIVER_FILES", "VK_ICD_FILENAMES", "VK_ADD_DRIVER_FILES",
    "VK_LAYER_PATH", "VK_ADD_LAYER_PATH", "__EGL_VENDOR_LIBRARY_DIRS", "GDK_PIXBUF_MODULE_FILE",
    "GIO_EXTRA_MODULES", "GI_TYPELIB_PATH", "FONTCONFIG_FILE", "GSETTINGS_SCHEMA_DIR", "PYTHONPATH",
    "PYTHONHOME",
)

# Choices: (label, value). The first entry is the default. Labels are translated when shown.
UI_LANGUAGES = [("Как в системе", ""), ("Português (Brasil)", "pt_BR"), ("English", "en"), ("Русский", "ru"), ("简体中文", "zh_CN")]
UPSCALERS = [("FSR 4", "fsr4"), ("FSR 4.1.1", "fsr411"), ("FSR 3", "fsr3"),
             ("TAA (нативное сглаживание)", "taa"), ("DLSS (NVIDIA RTX)", "dlss"), ("Выключен", "off")]
PRESETS = [("Native AA", 0), ("Quality (x1.5)", 1), ("Balanced (x1.7)", 2),
           ("Performance (x2)", 3), ("Ultra Performance (x3)", 4)]
PRESET_SCALES = [1.0, 1.5, 1.7, 2.0, 3.0]
ASPECTS = ["auto", "4:3", "16:10", "16:9", "21:9", "32:9"]
ASPECT_RATIOS = {"4:3": 4 / 3, "16:10": 16 / 10, "16:9": 16 / 9, "21:9": 64 / 27, "32:9": 32 / 9}
OUTPUTS = {
    "4:3": [(640, 480), (800, 600), (1024, 768), (1280, 960), (1440, 1080), (1600, 1200), (2048, 1536),
            (2880, 2160)],
    "16:10": [(640, 400), (960, 600), (1280, 800), (1440, 900), (1680, 1050), (1920, 1200), (2560, 1600),
              (3840, 2400)],
    "16:9": [(640, 360), (854, 480), (960, 540), (1280, 720), (1600, 900), (1920, 1080), (2560, 1440),
             (3200, 1800), (3840, 2160)],
    "21:9": [(860, 360), (1280, 540), (1720, 720), (2560, 1080), (3440, 1440), (3840, 1600), (5120, 2160)],
    "32:9": [(1280, 360), (1920, 540), (2560, 720), (3840, 1080), (5120, 1440), (7680, 2160)],
}


def parse_size(text, default=(1920, 1080)):
    try:
        w, h = (int(v) for v in text.lower().split("x"))
        return (w, h) if w > 0 and h > 0 else default
    except (ValueError, AttributeError):
        return default


def resolved_aspect(aspect, monitor):
    """The aspect ratio's name, 'auto' resolved to the group closest to the monitor's aspect ratio."""
    if aspect != "auto" or not monitor:
        return aspect
    ratio = monitor[0] / monitor[1]
    return min(ASPECT_RATIOS.keys(), key=lambda key: abs(ASPECT_RATIOS[key] - ratio))


def output_sizes(aspect, monitor):
    """(w, h) of the aspect ratio's output resolutions, the monitor's own included when it fits."""
    group = resolved_aspect(aspect, monitor)
    sizes = OUTPUTS.get(group, OUTPUTS["16:9"])
    if (monitor and group != "auto"
            and abs(monitor[0] / monitor[1] - ASPECT_RATIOS[group]) < 0.05
            and monitor not in sizes
            and 360 <= monitor[1] <= 4320):
        sizes = sorted(sizes + [monitor], key=lambda size: (size[1], size[0]))
    return sizes


def nearest_output(sizes, size):
    """The size itself if listed, else the one nearest in height (another aspect ratio keeps it)."""
    return size if size in sizes else min(sizes, key=lambda s: abs(s[1] - size[1]))


OUTPUT_RES = [
    ("960×540 (16:9 qHD)", "960x540"),
    ("1280×720 (16:9 HD)", "1280x720"),
    ("1600×900 (16:9 HD+)", "1600x900"),
    ("1920×1080 (16:9 Full HD)", "1920x1080"),
    ("2560×1440 (16:9 QHD)", "2560x1440"),
    ("3840×2160 (16:9 4K UHD)", "3840x2160"),
    ("1280×800 (16:10 Steam Deck)", "1280x800"),
    ("1920×1200 (16:10 WUXGA)", "1920x1200"),
    ("2560×1600 (16:10 WQXGA)", "2560x1600"),
    ("2560×1080 (21:9 Ultrawide)", "2560x1080"),
    ("3440×1440 (21:9 UW-QHD)", "3440x1440"),
    ("5120×2160 (21:9 5K2K)", "5120x2160"),
    ("3840×1080 (32:9 Super UW)", "3840x1080"),
    ("5120×1440 (32:9 Super UW)", "5120x1440"),
    ("1280×960 (4:3)", "1280x960"),
    ("1440×1080 (4:3)", "1440x1080"),
]

# Game effects (patches applied at start): bbport.ini key, title, default.
EFFECTS = [
    ("effect_chromatic_aberration", "Хроматическая аберрация", True),
    ("effect_dof", "Глубина резкости (DoF)", True),
    ("effect_motion_blur", "Размытие в движении", True),
    ("effect_ssao", "Затенение SSAO", True),
    ("effect_game_aa", "Собственное сглаживание игры", True),
    ("effect_dynamic_shadows", "Тени от динамических источников", True),
    ("effect_ssr", "Отражения SSR (не было в игре)", False),
    ("effect_bloom", "Свечение (Bloom)", True),
    ("puddle_reflections", "Отражения в лужах", False),
    ("skip_intro", "Пропуск заставок при запуске", True),
    ("debug_camera", "Свободная камера (Cross + L3 / F + Ctrl)", False),
    ("debug_menu", "Debug menu (левый touchpad / Tab; нужны шрифты)", False),
]
MODEL_LOD = [("Как в игре", "0"), ("Максимальная (-2)", "-2"), ("Ниже (1)", "1"),
             ("Минимальная (2)", "2")]
FPS_MODES = [("Без ограничения (патч)", "uncap"), ("60", "60"), ("90", "90"),
             ("30 (как на PS4)", "30")]
PRESENT_MODES = [("Mailbox", "Mailbox"), ("FIFO (VSync)", "Fifo"),
                 ("FIFO Relaxed", "FifoRelaxed"), ("Immediate", "Immediate")]

DRAW_PIPE = [("Гибридный (Рекомендуется)", "2", "Ускорение в игре, стабильность в кат-сценах"),
             ("Включён", "1"),
             ("Выключен", "0", "Стабильнее, стандартный режим")]
LANGUAGES = [("Английский", "1"), ("Английский (Великобритания)", "18"), ("Русский", "8"),
             ("Японский", "0"), ("Французский", "2"), ("Испанский", "3"),
             ("Испанский (Латинская Америка)", "20"), ("Немецкий", "4"), ("Итальянский", "5"),
             ("Нидерландский", "6"), ("Португальский", "7"), ("Португальский (Бразилия)", "17"),
             ("Польский", "16"), ("Турецкий", "19"), ("Корейский", "9"),
             ("Китайский (упрощённый)", "11"), ("Китайский (традиционный)", "10"),
             ("Финский", "12"), ("Шведский", "13"), ("Датский", "14"), ("Норвежский", "15")]
LIVE_RESOLUTION = [("Авто (по видеокарте)", "auto"), ("Выключена (быстрее)", "0"), ("Включена", "1")]

# The game window's backend (SDL3 + Vulkan): auto keeps the run.sh default (X11/XWayland under
# Steam, the compositor's Wayland otherwise); x11/wayland force it. The launcher's own GTK uses
# GDK_BACKEND, not this.
VIDEO_BACKENDS = [
    ("Авто", "auto", "Под Steam: X11 (XWayland) для Overlay и Steam Input; вне его — Wayland"),
    ("X11 (XWayland)", "x11", "Когда игра не открывает окно в нативном Wayland"),
    ("Wayland", "wayland", "Нативный бэкенд композитора, без XWayland"),
]
READBACKS = [("Relaxed", "", "По умолчанию"), ("Выключены", "0", "Быстрее, но лица могут искажаться"),
             ("Precise", "2", "Экспериментально; может зависать при запуске")]
PREUPLOAD = [("Обычная", "", "Без лишней видеопамяти"), ("Полная", "2", "Около 3 ГБ видеопамяти сверху"),
             ("Выключена", "0")]

VK_RECORD_THREADS = [("Авто", "auto"), ("1", "1"), ("2", "2"), ("3", "3"), ("4", "4"), ("5", "5")]

MEMORY_PRESETS = [
    ("Высокая (8+ ГБ / Рекомендуемая)", "high", "Для видеокарт с 8 ГБ VRAM и выше: максимальное качество"),
    ("Средняя (6 ГБ / Сбалансированная)", "medium", "Для видеокарт с 6 ГБ VRAM: умеренное освобождение VRAM в RAM (GC 10с)"),
    ("Низкая (4 ГБ / Для слабых ПК)", "low", "Агрессивный сброс в RAM, GC 4с, для видеокарт с 4 ГБ VRAM"),
    ("APU / 2 ГБ (Встроенная графика / 30 FPS)", "apu", "Лимит 2 ГБ, Auto Mip-Clamp, выгрузка в RAM, для APU (Radeon 610M)"),
]

LOG_LEVELS = [
    ("Минимальный (запуск и сбои)", "minimal"),
    ("Только ошибки", "error"),
    ("Предупреждения и ошибки", "warning"),
    ("Подробный (все журналы)", "verbose"),
    ("Без журнала (только сбои)", "none"),
]

DEFAULTS = {
    "ui_language": "",
    "log_level": "verbose",
    "game_dir": "" if PACKAGED else str(PORT_DIR.parent / "CUSA03173"),
    "user_dir": "",
    "mods_dir": "",
    "mods_enabled": True,
    "patches_dir": "",
    "language": "1",
    "fullscreen": False,
    "hdr": False,
    "present_mode": "Mailbox",
    "gamepad": "",
    "gamepad_name": "",
    "display": "",
    "skip_network_choice": True,
    "fps_mode": "uncap",
    "fps_limit": 0,
    "draw_pipe": "2",
    "vk_record_threads": "auto",
    "readbacks": "",
    "preupload": "",
    "mangohud": False,
    "frame_stats": False,
    "save_log": False,
    "crash_diag": False,
    "memory_model": "auto",
    "memory_preset": "high",
    "zero_copy": True,
    "as_0_3": False,
    "gpu_profile": False,
    "vk_validation": False,
    "extra_env": "",
    # The GPU by its Vulkan name ("" = the port's choice: a discrete one first).
    "gpu": "",
    # Online play (Online page, the online module gpu/bbnet20): off by default.
    "online": False,
    "online_bbsc": True,
    "online_net_gen": "19",
    "online_community": online.COMMUNITY_SERVER,
    "online_server": online.SHADNET_SERVER,
    "online_webapi": online.WEBAPI_SERVER,
    "online_npid": "",
    "online_password": "",
    "online_upnp": True,
    "p2p_relay": True,
    # Online pool: "seamless" (Seamless Hunt + BBSC), "huntersdream" (The Hunter's Dream, bbhost), or "legacy"
    "online_pool": "seamless",
    "thd_server": "",
    "thd_online_id": "",
    "thd_token": "",
    # Online service (Online page backwards compat):
    "online_backend": "shadnet",
    "online_bbhost_server": bbhost.DEFAULT_SERVER,
    "aspect": "auto",
    # The game window's SDL backend: auto (run.sh decides), x11 or wayland.
    "video_backend": "auto",
    "theme_mode": "system",
    "discord_client_id": "1560890616811102219",
}

# bbport.ini keys the launcher edits; the rest of the file is kept.
INI_DEFAULTS = {
    "discord_client_id": "1560890616811102219",
    "upscaler": "fsr4",
    "preset": "4",
    "sharpen": "1",
    "sharpness": "0.50",
    "object_motion": "1",
    "frame_generation": "0",
    "show_fps": "1",
    "aspect": "auto",
    "output_res": "1920x1080",
    "model_lod": "0",
    "live_resolution": "auto",
    "mouse_look": "1",
    "mouse_sensitivity": "1.00",
    "mouse_invert_y": "0",
    "mouse_invert_x": "0",
    "mouse_capture": "1",
    "disable_gyro": "0",
    "draw_pipe": "2",
    "fps_limit": "0",
    "video_backend": "auto",
    "p2p_relay": "1",
    **{key: "1" if default else "0" for key, _, default in EFFECTS},
}

# Controls (runtime_pad.c): input, label, default keyboard keys, default gamepad buttons, default mouse buttons.
CONTROLS = [
    ("cross", "Крест", "Space", "a", None),
    ("circle", "Круг", "Left Shift", "b", None),
    ("square", "Квадрат", "E", "x", None),
    ("triangle", "Треугольник", "Q", "y", None),
    ("l1", "L1", "1", "leftshoulder", None),
    ("r1", "R1", "3", "rightshoulder", "left"),
    ("l2", "L2", "R", "lefttrigger", "right"),
    ("r2", "R2", "F", "righttrigger", None),
    ("l3", "L3", "Z", "leftstick", None),
    ("r3", "R3", "C", "rightstick", "middle"),
    ("options", "Options", "Escape", "start", None),
    ("touchpad", "Тачпад, левая половина (жесты)", "Tab", "touchpad", None),
    ("touchpad_right", "Тачпад, правая половина (личные вещи)", "Backspace", "", None),
    ("up", "Крестовина вверх", "I", "dpup", None),
    ("down", "Крестовина вниз", "K", "dpdown", None),
    ("left", "Крестовина влево", "J", "dpleft", None),
    ("right", "Крестовина вправо", "L", "dpright", None),
    ("move_up", "Движение вперёд", "W", None, None),
    ("move_down", "Движение назад", "S", None, None),
    ("move_left", "Движение влево", "A", None, None),
    ("move_right", "Движение вправо", "D", None, None),
    ("look_up", "Камера вверх", "Up", None, "motion_up"),
    ("look_down", "Камера вниз", "Down", None, "motion_down"),
    ("look_left", "Камера влево", "Left", None, "motion_left"),
    ("look_right", "Камера вправо", "Right", None, "motion_right"),
]


def load_settings():
    settings = dict(DEFAULTS)
    loaded = False
    try:
        settings.update(json.loads(CONFIG_FILE.read_text(encoding="utf-8")))
        loaded = True
    except (OSError, ValueError):
        pass

    if not loaded:
        bak_file = CONFIG_FILE.with_suffix(".json.bak")
        if bak_file.is_file():
            try:
                settings.update(json.loads(bak_file.read_text(encoding="utf-8")))
            except (OSError, ValueError):
                pass

    for key, old_values in getattr(online, "OLD_DEFAULTS", {}).items():
        if str(settings.get(key, "")).strip() in old_values:
            settings[key] = DEFAULTS.get(key, "")
    return settings


def save_settings(settings):
    """Saves settings atomically with .bak preservation and restricted permissions."""
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    tmp_file = CONFIG_DIR / f".settings.json.tmp.{os.getpid()}"
    bak_file = CONFIG_FILE.with_suffix(".json.bak")
    data = json.dumps(settings, indent=2, ensure_ascii=False)

    try:
        tmp_file.write_text(data, encoding="utf-8")
        try:
            tmp_file.chmod(0o600)  # holds sensitive credentials (shadNet password)
        except OSError:
            pass

        # Preserve previous valid config as backup
        if CONFIG_FILE.is_file():
            try:
                shutil.copy2(CONFIG_FILE, bak_file)
            except OSError:
                pass

        # Atomic rename on POSIX filesystems
        os.replace(tmp_file, CONFIG_FILE)
    except Exception as e:
        if tmp_file.exists():
            try:
                tmp_file.unlink()
            except OSError:
                pass
        raise e


# DLSS (vk_dlss.cpp): the bridge (libbbport_dlss.so) and NVIDIA's runtime library
# libnvidia-ngx-dlss.so.<version>, beside bb-probe (the package, a build with DLSS_SDK_ROOT) or the
# player's own in <user>/dlss, which the game prefers. Linux needs the .so from NVIDIA's DLSS SDK
# (github.com/NVIDIA/DLSS, lib/Linux_x86_64/rel); a game's nvngx_dlss.dll is Windows-only.
NGX_PREFIX = "libnvidia-ngx-dlss.so."
DLSS_SDK_URL = "https://github.com/NVIDIA/DLSS/tree/main/lib/Linux_x86_64/rel"


def binaries_dir():
    return PORT_DIR / ("bin" if PACKAGED else "out")


def ngx_libraries(directory):
    """NVIDIA's DLSS runtime libraries in directory, newest version last."""
    def version(path):
        return tuple(int(p) if p.isdigit() else 0 for p in path.name[len(NGX_PREFIX):].split("."))
    try:
        return sorted((p for p in Path(directory).iterdir() if p.name.startswith(NGX_PREFIX)), key=version)
    except OSError:
        return []


def online_module(pool="seamless"):
    """The online module beside the GPU library (gpu/bbnet, or gpu/bbnet20 for shadNet / The
    Hunter's Dream pools; a build without its libraries has none)."""
    base = PORT_DIR / ("bin" if PACKAGED else "out") / "gpu"
    name = "libbbnet.so" if pool == "legacy" else "libbbnet20.so"
    return (base / name).is_file() or (base / "libbbnet.so").is_file() or (base / "libbbnet20.so").is_file()


def ini_path():
    return Path(os.environ.get("BB_CONFIG", DATA_DIR / "bbport.ini"))


def load_ini():
    values = dict(INI_DEFAULTS)
    lines = []
    try:
        lines = ini_path().read_text().splitlines()
    except OSError:
        pass
    for line in lines:
        if "=" in line and not line.lstrip().startswith("#"):
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values, lines


def save_ini(values, lines):
    """Rewrites edited keys in place, appends missing ones, preserves comments."""
    written = set()
    out = []
    for line in lines:
        if "=" in line and not line.lstrip().startswith("#"):
            key = line.split("=", 1)[0].strip()
            if key in values:
                if key not in written:
                    if values[key] is not None:
                        out.append(f"{key}={values[key]}")
                    written.add(key)
                continue
        out.append(line)
    if not lines:
        out.append("# bbport settings (in-game menu: Insert / L3+R3)")
    for key, value in values.items():
        if key not in written and value is not None:
            out.append(f"{key}={value}")
    ini_path().write_text("\n".join(out) + "\n")


def patches_dir(settings):
    return Path(settings.get("patches_dir") or DATA_DIR / "patches").expanduser()


def game_environment(s):
    """Environment for run.sh from the launcher settings."""
    env = dict(os.environ)
    lib_dir = PORT_DIR / "lib"
    bin_gpu = PORT_DIR / "bin" / "gpu"
    if lib_dir.is_dir():
        prefix = f"{lib_dir}:{bin_gpu}"
        env["LD_LIBRARY_PATH"] = f"{prefix}:{env['LD_LIBRARY_PATH']}" if env.get("LD_LIBRARY_PATH") else prefix
    elif bin_gpu.is_dir():
        env["LD_LIBRARY_PATH"] = f"{bin_gpu}:{env['LD_LIBRARY_PATH']}" if env.get("LD_LIBRARY_PATH") else str(bin_gpu)
    env["BB_GAME_DIR"] = str(Path(s["game_dir"]).expanduser())
    if s["user_dir"]:
        env["BB_USER_DIR"] = s["user_dir"]
    env["BB_MODS_DIR"] = str(Path(s.get("mods_dir") or DATA_DIR / "mods").expanduser())
    env["BB_MODS_CONFIG"] = str(DATA_DIR / "mods.json")
    env["BB_MODS_ENABLED"] = "1" if s.get("mods_enabled", True) else "0"
    env["BB_PATCHES_DIR"] = str(patches_dir(s))
    env["BB_PATCHES_CONFIG"] = str(DATA_DIR / "patches.json")
    env["BB_LANGUAGE"] = s["language"]
    env["BB_MENU_LANGUAGE"] = language()
    env["BB_FULLSCREEN"] = "1" if s["fullscreen"] else "0"
    if "BB_VIDEO_BACKEND" not in env:
        env["BB_VIDEO_BACKEND"] = s.get("video_backend", "auto")
    env["BB_PRESENT_MODE"] = s["present_mode"]
    if s.get("gamepad"):
        env["BB_GAMEPAD"] = s["gamepad"]
    if s.get("disable_gyro") == "1":
        env["BB_DISABLE_GYRO"] = "1"
    env["SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"] = "1"
    env["SDL_JOYSTICK_HIDAPI"] = "1"
    env["SDL_JOYSTICK_HIDAPI_PS4"] = "1"
    env["SDL_JOYSTICK_HIDAPI_PS5"] = "1"
    env["SDL_JOYSTICK_ENHANCED_REPORTS"] = "1"
    if s.get("display"):
        env["BB_DISPLAY"] = s["display"]
    env["BB_SKIP_NETWORK_CHOICE"] = "1" if s.get("skip_network_choice", True) else "0"
    if s["hdr"]:
        env["BB_HDR"] = "1"
    env["BB_FPS"] = s["fps_mode"]
    env["BB_FPS_LIMIT"] = str(s.get("fps_limit", 0))
    if s["draw_pipe"]:
        env["BB_DRAW_PIPE"] = s["draw_pipe"]
    if s.get("vk_record_threads") and s["vk_record_threads"] != "auto":
        env["BB_VK_RECORD_THREADS"] = s["vk_record_threads"]
    if s["readbacks"]:
        env["BB_READBACKS"] = s["readbacks"]
    if s.get("preupload"):
        env["BB_PREUPLOAD"] = s["preupload"]
    if s["mangohud"]:
        env["MANGOHUD"] = "1"
    if s["frame_stats"]:
        env["BB_FRAME_STATS"] = "1"
    if s.get("save_log"):
        env["BB_SAVE_LOG"] = "1"
    p2p_relay_enabled = s.get("p2p_relay", True)
    if p2p_relay_enabled is False or str(p2p_relay_enabled).strip() in ("0", "false", "False", "off"):
        env["SHADPS4_P2P_RELAY"] = "0"
        env["BB_P2P_RELAY"] = "0"
    else:
        env["BB_P2P_RELAY"] = "1"
    log_level = s.get("log_level", "verbose")
    env["BB_LOG_LEVEL"] = log_level
    if "BB_GPU_LOG" not in env:
        if log_level == "verbose":
            env["BB_GPU_LOG"] = "debug"
        elif log_level == "warning":
            env["BB_GPU_LOG"] = "warning"
        elif log_level == "error":
            env["BB_GPU_LOG"] = "error"
        elif log_level == "minimal":
            env["BB_GPU_LOG"] = "error"
        elif log_level == "none":
            env["BB_GPU_LOG"] = "off"
    model = s.get("memory_model", "auto")
    if model in ("new", "old"):
        env["BB_PC_MODEL"] = "1" if model == "new" else "0"
    mem_preset = s.get("memory_preset", "high")
    if mem_preset in ("low", "medium", "high", "apu"):
        env["BB_MEMORY_PRESET"] = mem_preset
        if mem_preset == "apu":
            env["BB_VRAM_LIMIT_MB"] = "2048"
            env["BB_AUTO_MIP_CLAMP"] = "1"
            env["BB_GUEST_GPU_MEMORY"] = "1"
            env["BB_CHUNK_UPLOADS"] = "1"
            env["BB_CHUNK_TEXTURES"] = "1"
            env.setdefault("BB_FRAME_GEN", "0")
            env.setdefault("BB_FPS_LIMIT", "30")
            env.setdefault("BB_FPS", "30")
            env.setdefault("BB_UPSCALE_PRESET", "3")
            env.setdefault("BB_SHADOW_DISTANCE", "0.4")
            env.setdefault("BB_LIGHT_GRID_OPT", "1")
            env.setdefault("BB_EFFECT_SSR", "0")
            env.setdefault("BB_EFFECT_SSAO", "0")
            env.setdefault("BB_EFFECT_DOF", "0")
            env.setdefault("BB_EFFECT_MOTION_BLUR", "0")
            env.setdefault("BB_EFFECT_BLOOM", "0")
        elif mem_preset == "low":
            if "BB_AUTO_MIP_CLAMP" not in env:
                env["BB_AUTO_MIP_CLAMP"] = "1"
    if s.get("zero_copy", True):
        env.setdefault("BB_GUEST_GPU_MEMORY", "1")
        env.setdefault("BB_CHUNK_UPLOADS", "1")
        env.setdefault("BB_CHUNK_TEXTURES", "1")
    # Link internal resolution scale preset to BB_RENDER_RES when upscaler is off
    if "BB_RENDER_RES" not in env:
        ini_file = PORT_DIR / "bbport.ini"
        upscaler_mode = "off"
        preset_idx = 0
        output_res_str = s.get("output_res", "1920x1080")
        if ini_file.is_file():
            try:
                for line in ini_file.read_text().splitlines():
                    if line.startswith("upscaler="):
                        upscaler_mode = line.split("=", 1)[1].strip()
                    elif line.startswith("preset="):
                        preset_idx = int(line.split("=", 1)[1].strip() or 0)
                    elif line.startswith("output_res="):
                        output_res_str = line.split("=", 1)[1].strip()
            except Exception:
                pass
        if upscaler_mode == "off" and preset_idx > 0:
            out_w, out_h = parse_size(output_res_str, (1920, 1080))
            if 0 <= preset_idx < len(PRESET_SCALES):
                scale = PRESET_SCALES[preset_idx]
                if scale > 1.0:
                    rw = max(2, round(out_w / scale / 2) * 2)
                    rh = max(2, round(out_h / scale / 2) * 2)
                    env["BB_RENDER_RES"] = f"{rw}x{rh}"
    # The GPU chosen by name: its index in Vulkan's order now (BB_GPU_ID). One not found (taken
    # out, renamed by a driver update) leaves the choice to the port. BB_GPU_ID from the shell
    # wins.
    if s.get("gpu") and "BB_GPU_ID" not in os.environ:
        from bbport_devices import connected_gpus
        index = next((index for index, _vendor, _kind, name, _best in connected_gpus()
                      if name == s["gpu"]), None)
        if index is not None:
            env["BB_GPU_ID"] = str(index)
    if s.get("as_0_3"):
        env["BB_AS_0_3"] = "1"
    if s.get("crash_diag"):
        env["BB_FREE_CHECK"] = "1"
        env["BB_WRITE_LOG"] = "1"
        env["BB_PRODUCER_CHECK"] = "1"
    if s["gpu_profile"]:
        env["BB_GPU_PROFILE"] = "1"
    if s["vk_validation"]:
        env["BB_VK_VALIDATION"] = "1"
    pool = "huntersdream" if s.get("online_pool") == "huntersdream" or s.get("online_backend") in ("bbhost", "hosts", "thehuntersdream", "hd") else "seamless"
    try:
        has_online_mod = online_module(pool)
    except TypeError:
        has_online_mod = online_module()
    if s.get("online") and has_online_mod:
        server = s["online_server"].strip() or online.SHADNET_SERVER
        community = online.community_address(s["online_community"])
        env["BB_ONLINE"] = "1"
        env["BB_SHADNET_SERVER"] = server
        env["BB_SHADNET_WEBAPI"] = online.webapi_setting(s["online_webapi"], server, community)
        env["BB_SHADNET_NPID"] = s["online_npid"].strip()
        env["BB_SHADNET_PASSWORD"] = s["online_password"]
        env["BB_UPNP"] = "1" if s["online_upnp"] else "0"
        env["BB_COMMUNITY_SERVER"] = community
        if "BB_NET_GEN" not in env:
            env["BB_NET_GEN"] = str(s.get("online_net_gen") or "19")
        backend = "bbhost" if (pool == "huntersdream" or s.get("online_backend") in ("bbhost", "hosts", "thehuntersdream", "hd")) else "shadnet"
        env["BB_ONLINE_BACKEND"] = backend
        if pool == "seamless":
            env["BB_BBSC"] = "1" if s.get("online_bbsc", True) else "0"
        if pool == "huntersdream" or backend == "bbhost":
            # run.sh turns these into the backend's addresses and the host overrides.
            env["BB_NET_BACKEND"] = "thd"
            server_val = s.get("thd_server", "") or s.get("online_bbhost_server", "")
            env["BB_COMMUNITY_SERVER"] = online.thd_base(server_val)
            thd_id = s.get("thd_online_id", "")
            thd_token = s.get("thd_token", "")
            if not thd_id or not thd_token:
                try:
                    import bbport_hosts as hosts
                    thd_id, thd_token = hosts.load_account(ACCOUNT_FILE)
                except Exception:
                    pass
            env["BB_THD_ONLINE_ID"] = thd_id
            env["BB_THD_TOKEN"] = thd_token
            try:
                import bbport_hosts as hosts
                base = hosts.server_base(server_val)
            except Exception:
                base = "https://thehuntersdream.com:18671"
            scheme, _sep, host = base.partition("://")
            env["BB_HOST_SCHEME"] = scheme
            env["BB_HOST_SERVER"] = host
            if thd_id and thd_token:
                env["BB_HOST_ONLINE_ID"] = thd_id
                env["BB_HOST_TOKEN"] = thd_token
    for item in s["extra_env"].split():
        if "=" in item:
            key, value = item.split("=", 1)
            env[key] = value
            k_lower = key.lower()
            if k_lower in ("bb_vram_limit", "bb_vram_limited", "vram_limit", "vram_limited"):
                env["BB_VRAM_LIMIT"] = value
            elif k_lower in ("bb_auto_mip_clamp", "auto_mip_clamp"):
                env["BB_AUTO_MIP_CLAMP"] = value
    return env
