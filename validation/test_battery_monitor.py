"""Run with uv run validation/test_battery_monitor.py."""
import csv
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/battery_monitor.py"
spec = importlib.util.spec_from_file_location("battery_monitor", SCRIPT)
monitor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(monitor)


def status(uptime=100):
    return {"firmware": "badge-test", "uptime_seconds": uptime,
            "battery": {"status": "ok", "voltage_v": 4.12, "percent": 73,
                        "sample_age_ms": 500, "present": True, "vbus_present": True,
                        "power_state": "charging"},
            "display": {"screensaver": True, "brightness": 160, "effective_brightness": 24}}


def sample(data, **kwargs):
    return monitor.make_sample(data, elapsed=10, label="充电", device="http://badge", **kwargs)


class BatteryMonitorTest(unittest.TestCase):
    def test_unknown_and_failed_measurements_are_not_zero(self):
        legacy = status()
        legacy["battery"].pop("percent")
        legacy.pop("display")
        row = sample(legacy)
        self.assertEqual(row["voltage_v"], 4.12)
        self.assertIsNone(row["percent"])
        self.assertIsNone(row["screensaver"])
        for state in ("read_failed", "not_connected"):
            data = status()
            data["battery"]["status"] = state
            self.assertIsNone(sample(data)["voltage_v"])
        data = status()
        data["battery"]["sample_age_ms"] = 16000
        self.assertEqual(sample(data)["error"], "battery_sample_stale")
        self.assertIsNone(sample(data)["voltage_v"])
        failed = sample(None, error="timeout")
        self.assertFalse(failed["reachable"])
        self.assertIsNone(failed["voltage_v"])

    def test_reboot_and_valid_zero_soc(self):
        data = status(1)
        data["battery"]["percent"] = 0
        row = sample(data, previous_uptime=200)
        self.assertTrue(row["reboot_detected"])
        self.assertEqual(row["percent"], 0)
        self.assertFalse(sample(status(201), previous_uptime=200)["reboot_detected"])

    def test_report_escapes_untrusted_device_and_label(self):
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "report.html"
            row = sample(status())
            row["firmware"] = "</script><script>alert(1)</script>"
            monitor.write_report(target, [row], {"label": "<img src=x>", "interval": 10}, running=False)
            document = target.read_text()
            self.assertNotIn("</script><script>alert", document)
            self.assertNotIn("<img src=x>", document)
            self.assertNotIn('http-equiv="refresh"', document)
            self.assertIn("\\u003c/script>", document)

    def test_cli_records_failure_recovery_and_reboot(self):
        class Handler(BaseHTTPRequestHandler):
            requests = 0

            def do_GET(self):
                self.assert_path = self.path
                Handler.requests += 1
                if Handler.requests == 2:
                    body = b"not-json"
                else:
                    body = json.dumps(status(100 if Handler.requests == 1 else 1)).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *_):
                pass

        with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server, tempfile.TemporaryDirectory() as temp:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            output = Path(temp) / "capture"
            env = {**os.environ, "HTTP_PROXY": "http://127.0.0.1:1", "HTTPS_PROXY": "http://127.0.0.1:1",
                   "UV_CACHE_DIR": str(ROOT / ".cache/uv")}
            command = ["uv", "run", "--no-project", str(SCRIPT), f"http://127.0.0.1:{server.server_port}",
                       "--interval", "1", "--timeout", "0.3", "--duration", "2.5", "--output", str(output)]
            try:
                result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=20)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                rows = monitor.load_csv(output / "samples.csv")
                self.assertEqual(len(rows), 3)
                self.assertTrue(rows[0]["reachable"])
                self.assertFalse(rows[1]["reachable"])
                self.assertIsNone(rows[1]["voltage_v"])
                self.assertTrue(rows[2]["reboot_detected"])
                self.assertTrue(all(row["screensaver"] for row in (rows[0], rows[2])))
                raw = [json.loads(line) for line in (output / "raw.jsonl").read_text().splitlines()]
                self.assertEqual(len(raw), 3)
                self.assertIsNone(raw[1]["status"])
                report = (output / "battery.html").read_text()
                self.assertNotIn('http-equiv="refresh"', report)
                # Reusing a session directory must never overwrite previous samples.
                repeat = subprocess.run(command, env=env, text=True, capture_output=True, timeout=10)
                self.assertNotEqual(repeat.returncode, 0)
                self.assertEqual(len(monitor.load_csv(output / "samples.csv")), 3)
                rendered = subprocess.run(["uv", "run", "--no-project", str(SCRIPT), "--plot", str(output / "samples.csv")],
                                          env=env, text=True, capture_output=True, timeout=10)
                self.assertEqual(rendered.returncode, 0, rendered.stderr)
                self.assertTrue((output / "samples-report.html").exists())
            finally:
                server.shutdown()
                thread.join()


if __name__ == "__main__":
    unittest.main()
