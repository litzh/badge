"""Embed build parameters and a UTF-8 system prompt, without logging secrets."""
import hashlib
import os
from pathlib import Path
import re
import sys

from generate_wifi_config import literal

ROOT = Path(__file__).resolve().parents[1]


def generate(env, target):
    target.unlink(missing_ok=True)
    keys = [env.get(name, "").strip() for name in ("MINIMAX_API_KEY", "DEEPSEEK_API_KEY")]
    if bool(keys[0]) != bool(keys[1]):
        raise ValueError("Set both MINIMAX_API_KEY and DEEPSEEK_API_KEY, or neither to disable cloud voice")
    if any(any(c in key for c in "\r\n\0") or len(key) > 4096 for key in keys):
        raise ValueError("API keys contain invalid characters or exceed 4096 bytes")
    voice = env.get("BADGE_VOICE_ID", env.get("MINIMAX_VOICE_ID", "male-qn-qingse"))
    model = env.get("DEEPSEEK_MODEL", "deepseek-flash")
    tts_model = env.get("MINIMAX_TTS_MODEL", "speech-2.8-turbo")
    for name, value in (("voice ID", voice), ("DeepSeek model", model), ("TTS model", tts_model)):
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", value):
            raise ValueError(f"Invalid {name}")
    prompt_path = Path(env.get("BADGE_SYSTEM_PROMPT_FILE", str(ROOT / "prompts/voice.txt"))).expanduser().resolve()
    prompt = prompt_path.read_text(encoding="utf-8-sig").strip()
    if not prompt or "\0" in prompt or len(prompt.encode()) > 16384:
        raise ValueError("System prompt must be non-empty UTF-8 text up to 16384 bytes, without NUL")
    seconds = int(env.get("BADGE_VOICE_MAX_SECONDS", "30"))
    volume = int(env.get("BADGE_VOICE_VOLUME", "60"))
    if not 1 <= seconds <= 60 or not 0 <= volume <= 100:
        raise ValueError("BADGE_VOICE_MAX_SECONDS must be 1..60; BADGE_VOICE_VOLUME must be 0..100")
    values = dict(VOICE_MINIMAX_KEY=keys[0], VOICE_DEEPSEEK_KEY=keys[1], VOICE_ID=voice,
                  VOICE_LLM_MODEL=model, VOICE_TTS_MODEL=tts_model, VOICE_SYSTEM_PROMPT=prompt,
                  VOICE_PROMPT_SHA256=hashlib.sha256(prompt.encode()).hexdigest())
    text = "#pragma once\n// Generated; contains credentials. Do not commit or distribute.\n"
    text += "".join(f"constexpr char {name}[] = {literal(value)};\n" for name, value in values.items())
    text += f"constexpr int VOICE_MAX_SECONDS = {seconds};\nconstexpr int VOICE_VOLUME = {volume};\n"
    # Create with restricted mode before writing secrets.
    with os.fdopen(os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "w", encoding="utf-8") as stream:
        stream.write(text)
    print(f"VOICE build: {'enabled' if all(keys) else 'disabled (no keys)'}, {model}, {voice}, prompt sha256={values['VOICE_PROMPT_SHA256'][:12]}")


if __name__ == "__main__":
    try:
        generate(os.environ, Path(sys.argv[1]))
    except (ValueError, OSError) as error:
        raise SystemExit(str(error)) from None
