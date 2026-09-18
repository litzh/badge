#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///
"""Poll Badge without waking its screen. CSV + JSONL + a live, offline HTML chart."""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import html
import http.client
import json
import math
import os
from pathlib import Path
import signal
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser


LEGACY_FIELDS = (
    "timestamp_utc", "elapsed_seconds", "label", "device", "reachable", "error",
    "firmware", "uptime_seconds", "reboot_detected", "battery_status", "voltage_v",
    "percent", "present", "vbus_present", "power_state", "charger_status_code",
    "sample_age_ms", "screensaver", "brightness", "effective_brightness", "rssi_dbm",
    "request_ms",
)
FIELDS = LEGACY_FIELDS + ("screen_off", "display_mode", "battery_low", "shutdown_pending", "shutdown_status")
MAX_RESPONSE = 128 * 1024
TEMPLATE = Path(__file__).with_name("battery_chart.html")


def number(value):
    return value if type(value) in (int, float) and math.isfinite(value) else None


def boolean(value):
    return value if type(value) is bool else None


def device_url(value: str) -> str:
    value = value.strip()
    if "://" not in value:
        value = "http://" + value
    parts = urllib.parse.urlsplit(value)
    if (parts.scheme not in ("http", "https") or not parts.hostname or parts.username or
            parts.password or parts.query or parts.fragment or parts.path not in ("", "/")):
        raise argparse.ArgumentTypeError("设备地址应为 http://IP[:端口]，不带路径或凭据")
    try:
        parts.port
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    return value.rstrip("/")


def positive(value: str) -> float:
    result = float(value)
    if not math.isfinite(result) or result <= 0:
        raise argparse.ArgumentTypeError("必须为大于 0 的有限数字")
    return result


def fetch_status(opener, device: str, timeout: float) -> dict:
    request = urllib.request.Request(device + "/status", headers={"Accept": "application/json"})
    with opener.open(request, timeout=timeout) as response:
        raw = response.read(MAX_RESPONSE + 1)
    if len(raw) > MAX_RESPONSE:
        raise ValueError("设备响应超过 128 KiB")
    def reject_constant(value):
        raise ValueError(f"响应包含非有限数字：{value}")
    status = json.loads(raw, parse_constant=reject_constant)
    if not isinstance(status, dict) or not isinstance(status.get("battery"), dict):
        raise ValueError("响应缺少 battery 对象")
    return status


def make_sample(status, *, elapsed, label, device, error="", request_ms=0,
                previous_uptime=None):
    row = dict.fromkeys(FIELDS)
    row.update(timestamp_utc=datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
               elapsed_seconds=round(elapsed, 3), label=label, device=device,
               reachable=status is not None, error=error, request_ms=round(request_ms, 1))
    if status is None:
        return row
    battery = status["battery"]
    display = status.get("display") or {}
    wifi = status.get("wifi") or {}
    if not isinstance(display, dict):
        display = {}
    if not isinstance(wifi, dict):
        wifi = {}
    uptime = number(status.get("uptime_seconds"))
    row.update(firmware=status.get("firmware"), uptime_seconds=uptime,
               reboot_detected=uptime is not None and previous_uptime is not None and
               uptime < previous_uptime,
               battery_status=battery.get("status"), power_state=battery.get("power_state"))
    # Never turn an absent/failed/stale battery sample into a zero-voltage point.
    age = number(battery.get("sample_age_ms"))
    row["sample_age_ms"] = age
    valid = battery.get("status") == "ok" and (age is None or 0 <= age <= 15000)
    if valid:
        voltage = number(battery.get("voltage_v"))
        row["voltage_v"] = voltage if voltage is not None and voltage > 0 else None
        percent = number(battery.get("percent"))
        row["percent"] = percent if percent is not None and 0 <= percent <= 100 else None
    elif battery.get("status") == "ok":
        row["error"] = "battery_sample_stale"
    for field in ("present", "vbus_present"):
        row[field] = boolean(battery.get(field))
    row["charger_status_code"] = number(battery.get("charger_status_code"))
    row["screensaver"] = boolean(display.get("screensaver"))
    row["screen_off"] = boolean(display.get("screen_off"))
    row["display_mode"] = display.get("mode")
    row["battery_low"] = boolean(battery.get("low"))
    row["shutdown_pending"] = boolean(battery.get("shutdown_pending"))
    row["shutdown_status"] = battery.get("shutdown_status")
    for field in ("brightness", "effective_brightness"):
        row[field] = number(display.get(field))
    row["rssi_dbm"] = number(wifi.get("rssi_dbm"))
    return row


