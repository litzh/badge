import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from generate_voice_config import generate


class VoiceBuildTests(unittest.TestCase):
    def test_prompt_and_parameters_are_embedded_without_secret_logging(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            prompt = root / "prompt.txt"
            prompt.write_text('中文短回答。\n不要输出"长篇"。', encoding="utf-8")
            target = root / "voice.h"
            log = io.StringIO()
            env = {"MINIMAX_API_KEY": 'secret-";evil', "DEEPSEEK_API_KEY": "secret-two",
                   "BADGE_SYSTEM_PROMPT_FILE": str(prompt), "BADGE_VOICE_ID": "male-qn-qingse"}
            with contextlib.redirect_stdout(log):
                generate(env, target)
            source = target.read_text()
            self.assertIn("VOICE_SYSTEM_PROMPT", source)
            self.assertIn("\\xe4", source)
            self.assertNotIn('secret-', log.getvalue())
            self.assertNotIn('evil', source)
            self.assertEqual(target.stat().st_mode & 0o777, 0o600)

    def test_invalid_config_removes_stale_credentials(self):
        cases = [
            {"MINIMAX_API_KEY": "one"},
            {"BADGE_VOICE_ID": 'a";bad'},
            {"BADGE_VOICE_MAX_SECONDS": "600"},
            {"BADGE_VOICE_VOLUME": "101"},
            {"MINIMAX_API_KEY": "a\nb", "DEEPSEEK_API_KEY": "two"},
        ]
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "voice.h"
            for env in cases:
                target.write_text("old-secret")
                with self.subTest(env=list(env)), self.assertRaises(ValueError):
                    generate(env, target)
                self.assertFalse(target.exists())

    def test_prompt_must_exist_and_be_nonempty(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); prompt = root / "empty.txt"; prompt.write_text(" \n")
            with self.assertRaises(ValueError):
                generate({"BADGE_SYSTEM_PROMPT_FILE": str(prompt)}, root / "out.h")
            with self.assertRaises(FileNotFoundError):
                generate({"BADGE_SYSTEM_PROMPT_FILE": str(root / "missing.txt")}, root / "out.h")


if __name__ == "__main__":
    unittest.main()
