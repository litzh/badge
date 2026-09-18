"""uv run validation/device_screen_off.py http://DEVICE_IP

Takes ~5 minutes 30 seconds. Wakes via empty echo, observes the real timeout,
checks mic/render stop, then verifies wake and return to the screensaver.
Never injects low-voltage data or requests hardware poweroff.
"""
import json
import sys
import time
import urllib.request

base = sys.argv[1].rstrip("/")
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def status():
    with opener.open(base + "/status", timeout=4) as response:
        return json.load(response)


def wake():
    req = urllib.request.Request(base + "/echo", data=b'{"message":""}',
                                 headers={"Content-Type": "application/json"}, method="POST")
    with opener.open(req, timeout=4) as response:
        assert response.status == 200


initial = status()
assert not initial["battery"]["low"] and not initial["battery"]["shutdown_pending"], "Charge battery before this test"
wake()
started = time.monotonic()
next_log = 0
saw_animation = False
while time.monotonic() - started < 325:
    s = status()
    d = s["display"]
    elapsed = time.monotonic() - started
    if elapsed >= next_log:
        print(f"{elapsed:.0f}s: mode={d['mode']}, idle_ms={d['idle_ms']}, mic={s['microphone']['active']}", flush=True)
        next_log += 30
    assert not s["battery"]["shutdown_pending"]
    if d["mode"] == "screensaver":
        saw_animation = True
    if d["screen_off"]:
        assert d["idle_ms"] >= 310000, d
        assert d["mode"] == "off" and not d["screensaver"] and d["effective_brightness"] == 0, d
        break
    time.sleep(2)
else:
    raise AssertionError("Did not enter display sleep at 10s + 5min")
assert saw_animation
time.sleep(1)
off = status()
assert not off["microphone"]["active"], off["microphone"]
time.sleep(3)
still = status()
assert still["display"]["screen_off"]
assert still["visualizer"]["frame_count"] == off["visualizer"]["frame_count"]
assert still["microphone"]["sample_count"] == off["microphone"]["sample_count"]
assert still["battery"]["sample_age_ms"] < 6000
print("PASS: five-minute timeout, panel sleep, microphone/render stopped; telemetry still online", flush=True)
wake()
s = status()
assert s["display"]["mode"] == "status" and not s["display"]["screen_off"], s["display"]
assert s["display"]["effective_brightness"] == s["display"]["brightness"]
deadline = time.monotonic() + 14
while time.monotonic() < deadline:
    s = status()
    if s["display"]["screensaver"] and s["microphone"]["active"]:
        break
    time.sleep(.3)
else:
    raise AssertionError("Screensaver/microphone did not resume after wake")
print("PASS: wake restores brightness; screensaver and microphone restart", flush=True)
print(json.dumps({"firmware": s["firmware"], "battery": s["battery"], "display": s["display"]}, indent=2), flush=True)
