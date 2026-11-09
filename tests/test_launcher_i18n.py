"""Regression tests for launcher localization and translation coverage."""

import ast
import glob
import os
import string
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
from paths import ROOT

sys.path.insert(0, str(ROOT / "launcher"))
import bbport_i18n


class LauncherI18nTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.formatter = string.Formatter()
        cls.tr_strings = set()

        for py_file in glob.glob(str(ROOT / "launcher/**/*.py"), recursive=True):
            with open(py_file, "r", encoding="utf-8") as f:
                try:
                    tree = ast.parse(f.read())
                except Exception:
                    continue
                for node in ast.walk(tree):
                    if isinstance(node, ast.Call):
                        func = node.func
                        if ((isinstance(func, ast.Name) and func.id == "tr") or
                                (isinstance(func, ast.Attribute) and func.attr == "tr")):
                            if (node.args and isinstance(node.args[0], ast.Constant)
                                    and isinstance(node.args[0].value, str)):
                                cls.tr_strings.add(node.args[0].value)
                    elif isinstance(node, ast.Assign):
                        for target in node.targets:
                            if isinstance(target, ast.Name) and target.id == "GUEST_MEMORY_MODES":
                                if isinstance(node.value, ast.List):
                                    for elt in node.value.elts:
                                        if isinstance(elt, ast.Tuple):
                                            if len(elt.elts) > 0 and isinstance(elt.elts[0], ast.Constant) and isinstance(elt.elts[0].value, str):
                                                cls.tr_strings.add(elt.elts[0].value)
                                            if len(elt.elts) > 2 and isinstance(elt.elts[2], ast.Constant) and isinstance(elt.elts[2].value, str):
                                                cls.tr_strings.add(elt.elts[2].value)

    @classmethod
    def tearDownClass(cls):
        bbport_i18n.set_language("ru")

    def test_pt_br_has_no_untranslated_russian_strings(self):
        bbport_i18n.set_language("pt_BR")
        untranslated = []
        for s in sorted(self.tr_strings):
            res = bbport_i18n.tr(s)
            has_cyrillic = any("\u0400" <= ch <= "\u04FF" for ch in res)
            if has_cyrillic:
                untranslated.append((s, res))
        self.assertEqual(untranslated, [], f"Untranslated Russian strings in pt_BR: {untranslated}")

    def test_en_has_no_untranslated_russian_strings(self):
        bbport_i18n.set_language("en")
        untranslated = []
        for s in sorted(self.tr_strings):
            res = bbport_i18n.tr(s)
            has_cyrillic = any("\u0400" <= ch <= "\u04FF" for ch in res)
            if has_cyrillic:
                untranslated.append((s, res))
        self.assertEqual(untranslated, [], f"Untranslated Russian strings in EN: {untranslated}")

    def test_pt_br_format_placeholders_match(self):
        bbport_i18n.set_language("pt_BR")
        for s in sorted(self.tr_strings):
            trans = bbport_i18n.tr(s)
            s_fields = [fn for _, fn, _, _ in self.formatter.parse(s) if fn is not None]
            trans_fields = [fn for _, fn, _, _ in self.formatter.parse(trans) if fn is not None]
            self.assertEqual(
                len(s_fields), len(trans_fields),
                f"Format placeholder count mismatch for key {repr(s)}: expected {len(s_fields)}, got {len(trans_fields)}"
            )

    def test_guest_memory_strings_localized(self):
        keys = [
            "Режим памяти гостя (BB_GUEST_MEMORY)",
            "Как выделять прямую память игры. На Wine/Proton+AMD выберите placed или dmabuf, иначе краш на staging buffer",
            "host: импорт памяти хоста (VK_EXT_external_memory_host)",
            "Быстрее всего на NVIDIA и на чистом Windows; на Wine/Proton+AMD крашится (CS rejected)",
            "dmabuf: выделение разделяемой памяти через dma-buf",
            "Работает на Linux/AMD без GPU fault; немного медленнее host на Windows",
            "private: приватные области для каждого прямого выделения",
            "Для Windows-драйверов, которые не импортируют секции/memfd, только VirtualAlloc",
            "placed: отображение памяти Vulkan по адресам игры",
            "VK_EXT_map_memory_placed; нужна на Wine/Proton+AMD, где host падает",
        ]
        for lang in ("en", "pt_BR", "pt", "zh_CN", "fr_FR"):
            bbport_i18n.set_language(lang)
            for k in keys:
                trans = bbport_i18n.tr(k)
                has_cyrillic = any("\u0400" <= ch <= "\u04FF" for ch in trans)
                self.assertFalse(has_cyrillic, f"Untranslated Russian in {lang} for key: {k}")

    def test_online_page_screenshot_strings_localized(self):
        bbport_i18n.set_language("pt_BR")
        expected = {
            "Игровой сервер (сообщения, пятна крови, призраки)":
                "Servidor de jogo (mensagens, manchas de sangue, espectros)",
            "Игровой сервер": "Servidor de jogo",
            "Сервер кооператива (колокола и призывы)": "Servidor cooperativo (sinos e invocações)",
            "Адрес сервера": "Endereço do servidor",
            "Адрес WebAPI": "Endereço do WebAPI",
            "Бесшовный кооператив (BBSC)": "Cooperativo contínuo (BBSC)",
            "Учётная запись": "Conta",
            "Online ID (NPID)": "ID Online (NPID)",
            "Пароль": "Senha",
            "Соединение": "Conexão",
            "UPnP (открыть порт на роутере автоматически)":
                "UPnP (abrir porta no roteador automaticamente)",
            "Выключите при Tailscale или другом VPN": "Desative ao usar Tailscale ou outra VPN",
            "Проверить соединение": "Testar conexão",
            "Проверить": "Testar",
        }
        for k, v in expected.items():
            self.assertEqual(bbport_i18n.tr(k), v)


if __name__ == "__main__":
    unittest.main()
