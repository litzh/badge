"""Read-only NTP check: uv run validation/device_time_sync.py http://DEVICE_IP."""
import datetime as dt
import json
import sys
import time
import urllib.error
import urllib.request

client = urllib.request.build_opener(urllib.request.ProxyHandler({}))
base = sys.argv[1].rstrip("/")


def status():
    with client.open(base + "/status", timeout=5) as response:
        data = json.load(response)
    assert data["firmware"] == "badge-0.5.1", data.get("firmware")
    return data


deadline = time.monotonic() + 180
previous = None
while True:
    try:
        data = status()
        sync = data["time_sync"]
        state = (data["wifi"]["state"], sync["status"], sync["last_error"])
        if state != previous:
            print("STATE", state, flush=True)
            previous = state
        if sync["status"] == "synced":
            break
    except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
        print("Waiting:", error, flush=True)
    assert time.monotonic() < deadline, "NTP did not synchronize within 180 seconds"
    time.sleep(2)

assert sync["servers"] == ["ntp1.aliyun.com", "ntp1.tencent.com"], sync
assert sync["timezone"] == "Asia/Shanghai" and sync["sync_interval_seconds"] == 3600, sync
assert sync["last_error"] is None and sync["last_ntp_sync"].endswith("Z"), sync
last_clock = last_idle = None
for _ in range(3):
    data = status()
    sync = data["time_sync"]
    assert sync["time_valid"], sync
    clock = dt.datetime.fromisoformat(sync["time"])
    assert clock.utcoffset() == dt.timedelta(hours=8), sync
    difference = abs((dt.datetime.now(dt.timezone.utc) - clock).total_seconds())
    assert difference < 5, (sync, difference)
    idle = data["display"]["idle_ms"]
    if last_clock is not None:
        assert clock > last_clock, "Clock did not advance"
        assert idle > last_idle, "Idle timer reset during read-only polling; check physical interaction"
    last_clock, last_idle = clock, idle
    print("TIME", sync["time"], "host difference", round(difference, 2), "seconds; display", data["display"]["mode"], flush=True)
    time.sleep(5)
print("PASS badge identity, specified NTP servers, Beijing time, ticking, idle timer preserved", flush=True)
