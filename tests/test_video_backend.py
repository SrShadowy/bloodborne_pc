"""run.sh: the window backend decision (BB_VIDEO_BACKEND — docs/Hyprland_compat_plan_2026-10-20.md).

The game window is SDL3 + Vulkan, so Steam sessions on Wayland compositors (Hyprland) need a
choice the report can trace: auto (X11/XWayland under Steam), x11 or wayland, from the
environment or a video_backend= line in bbport.ini.
"""
from paths import ROOT
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest


class VideoBackendTests(unittest.TestCase):
    def run_once(self, env_extra=None, ini_extra='', steam=False):
        with tempfile.TemporaryDirectory() as directory:
            data = Path(directory)
            out = data / 'out'
            out.mkdir()
            # The same single-ELF stand-in of test_run_settings (patches.py really runs).
            elf = bytearray(120)
            struct.pack_into('<Q', elf, 0x20, 64)
            struct.pack_into('<HH', elf, 0x36, 56, 1)
            struct.pack_into('<IIQQQQQQ', elf, 64, 1, 0, 0, 0, 0, 0, 0x6000000, 0)
            (out / 'eboot.elf').write_bytes(elf)
            (data / 'eboot.bin').touch()
            config = data / 'bbport.ini'
            config.write_text('upscaler=fsr3\npreset=4\n' + ini_extra)
            python = data / 'python'
            python.write_text(f'#!{sys.executable}\n'
                              'import subprocess, sys\n'
                              'if sys.argv[1] in ("scripts/patches.py", "scripts/mods.py"):\n'
                              '    sys.exit(subprocess.call([sys.executable, *sys.argv[1:]]))\n')
            python.chmod(0o755)
            probe = data / 'probe'
            probe.write_text(f'#!{sys.executable}\n'
                             'import json, os\n'
                             'from pathlib import Path\n'
                             'keys=("BB_VIDEO_BACKEND", "SDL_VIDEODRIVER", "SDL_VIDEO_DRIVER")\n'
                             'with (Path(os.environ["BB_CONFIG"]).parent/"environments").open("a") as f:\n'
                             '    f.write(json.dumps({key: os.environ.get(key) for key in keys})+"\\n")\n')
            probe.chmod(0o755)
            env = dict(os.environ, BB_PREBUILT='1', BB_PROBE=str(probe), PYTHON=str(python),
                       BB_DATA_DIR=str(data), BB_CONFIG=str(config), BB_GAME_DIR=str(data))
            if not steam:
                # Not under Steam by default, whatever the host shell has.
                env = {key: value for key, value in env.items()
                       if key not in ('SteamOverlayGameId', 'STEAM_RUNTIME',
                                      'SteamGameId', 'SteamAppId')}
            else:
                env['STEAM_RUNTIME'] = '/steam'
                env['SteamAppId'] = '775550'
            env.pop('BB_VIDEO_BACKEND', None)
            env.pop('SDL_VIDEODRIVER', None)
            env.pop('SDL_VIDEO_DRIVER', None)
            if env_extra:
                env.update(env_extra)
            result = subprocess.run([shutil.which('bash'), 'run.sh'],
                                    cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, check=True, timeout=60, text=True)
            rows = [json.loads(line) for line in (data / 'environments').read_text().splitlines()]
            self.assertEqual(len(rows), 1)
            return rows[0], result.stdout

    def test_auto_without_steam_leaves_the_choice_to_sdl(self):
        row, output = self.run_once()
        self.assertEqual(row['BB_VIDEO_BACKEND'], 'auto')
        self.assertIsNone(row['SDL_VIDEODRIVER'])
        self.assertIsNone(row['SDL_VIDEO_DRIVER'])
        self.assertIn('Video backend: auto (SDL decides)', output)

    def test_auto_under_steam_forces_x11(self):
        row, output = self.run_once(steam=True)
        self.assertEqual(row['BB_VIDEO_BACKEND'], 'auto')
        self.assertEqual(row['SDL_VIDEODRIVER'], 'x11')
        self.assertEqual(row['SDL_VIDEO_DRIVER'], 'x11')
        self.assertIn('Video backend: x11 (XWayland; forced under Steam', output)

    def test_explicit_wayland_beats_steam(self):
        row, output = self.run_once(env_extra={'BB_VIDEO_BACKEND': 'wayland'}, steam=True)
        self.assertEqual(row['BB_VIDEO_BACKEND'], 'wayland')
        self.assertEqual(row['SDL_VIDEODRIVER'], 'wayland')
        self.assertEqual(row['SDL_VIDEO_DRIVER'], 'wayland')
        self.assertIn('Video backend: wayland', output)

    def test_ini_backend_without_the_variable(self):
        row, output = self.run_once(ini_extra='video_backend=x11\n')
        self.assertEqual(row['BB_VIDEO_BACKEND'], 'x11')
        self.assertEqual(row['SDL_VIDEODRIVER'], 'x11')
        self.assertEqual(row['SDL_VIDEO_DRIVER'], 'x11')
        self.assertIn('Video backend: x11 (XWayland on a Wayland session', output)

    def test_environment_beats_the_ini(self):
        row, output = self.run_once(env_extra={'BB_VIDEO_BACKEND': 'x11'},
                                    ini_extra='video_backend=wayland\n')
        self.assertEqual(row['BB_VIDEO_BACKEND'], 'x11')
        self.assertEqual(row['SDL_VIDEODRIVER'], 'x11')
        self.assertEqual(row['SDL_VIDEO_DRIVER'], 'x11')

    def test_unknown_ini_backend_is_ignored(self):
        row, output = self.run_once(ini_extra='video_backend=vulkan\n')
        self.assertEqual(row['BB_VIDEO_BACKEND'], 'auto')
        self.assertIsNone(row['SDL_VIDEODRIVER'])
        self.assertIn('Video backend: auto (SDL decides)', output)


if __name__ == '__main__':
    unittest.main()
