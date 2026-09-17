"""uv run validation/device_visualizer.py http://DEVICE_IP

Checks live audio/temperature/frame telemetry and mic stop/restart on screen wake.
"""
import json
import sys
import time
import urllib.request

device = sys.argv[1].rstrip("/")
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def status():
    with opener.open(device + "/status", timeout=4) as response:
        return json.load(response)


request = urllib.request.Request(device + "/echo", data=b'{"message":""}',
                                 headers={"Content-Type": "application/json"}, method="POST")
with opener.open(request, timeout=4) as response:
    assert response.status == 200
deadline = time.monotonic() + 3
while True:
    awake = status()
    assert not awake["display"]["screensaver"], awake["display"]
    if not awake["microphone"]["active"]:
        break
    assert time.monotonic() < deadline, "Microphone did not stop on wake"
    time.sleep(.15)
print("PASS: wake stops microphone", flush=True)
deadline = time.monotonic() + 15
while True:
    current = status()
    if current["display"]["screensaver"] and current["microphone"]["active"]:
        break
    assert time.monotonic() < deadline, current
    time.sleep(.4)
samples = []
start = time.monotonic()
for _ in range(15):
    current = status()
    samples.append(current)
    mic, visual, imu = current["microphone"], current["visualizer"], current["imu"]
    assert current["display"]["screensaver"]
    assert current["display"]["effective_brightness"] == min(current["display"]["brightness"], 128)
    assert mic["ready"] and mic["active"] and mic["error"] is None, mic
    assert mic["sample_count"] > 0 and mic["sample_age_ms"] < 500, mic
    assert all(mic[k] is not None and 0 <= mic[k] <= 1 for k in ("level", "low", "mid", "high")), mic
    assert -97 <= mic["dbfs"] <= 3, mic
    assert visual["ready"] and visual["mode"] == "lumina" and visual["particle_count"] == 80, visual
    if visual["frame_count"] >= 16:
        assert 100 < visual["lit_pixels"] < 466 * 466 / 3, visual
        assert visual["peak_channel_6bit"] > 0, visual
    assert imu["temperature_status"] == "ok" and -40 <= imu["temperature_c"] <= 85, imu
    assert imu["sample_age_ms"] < 500 and imu["temperature_sample_age_ms"] < 2000, imu
    time.sleep(.2)
elapsed = time.monotonic() - start
assert samples[-1]["microphone"]["sample_count"] > samples[0]["microphone"]["sample_count"]
assert samples[-1]["visualizer"]["frame_count"] > samples[0]["visualizer"]["frame_count"]
print("PASS: microphone streaming, three bands, temperature, animation, GET does not wake", flush=True)
print(json.dumps({"firmware": current["firmware"], "display": current["display"],
                  "imu_temperature_c": current["imu"]["temperature_c"],
                  "microphone": current["microphone"], "visualizer": current["visualizer"],
                  "frame_count_delta": samples[-1]["visualizer"]["frame_count"]-samples[0]["visualizer"]["frame_count"],
                  "observation_seconds": round(elapsed, 3), "free_heap_bytes": current["free_heap_bytes"],
                  "free_psram_bytes": current["free_psram_bytes"]}, indent=2), flush=True)
