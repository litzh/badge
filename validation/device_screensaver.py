"""uv run validation/device_screensaver.py http://DEVICE_IP

Wakes the screen with an empty echo; restores brightness and finishes in screensaver.
Physical touch/button behavior and animation appearance still need visual confirmation.
"""
import json
import sys
import time
import urllib.request

device = sys.argv[1].rstrip("/")
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def request(path, method="GET", body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(device + path, data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    with opener.open(req, timeout=3) as response:
        return json.load(response)


def await_saver():
    deadline = time.monotonic() + 13
    while time.monotonic() < deadline:
        state = request("/status")["display"]
        if state["screensaver"]:
            assert state["idle_ms"] >= 10000, state
            assert state["screensaver_brightness_limit"] == 128, state
            assert state["effective_brightness"] == min(state["brightness"], 128), state
            return state
        time.sleep(.4)
    raise AssertionError("Polling prevented screensaver or timer did not fire")


initial = request("/display")
saved = initial["brightness"]
try:
    request("/echo", "POST", {"message": ""})
    state = request("/display")
    assert not state["screensaver"] and state["effective_brightness"] == saved, state
    assert state["idle_ms"] < 2000, state
    entered = await_saver()
    time.sleep(1)
    still = request("/display")
    assert still["screensaver"] and still["idle_ms"] > entered["idle_ms"], still
    print("PASS: echo wakes; 10s timer; status/display polling does not wake", flush=True)
    request("/display/brightness", "PUT", {"value": 8})
    state = request("/display")
    assert not state["screensaver"] and state["effective_brightness"] == 8, state
    await_saver()
    assert request("/display")["effective_brightness"] == 8
    print("PASS: brightness write wakes; screensaver never increases low brightness", flush=True)
    battery = request("/status")["battery"]
    assert battery["status"] == "ok" and battery["present"], battery
    assert 2 < battery["voltage_v"] < 5, battery
    assert battery["sample_age_ms"] < 6000, battery
    assert battery["percent"] is None or 0 <= battery["percent"] <= 100, battery
    print("Battery:", json.dumps(battery), flush=True)
finally:
    request("/display/brightness", "PUT", {"value": saved})
    await_saver()
    print("Restored brightness:", saved, "; screensaver active", flush=True)
