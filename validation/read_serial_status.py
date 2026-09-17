"""uv run --with pyserial validation/read_serial_status.py /dev/cu.usbmodem..."""
import json
import sys
import time
import serial

with serial.Serial(sys.argv[1], 115200, timeout=.5) as port:
    port.write(b"status\n")
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        line = port.readline().decode(errors="replace").strip()
        # A driver log and console JSON can share one USB line.
        start = line.find('{"firmware":')
        if start < 0:
            if line:
                print(line)
            continue
        line = line[start:]
        try:
            data = json.loads(line)
        except ValueError:
            continue
        keep = {key: data.get(key) for key in ("firmware", "uptime_seconds", "free_heap_bytes",
                                              "free_psram_bytes", "battery", "display", "microphone", "visualizer", "imu")}
        keep["wifi"] = {key: data.get("wifi", {}).get(key) for key in ("state", "source", "ip", "last_disconnect_reason")}
        print(json.dumps(keep, indent=2))
        break
    else:
        raise SystemExit("No status JSON received")