def write_report(path: Path, samples: list[dict], metadata: dict, *, running: bool):
    payload = json.dumps({"samples": samples, "metadata": metadata, "running": running},
                         ensure_ascii=False, allow_nan=False).replace("<", "\\u003c")
    template = TEMPLATE.read_text(encoding="utf-8")
    # Use a meta refresh so file:// reports stay live without a local server or CDN.
    refresh = '<meta http-equiv="refresh" content="10">' if running else ""
    document = template.replace("__REFRESH__", refresh).replace("__LABEL__", html.escape(metadata["label"]))
    document = document.replace("__DATA__", payload)
    temporary = path.with_suffix(".html.tmp")
    temporary.write_text(document, encoding="utf-8")
    temporary.replace(path)


def load_csv(path: Path) -> list[dict]:
    boolean_fields = {"reachable", "reboot_detected", "present", "vbus_present", "screensaver",
                      "screen_off", "battery_low", "shutdown_pending"}
    text_fields = {"timestamp_utc", "label", "device", "error", "firmware", "battery_status", "power_state",
                   "display_mode", "shutdown_status"}
    rows = []
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        if not reader.fieldnames or not set(LEGACY_FIELDS).issubset(reader.fieldnames):
            raise ValueError("CSV 不是本程序生成的电池记录")
        for raw in reader:
            row = {}
            for field in FIELDS:
                value = raw.get(field)
                if value in (None, ""):
                    row[field] = None
                elif field in text_fields:
                    row[field] = value
                elif field in boolean_fields:
                    if value not in ("True", "False"):
                        raise ValueError(f"CSV 的 {field} 不是布尔值")
                    row[field] = value == "True"
                else:
                    row[field] = number(float(value))
            if row["elapsed_seconds"] is None:
                raise ValueError("CSV 缺少有效的 elapsed_seconds")
            rows.append(row)
    return rows


