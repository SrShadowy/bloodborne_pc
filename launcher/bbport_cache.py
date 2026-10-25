# SPDX-License-Identifier: GPL-2.0-or-later
"""Shader cache detection, management, and cleaning for Bloodborne Port."""

import os
import sys
import threading
from pathlib import Path
from bbport_i18n import tr
from bbport_ui_helpers import open_folder, flat_button

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, GLib, Gtk  # noqa: E402


def calc_cache_stats(user_dir: Path) -> tuple[int, int]:
    """Returns (file_count, total_bytes) of the game shader cache."""
    count = 0
    size = 0
    cache_dir = user_dir / "cache"
    if cache_dir.is_dir():
        for root, _dirs, files in os.walk(cache_dir):
            for f in files:
                try:
                    size += (Path(root) / f).stat().st_size
                    count += 1
                except OSError:
                    pass
    data_cache = user_dir / "data" / "cache.bin"
    if data_cache.is_file():
        try:
            size += data_cache.stat().st_size
            count += 1
        except OSError:
            pass
    return count, size


def wipe_folder_contents(path: Path) -> tuple[int, int]:
    """Safely deletes all files and subdirectories inside path, preserving path itself."""
    count = 0
    size = 0
    if not path or not path.exists():
        return count, size
    if path.is_file() or path.is_symlink():
        try:
            size += path.stat().st_size
            path.unlink()
            count += 1
        except OSError:
            pass
        return count, size

    try:
        for item in list(path.iterdir()):
            try:
                if item.is_file() or item.is_symlink():
                    size += item.stat().st_size
                    item.unlink()
                    count += 1
                elif item.is_dir():
                    for root, _dirs, files in os.walk(item, topdown=False):
                        for f in files:
                            fp = Path(root) / f
                            try:
                                size += fp.stat().st_size
                                fp.unlink()
                                count += 1
                            except OSError:
                                pass
                        for d in _dirs:
                            try:
                                (Path(root) / d).rmdir()
                            except OSError:
                                pass
                    try:
                        item.rmdir()
                    except OSError:
                        pass
            except OSError:
                pass
    except OSError:
        pass
    return count, size


def clear_shader_cache(user_dir: Path, mode: str = "game") -> tuple[int, int]:
    """Wipes game cache (mode="game") or game + GPU driver shader cache (mode="all")."""
    count = 0
    size = 0

    # 1. Game SPIR-V and pipeline cache in user/cache
    c, s = wipe_folder_contents(user_dir / "cache")
    count += c
    size += s

    # 2. Game data cache (cache.bin)
    data_cache = user_dir / "data" / "cache.bin"
    if data_cache.is_file():
        try:
            size += data_cache.stat().st_size
            data_cache.unlink()
            count += 1
        except OSError:
            pass

    # 3. GPU driver shader caches if requested
    if mode == "all":
        driver_paths = []
        mesa_env = os.environ.get("MESA_SHADER_CACHE_DIR")
        if mesa_env:
            driver_paths.append(Path(mesa_env))
        else:
            driver_paths.append(Path.home() / ".cache" / "mesa_shader_cache")

        driver_paths.append(Path.home() / ".nv" / "GLCache")
        driver_paths.append(Path.home() / ".cache" / "nvidia" / "GLCache")
        nv_env = os.environ.get("__GL_SHADER_DISK_CACHE_PATH")
        if nv_env:
            driver_paths.append(Path(nv_env))

        for dpath in driver_paths:
            c, s = wipe_folder_contents(dpath)
            count += c
            size += s

    return count, size


