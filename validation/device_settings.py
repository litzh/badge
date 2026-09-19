"""Silent settings/queued-shutdown test. Restores settings and cancels shutdown before its deadline."""
import argparse
import json
from pathlib import Path
import time
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('base_url')
args = parser.parse_args()
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

def call(path, body=None, method=None):
    request = urllib.request.Request(args.base_url.rstrip('/') + path,
        data=None if body is None else json.dumps(body).encode(),
        headers={'Content-Type': 'application/json'}, method=method)
    with opener.open(request, timeout=2) as response:
        return json.load(response)

before = call('/status')
assert before['firmware'] == 'badge-0.9.0' and not before['voice']['busy']
assert before['shutdown']['state'] in ('idle', 'cancelled', 'failed'), 'Shutdown already pending'
for _ in range(45):
    if call('/status')['time_sync']['time_valid']: break
    time.sleep(1)
else: raise RuntimeError('Waiting for NTP')
turn = None
results = []
output = Path(__file__).resolve().parents[1] / 'validation-output/voice'
output.mkdir(parents=True, exist_ok=True)
report = {'original': {'volume': before['voice']['volume'], 'brightness': before['display']['brightness']}, 'results': results}
report_path = output / time.strftime('%Y%m%d-%H%M%S-settings.json')
report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2))

def ask(text):
    global turn
    response = call('/voice/ask', {'text': text, 'speak': False})
    turn = response['turn']
    deadline = time.monotonic() + 110
    while time.monotonic() < deadline:
        response = call('/voice')
        assert response['turn'] == turn, 'Another conversation started; stop test'
        if not response['busy']: break
        time.sleep(.15)
    else: raise RuntimeError('Conversation timed out')
    results.append(response)
    print(response['state'], response['local_tool_calls'], response['answer'], flush=True)
    assert response['state'] == 'done' and response['answer'] and len(response['answer']) <= 160
    assert response['recorded_ms'] == response['asr_ms'] == response['tts_ms'] == response['playback_ms'] == 0
    assert response['history_turns'] == before['voice']['history_turns']
    return response

try:
    answer = ask('请把屏幕亮度设为百分之五十，音量设为百分之三十。')
    state = call('/status')
    assert answer['local_tool_calls'] >= 2
    assert state['voice']['volume'] == 30 and state['display']['brightness'] == 128
    answer = ask('请把屏幕调亮一点，音量调小一点。')
    state = call('/status')
    assert answer['local_tool_calls'] >= 3
    assert state['voice']['volume'] == 20 and state['display']['brightness'] == 153
    ask('请在十秒后关机。')
    state = call('/status')['shutdown']
    assert state['state'] == 'countdown' and 6 <= state['remaining_seconds'] <= 10
    cancelled = call('/shutdown/cancel', {})
    assert cancelled['state'] == 'cancelled' and cancelled['remaining_seconds'] == 0
    assert call('/status')['shutdown']['state'] == 'cancelled'
    report['shutdown_before_cancel'] = state
    print('PASS absolute/relative saved settings, shutdown countdown and cancellation; no recording or playback', flush=True)
finally:
    current = call('/voice')
    if turn is not None and current['turn'] == turn:
        call('/shutdown/cancel', {})
        if current['busy']: call('/voice/cancel', {})
        call('/voice/volume', {'volume': before['voice']['volume']})
        call('/display/brightness', {'value': before['display']['brightness']}, 'PUT')
        restored = call('/status')
        assert restored['voice']['volume'] == before['voice']['volume']
        assert restored['display']['brightness'] == before['display']['brightness']
        report['settings_restored'] = True
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2))