def main(argv=None):
    parser = argparse.ArgumentParser(description="持续记录 Badge 电池曲线，不唤醒屏保。Ctrl+C 停止并保留报告。")
    parser.add_argument("device", nargs="?", default=os.environ.get("BADGE_DEVICE"), help="设备 IP 或 URL；也可设 BADGE_DEVICE")
    parser.add_argument("--interval", type=positive, default=10, help="采样间隔秒，默认 10")
    parser.add_argument("--timeout", type=positive, default=3, help="单次网络超时秒，默认 3")
    parser.add_argument("--duration", type=positive, help="自动停止的时长（秒）；默认一直记录")
    parser.add_argument("--label", default="电池测试", help="本次测试名称，例如充电或待机")
    parser.add_argument("--output", type=Path, help="新建输出目录；拒绝复用已有目录以免覆盖数据")
    parser.add_argument("--open", action="store_true", help="在浏览器打开实时曲线")
    parser.add_argument("--plot", type=Path, metavar="CSV", help="从已有 CSV 生成静态报告，无需设备")
    args = parser.parse_args(argv)
    if args.plot:
        samples = load_csv(args.plot)
        report = args.plot.with_name(args.plot.stem + "-report.html")
        metadata = {"label": samples[0]["label"] if samples else args.label,
                    "device": samples[0]["device"] if samples else "", "interval": args.interval}
        write_report(report, samples, metadata, running=False)
        print(f"报告：{report.resolve()}")
        if args.open:
            webbrowser.open(report.resolve().as_uri())
        return 0
    if not args.device:
        parser.error("请提供设备地址，或设置 BADGE_DEVICE")
    try:
        device = device_url(args.device)
    except argparse.ArgumentTypeError as exc:
        parser.error(str(exc))
    if args.interval < 1:
        parser.error("采样间隔至少 1 秒；电池固件每 5 秒更新，建议 10 秒或更长")
    if args.timeout >= args.interval:
        parser.error("--timeout 必须小于 --interval")
    output = args.output or Path(__file__).resolve().parents[1] / "recordings" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    output.mkdir(parents=True, exist_ok=False)
    report = output / "battery.html"
    metadata = {"label": args.label, "device": device, "interval": args.interval,
                "started_at": datetime.now(timezone.utc).isoformat(), "timeout": args.timeout,
                "duration": args.duration}
    (output / "session.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    # LAN traffic must not be routed through a user's HTTP_PROXY/HTTPS_PROXY.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    stop = threading.Event()
    old_signals = {sig: signal.signal(sig, lambda *_: stop.set()) for sig in (signal.SIGINT, signal.SIGTERM)}
    samples = []
    previous_uptime = None
    started = time.monotonic()
    next_poll = started
    print(f"设备：{device}，每 {args.interval:g} 秒采样；Ctrl+C 停止", flush=True)
    print(f"数据：{output.resolve()}\n曲线：{report.resolve()}", flush=True)
    try:
        with (output / "samples.csv").open("x", newline="", encoding="utf-8") as csv_file, \
                (output / "raw.jsonl").open("x", encoding="utf-8") as raw_file:
            writer = csv.DictWriter(csv_file, fieldnames=FIELDS)
            writer.writeheader()
            csv_file.flush()
            write_report(report, samples, metadata, running=True)
            if args.open:
                webbrowser.open(report.resolve().as_uri())
            while not stop.is_set():
                now = time.monotonic()
                remaining = None if args.duration is None else args.duration - (now - started)
                if remaining is not None and remaining <= 0:
                    break
                delay = max(0, next_poll - now)
                if stop.wait(delay if remaining is None else min(delay, remaining)):
                    break
                if args.duration is not None and time.monotonic() - started >= args.duration:
                    break
                request_started = time.monotonic()
                status, error = None, ""
                try:
                    status = fetch_status(opener, device, args.timeout)
                except (OSError, ValueError, urllib.error.URLError, http.client.HTTPException) as exc:
                    error = f"{type(exc).__name__}: {exc}"
                completed = time.monotonic()
                row = make_sample(status, elapsed=completed - started, label=args.label,
                                  device=device, error=error, request_ms=(completed - request_started) * 1000,
                                  previous_uptime=previous_uptime)
                if row["uptime_seconds"] is not None:
                    previous_uptime = row["uptime_seconds"]
                writer.writerow(row)
                raw_file.write(json.dumps({"sample": row, "status": status}, ensure_ascii=False,
                                          allow_nan=False) + "\n")
                csv_file.flush()
                raw_file.flush()
                os.fsync(csv_file.fileno())
                os.fsync(raw_file.fileno())
                samples.append(row)
                # A report problem must not silently discard the primary measurements.
                try:
                    write_report(report, samples, metadata, running=True)
                except OSError as exc:
                    print(f"曲线更新失败（CSV 仍在保存）：{exc}", file=sys.stderr, flush=True)
                voltage = row["voltage_v"]
                info = f"{voltage:.3f} V" if voltage is not None else "电压不可用"
                print(f"{row['timestamp_utc']}  {row['elapsed_seconds']:.0f}s  {info}  "
                      f"{row['power_state'] or '--'}  SOC={row['percent']}  "
                      f"屏保={row['screensaver']}  {row['error'] or row['battery_status'] or ''}", flush=True)
                next_poll += args.interval
                # After a sleep/slow request, skip missed slots rather than hammering the device.
                if next_poll <= time.monotonic():
                    next_poll = time.monotonic() + args.interval
    finally:
        try:
            write_report(report, samples, metadata, running=False)
        finally:
            for sig, handler in old_signals.items():
                signal.signal(sig, handler)
        print(f"已保存 {len(samples)} 条记录：{report.resolve()}", flush=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        raise SystemExit(f"错误：{exc}") from None