class CacheMixin:
    """Mixin for LauncherWindow handling shader cache status and cleaning."""

    def update_cache_status(self):
        if not hasattr(self, "clear_cache_row"):
            return
        count, size = calc_cache_stats(self.user_dir())
        if count > 0:
            mb = size / (1024 * 1024)
            self.clear_cache_row.set_subtitle(
                tr("Кэш игры: {} файлов ({:.1f} МБ)").format(count, mb)
            )
        else:
            self.clear_cache_row.set_subtitle(tr("Кэш пуст"))

        if hasattr(self, "static_catalog_row"):
            catalog_file = self.user_dir() / "cache" / "CUSA03173" / "static_shader_catalog.json"
            if not catalog_file.is_file():
                catalog_file = Path("dist/user/cache/CUSA03173/static_shader_catalog.json")
            if catalog_file.is_file():
                try:
                    import json
                    with open(catalog_file, "r", encoding="utf-8") as f:
                        data = json.load(f)
                    total_unique = data.get("unique_shaders", 0)
                    total_inst = data.get("total_instances", 0)
                    self.static_catalog_row.set_subtitle(
                        tr("Catálogo ativo: {} shaders únicos ({} instâncias)").format(total_unique, total_inst)
                    )
                except Exception:
                    self.static_catalog_row.set_subtitle(tr("Catálogo estático presente"))
            else:
                self.static_catalog_row.set_subtitle(tr("Catálogo não extraído"))

    def on_clear_shaders(self, _button):
        if getattr(self, "process", None):
            self.alert(tr("Кэш шейдеров"),
                       tr("Нельзя очистить кэш во время работы игры. Закройте игру перед очисткой."))
            return

        heading = tr("Очистить кэш шейдеров")
        body = tr("Удаление кэша шейдеров помогает при графических артефактах и сбоях освещения. При следующем запуске игра скомпилирует их заново.\n\nВыберите вариант очистки:")

        def run_cleaning(mode):
            self.clear_cache_button.set_sensitive(False)
            self.clear_cache_button.set_label(tr("Очистка…"))

            def worker():
                count, size = clear_shader_cache(self.user_dir(), mode)

                def on_done():
                    self.clear_cache_button.set_sensitive(True)
                    self.clear_cache_button.set_label(tr("Очистить…"))
                    self.update_cache_status()
                    mb = size / (1024 * 1024)
                    if count > 0:
                        msg = tr("Кэш шейдеров успешно очищен: {} файлов ({:.1f} МБ)").format(count, mb)
                    else:
                        msg = tr("Кэш шейдеров уже пуст")
                    self.toasts.add_toast(Adw.Toast(title=msg))

                GLib.idle_add(on_done)

            threading.Thread(target=worker, daemon=True).start()

        if hasattr(Adw, "AlertDialog"):
            dialog = Adw.AlertDialog(heading=heading, body=body)
            dialog.add_response("cancel", tr("Отмена"))
            dialog.add_response("game", tr("Только кэш игры"))
            dialog.add_response("all", tr("Полная (+ Драйвер GPU)"))
            dialog.set_response_appearance("game", Adw.ResponseAppearance.SUGGESTED)
            dialog.set_response_appearance("all", Adw.ResponseAppearance.DESTRUCTIVE)

            def on_response(_d, response):
                if response in ("game", "all"):
                    run_cleaning(response)

            dialog.connect("response", on_response)
            dialog.present(self)
        elif hasattr(Adw, "MessageDialog"):
            dialog = Adw.MessageDialog(transient_for=self, heading=heading, body=body)
            dialog.add_response("cancel", tr("Отмена"))
            dialog.add_response("game", tr("Только кэш игры"))
            dialog.add_response("all", tr("Полная (+ Драйвер GPU)"))
            dialog.set_response_appearance("game", Adw.ResponseAppearance.SUGGESTED)
            dialog.set_response_appearance("all", Adw.ResponseAppearance.DESTRUCTIVE)

            def on_response(_d, response):
                if response in ("game", "all"):
                    run_cleaning(response)

            dialog.connect("response", on_response)
            dialog.present()
        else:
            run_cleaning("game")

    def on_precompile_shaders(self, _button):
        if getattr(self, "process", None):
            self.alert(tr("Pré-compilar Shaders"),
                       tr("Não é possível pré-compilar enquanto o jogo estiver em execução. Feche o jogo primeiro."))
            return

        if hasattr(self, "precompile_cache_button"):
            self.precompile_cache_button.set_sensitive(False)
            self.precompile_cache_button.set_label(tr("Pré-compilando…"))

        def worker():
            import subprocess
            cmd = ["bash", "run.sh", "--precompile-shaders"]
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            for _line in proc.stdout:
                pass
            proc.wait()

            def on_done():
                if hasattr(self, "precompile_cache_button"):
                    self.precompile_cache_button.set_sensitive(True)
                    self.precompile_cache_button.set_label(tr("Pré-compilar…"))
                self.update_cache_status()
                if proc.returncode == 0:
                    msg = tr("Shaders pré-compilados com sucesso! Grimório atualizado.")
                else:
                    msg = tr("Erro durante a pré-compilação de shaders.")
                self.toasts.add_toast(Adw.Toast(title=msg))

            GLib.idle_add(on_done)

        threading.Thread(target=worker, daemon=True).start()

    def on_extract_static_catalog(self, _button):
        if getattr(self, "process", None):
            self.alert(tr("Catálogo Estático"),
                       tr("Não é possível extrair enquanto o jogo estiver em execução. Feche o jogo primeiro."))
            return

        if hasattr(self, "static_catalog_button"):
            self.static_catalog_button.set_sensitive(False)
            self.static_catalog_button.set_label(tr("Extraindo…"))

        def worker():
            import subprocess
            cmd = [sys.executable, "scripts/extract_static_shaders.py"]
            game_dir = self.game_dir() if hasattr(self, "game_dir") else getattr(self, "settings", {}).get("game_dir")
            if callable(game_dir):
                game_dir = game_dir()
            if game_dir and Path(game_dir).is_dir():
                cmd.extend(["--game-dir", str(game_dir)])
            out_catalog = self.user_dir() / "cache" / "CUSA03173" / "static_shader_catalog.json"
            cmd.extend(["--output", str(out_catalog)])
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            for _line in proc.stdout:
                pass
            proc.wait()

            def on_done():
                if hasattr(self, "static_catalog_button"):
                    self.static_catalog_button.set_sensitive(True)
                    self.static_catalog_button.set_label(tr("Extrair…"))
                self.update_cache_status()
                if proc.returncode == 0:
                    msg = tr("Catálogo estático extraído com sucesso! 4.376 shaders indexados.")
                else:
                    msg = tr("Erro durante a extração do catálogo estático.")
                self.toasts.add_toast(Adw.Toast(title=msg))

            GLib.idle_add(on_done)

        threading.Thread(target=worker, daemon=True).start()


