"""Silent end-to-end local tool test: uv run validation/device_tools.py http://DEVICE_IP.

Uses DeepSeek with live device readings, but no ASR/TTS, recording, playback or history writes.
"""
import argparse
import json
from pathlib import Path
import time
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('base_url')
parser.add_argument('--search', action='store_true', help='Also verify local tools mixed with web search')
args = parser.parse_args()
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

def call(path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(args.base_url.rstrip('/') + path, data=data, headers={'Content-Type': 'application/json'})
    with opener.open(request, timeout=8) as response:
        return json.load(response)

before = call('/status')
assert before['firmware'] == 'badge-0.9.0', 'Wrong firmware/device'
assert not before['voice']['busy'], 'Device is in use'
for _ in range(45):
    if call('/status')['time_sync']['time_valid']: break
    time.sleep(1)
else: raise RuntimeError('Waiting for NTP')

question = '读取本机当前电量、姿态、芯片温度、音量、屏幕亮度和网络信号，简短告诉我结果。'
if args.search:
    question = '读取本机电量和音量，再联网搜索 DeepSeek 官方文档中 deepseek-flash 的模型名称，简短告诉我这三个结果。'
started = call('/voice/ask', {'text': question, 'speak': False})
turn = started['turn']
try:
    deadline = time.monotonic() + 110
    last = ''
    while time.monotonic() < deadline:
        voice = call('/voice')
        assert voice['turn'] == turn, 'Another conversation started; stop test'
        if voice['state'] != last:
            print('VOICE', voice['state'], flush=True); last = voice['state']
        if not voice['busy']: break
        time.sleep(.3)
    else: raise RuntimeError('Tool conversation timed out')
    output = Path(__file__).resolve().parents[1] / 'validation-output/voice'
    output.mkdir(parents=True, exist_ok=True)
    (output / time.strftime('%Y%m%d-%H%M%S-tools.json')).write_text(json.dumps(voice, ensure_ascii=False, indent=2))
    print(json.dumps(voice, ensure_ascii=False, indent=2))
    assert voice['state'] == 'done' and voice['answer'] and len(voice['answer']) <= 160
    assert voice['local_tool_calls'] >= 1 and voice['last_tool'] == 'read_device_state'
    assert voice['last_tool_result']['ok']
    if args.search: assert voice['searches'] >= 1
    assert voice['recorded_ms'] == voice['asr_ms'] == voice['tts_ms'] == voice['playback_ms'] == 0
    assert voice['history_turns'] == before['voice']['history_turns']
    after = call('/status')
    assert after['voice']['volume'] == before['voice']['volume']
    assert after['display']['brightness'] == before['display']['brightness']
    assert not after['microphone']['voice_owned']
    print('PASS live device tool result and short answer, settings/history preserved, no audio')
finally:
    current = call('/voice')
    if current['turn'] == turn and current['busy']: call('/voice/cancel', {})
