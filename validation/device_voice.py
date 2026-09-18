"""Badge voice integration checks. Mutates device state; loopback records locally.

uv run validation/device_voice.py http://DEVICE_IP --say '语音测试。'
uv run validation/device_voice.py http://DEVICE_IP --ask '一加一等于几？'
uv run validation/device_voice.py http://DEVICE_IP --loopback 2
uv run validation/device_voice.py http://DEVICE_IP --cancel
"""
import argparse
import json
from pathlib import Path
import time
import urllib.error
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base_url')
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--say')
    mode.add_argument('--ask')
    mode.add_argument('--loopback', type=float)
    mode.add_argument('--cancel', action='store_true')
    parser.add_argument('--output', type=Path, default=Path('validation-output/voice'))
    args = parser.parse_args()
    if args.loopback is not None and not 0.5 <= args.loopback <= 20:
        parser.error('--loopback must be between 0.5 and 20 seconds')
    # Explicit direct LAN connection, unaffected by the workstation's proxy.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    def call(path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(args.base_url.rstrip('/') + path, data=data,
                                     headers={'Content-Type': 'application/json'})
        try:
            with opener.open(req, timeout=8) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            raise RuntimeError(f'HTTP {error.code}: {error.read().decode()}') from None
    status = call('/status')
    if not str(status.get('firmware', '')).startswith('badge-'):
        raise SystemExit('Refusing: device is not a badge')
    if status.get('voice', {}).get('busy'):
        raise SystemExit('Device already busy; not interrupting an existing conversation')
    before = {'firmware': status['firmware'], 'free_heap_bytes': status['free_heap_bytes'],
              'free_psram_bytes': status['free_psram_bytes'], 'voice': status.get('voice')}
    if args.say:
        call('/voice/say', {'text': args.say})
    elif args.ask:
        call('/voice/ask', {'text': args.ask})
    else:
        call('/voice/start', {'mode': 'loopback'})
        until = time.monotonic() + 5
        while time.monotonic() < until:
            current = call('/voice')
            if current['state'] == 'recording': break
            if not current['busy']: raise RuntimeError(current)
            time.sleep(0.1)
        else:
            call('/voice/cancel', {})
            raise RuntimeError('Recording did not start')
        if args.cancel:
            call('/voice/cancel', {})
        else:
            time.sleep(args.loopback)
            call('/voice/stop', {})
    deadline = time.monotonic() + 240
    states = []
    while time.monotonic() < deadline:
        result = call('/voice')
        if not states or states[-1] != result['state']:
            states.append(result['state']); print('VOICE', result['state'], flush=True)
        if not result['busy']: break
        time.sleep(0.5)
    else:
        call('/voice/cancel', {})
        raise RuntimeError('Voice job timed out')
    args.output.mkdir(parents=True, exist_ok=True)
    label = time.strftime('%Y%m%d-%H%M%S')
    (args.output / f'{label}.json').write_text(json.dumps({'before': before, 'states': states, 'result': result}, ensure_ascii=False, indent=2))
    print(json.dumps(result, ensure_ascii=False, indent=2))
    expected = 'cancelled' if args.cancel else 'done'
    if result['state'] != expected: raise SystemExit(f'Expected {expected}')
    if args.loopback:
        with opener.open(args.base_url.rstrip('/') + '/voice/recording.wav', timeout=10) as response:
            (args.output / f'{label}.wav').write_bytes(response.read())
    if args.ask and (not result['answer'] or len(result['answer']) > 160):
        raise SystemExit('Answer is empty or too long')
    # Polling should not keep the screen awake after the voice worker releases it.
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        status = call('/status')
        if status['display']['screensaver'] or status['display']['screen_off']:
            assert not status['microphone']['voice_owned']
            if status['display']['screensaver']:
                assert status['microphone']['active']
            print('PASS voice job, audio ownership release and idle display recovery')
            return
        time.sleep(0.5)
    raise SystemExit('Screensaver did not resume')


if __name__ == '__main__':
    main()
