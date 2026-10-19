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
