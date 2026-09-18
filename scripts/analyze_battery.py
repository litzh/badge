# /// script
# requires-python = ">=3.10"
# dependencies = ["matplotlib>=3.9,<4"]
# ///
"""Analyze the longest discharge and the following recharge; never modify source logs."""
import argparse
import csv
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager


def timestamp(row):
    return datetime.fromisoformat(row["timestamp_utc"])


def minutes(first, last):
    return (timestamp(last) - timestamp(first)).total_seconds() / 60


def local(row):
    return timestamp(row).astimezone(timezone(timedelta(hours=8))).strftime("%m-%d %H:%M:%S")


def duration(value):
    seconds = round(value * 60)
    return f"{seconds // 3600} 小时 {(seconds % 3600) // 60} 分 {seconds % 60} 秒"


def analyze(source, output):
    with source.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    runs, current = [], []
    for index, row in enumerate(rows):
        discharging = (row["reachable"] == "True" and row["battery_status"] == "ok" and
                       row["vbus_present"] == "False" and row["power_state"] == "discharging" and row["voltage_v"])
        if discharging:
            if current and (row["reboot_detected"] == "True" or minutes(rows[current[-1]], row) > 1):
                runs.append(current)
                current = []
            current.append(index)
        elif current:
            runs.append(current)
            current = []
    if current:
        runs.append(current)
    if not runs:
        raise ValueError("没有有效连续放电区间")
    run = max(runs, key=lambda indexes: minutes(rows[indexes[0]], rows[indexes[-1]]))
    discharge = [rows[i] for i in run]
    charge_start = next((i for i in range(run[-1] + 1, len(rows)) if
                         rows[i]["reachable"] == "True" and rows[i]["power_state"] == "charging"), None)
    if charge_start is None:
        raise ValueError("最长放电之后未发现充电记录")
    # Charging completion requires PMU done + standby + 100%, not just voltage.
    full = next((i for i in range(charge_start, len(rows)) if
                 rows[i]["vbus_present"] == "True" and rows[i]["power_state"] == "standby" and
                 rows[i]["percent"] == "100" and rows[i]["charger_status_code"] == "4"), None)
    if full is None:
        raise ValueError("未发现充满终点")
    charge = rows[charge_start:full + 1]
    if any(r["reachable"] != "True" or not r["voltage_v"] or r["vbus_present"] != "True" for r in charge):
        raise ValueError("充电段含断连/拔线；需人工划分，不能当成完整充电")
    low_table = []
    for soc in [80, 50, 20, 15, 10, 5, 3, 1, 0]:
        row = next((r for r in discharge if r["percent"] and float(r["percent"]) <= soc), None)
        if row:
            low_table.append({"percent": soc, "time": local(row), "voltage_v": float(row["voltage_v"]),
                              "remaining_minutes": round(minutes(row, discharge[-1]), 2)})
    charge_table = []
    for soc in [20, 50, 80, 90, 95, 99, 100]:
        row = next((r for r in charge if r["percent"] and float(r["percent"]) >= soc), None)
        if row:
            charge_table.append({"percent": soc, "minutes": round(minutes(charge[0], row), 2),
                                 "voltage_v": float(row["voltage_v"])})
    cutoff = next(r for r in discharge if float(r["voltage_v"]) <= 3.35)
    summary = {
        "source": str(source.resolve()), "sample_count": len(rows),
        "reachable_samples": sum(r["reachable"] == "True" for r in rows),
        "discharge_start": local(discharge[0]), "discharge_end": local(discharge[-1]),
        "discharge_minutes": minutes(discharge[0], discharge[-1]),
        "discharge_last_voltage_v": float(discharge[-1]["voltage_v"]),
        "charge_start": local(charge[0]), "charge_full": local(charge[-1]),
        "charge_minutes": minutes(charge[0], charge[-1]),
        "cutoff_3_35_remaining_minutes": minutes(cutoff, discharge[-1]),
        "charging_reboots": [local(r) for r in charge[1:] if r["reboot_detected"] == "True"],
        "discharge_milestones": low_table, "charge_milestones": charge_table,
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    font = Path("/System/Library/Fonts/STHeiti Medium.ttc")
    chinese = font.exists()
    if chinese:
        font_manager.fontManager.addfont(str(font))
        plt.rcParams["font.family"] = font_manager.FontProperties(fname=str(font)).get_name()
    def label(zh, en):
        return zh if chinese else en
    plt.rcParams.update({"font.size": 10, "axes.spines.top": False})
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.6), layout="constrained")
    for ax, data, title, color in zip(axes, [discharge, charge],
                                    [label("放电：屏保持续运行", "Discharge: continuous screensaver"),
                                     label("充电：设备保持运行", "Recharge: device running")],
                                    ["#168c80", "#d77924"]):
        x = [minutes(data[0], r) for r in data]
        ax.plot(x, [float(r["voltage_v"]) for r in data], color=color, linewidth=1.6, label=label("电池电压", "Battery voltage"))
        ax.set(xlabel=label("经过时间（分钟）", "Elapsed time (minutes)"),
               ylabel=label("电池电压（V）", "Battery voltage (V)"), title=title, ylim=(2.8, 4.3))
        ax.grid(alpha=.18)
        other = ax.twinx()
        other.plot(x, [float(r["percent"]) for r in data], color="#7283af", linewidth=1, alpha=.75, label=label("PMU 电量估计", "PMU SOC estimate"))
        other.set(ylabel=label("PMU 电量估计（%）", "PMU estimate (%)"), ylim=(-5, 105))
        if data is discharge:
            ax.axhline(3.5, color="#ce9f36", linestyle=":", linewidth=1)
            ax.axhline(3.35, color="#cf4c57", linestyle="--", linewidth=1)
            ax.text(8, 3.36, label("首版保护阈值：3.35 V", "Initial cutoff: 3.35 V"), color="#a63b44", fontsize=9)
        handles, labels = ax.get_legend_handles_labels()
        other_handles, other_labels = other.get_legend_handles_labels()
        ax.legend(handles + other_handles, labels + other_labels, loc="lower left", fontsize=8)
    fig.suptitle(label("Badge 0.4.2 ｜ 2026-09-17 / 18 ｜ 屏保亮度 128 ｜ 每 10 秒采样",
                       "Badge 0.4.2 | 2026-09-17 / 18 | Screensaver brightness 128 | 10 s sampling"), fontsize=12)
    fig.savefig(output / "battery-cycle.png", dpi=180)
    fig.savefig(output / "battery-cycle.svg")
    plt.close(fig)

    lines = ["# 电池完整周期分析（2026-09-17 至 18）", "",
             f"数据：`{source.parent.name}/samples.csv`，共 {len(rows)} 条采样（北京时间）。原始记录未改动。", "",
             "## 结论", "",
             f"- 完整放电：{local(discharge[0])} → {local(discharge[-1])}，最后在线为止 **{duration(summary['discharge_minutes'])}**。",
             "  该段持续屏保、亮度 128、Wi-Fi 在线、麦克风和 IMU 参与粒子渲染；不是关屏或深睡续航。",
             f"- 最后在线电压 **{summary['discharge_last_voltage_v']:.3f}V**；下一次请求超时。结合用户确认，本段为耗尽实验，但日志没有精确的物理断电时间。",
             f"- 次日充电：{local(charge[0])} → {local(charge[-1])}，首次记录到充电至 PMU 报充满 **{duration(summary['charge_minutes'])}**。",
             "  起点设备 uptime=10 秒，可能略晚于实际插电；充满由 standby + 100% + PMU 状态码 4 共同判断。",
             "- 前面还有一次约 3 分钟补电，已排除，不与完整充电周期混算；夜间离线段不计入放电或充电时长。",
             f"- 充电中途发生 uptime 回退：{', '.join(summary['charging_reboots']) or '无'}。没有复位原因日志，不能归因为电池、固件或人工操作。",
             "- PMU 百分比在完整放电段单调下降，但不是线性的剩余时间：50% 时只剩约 87 分钟（总观察时长的约 31%）。",
             "  显示时标为电量估计，不据此估算精确剩余分钟；单次记录不足以重标定电量计。", "",
             "## 放电关键点", "", "| PMU 电量 | 时间 | 电压 | 距最后在线 |", "|---:|---|---:|---:|"]
    for row in low_table:
        lines.append(f"| {row['percent']}% | {row['time']} | {row['voltage_v']:.3f}V | {row['remaining_minutes']:.1f} 分钟 |")
    lines += ["", "## 充电关键点", "", "| PMU 电量 | 从首次充电样本起 | 电压 |", "|---:|---:|---:|"]
    for row in charge_table:
        lines.append(f"| {row['percent']}% | {row['minutes']:.1f} 分钟 | {row['voltage_v']:.3f}V |")
    lines += ["", "## 首版管理策略", "",
              "- 保留 PMU 百分比显示，附带电压、充电/充满和低电提示。",
              "- 仅电池供电时，≤15% 或 ≤3.50V 持续 15 秒进入低电模式：亮度最高 64，空闲 10 秒直接熄屏，不启动耗电屏保。",
              "- ≤3.35V 连续有效采样 15 秒，重新检查未接 USB、电池仍在且电压仍低，再请求 AXP2101 关机；不等待 0%/2.9V。",
              f"  本轮首次 3.35V 以下距最后在线约 {summary['cutoff_3_35_remaining_minutes']:.1f} 分钟；新策略会改变负载，不能直接断言新续航损失相同。",
              "- 关机依据电压，不依据可能失准的百分比；USB 供电、读取失败、过期或不连续样本不能触发误关机。",
              "- 低电恢复采用迟滞：断开 USB 时要求 ≥3.60V 且 ≥20%（百分比未知则只看电压）持续 30 秒；接上有效 USB 立即解除。",
              "- 正常空闲 10 秒进入屏保，屏保最多 5 分钟，然后面板休眠并停止采音和渲染；Wi-Fi 与电池查询保留。",
              "- 阈值是针对当前样机的首版保守软件策略，不替代电池保护板，也不改充电电流、充电终止电压和 PMU 硬件保护。",
              "- 没有电流/容量测量，无法由本记录计算 mAh、充电功率或保证新固件续航；改动后需再跑一个周期比较。", "",
              "![充放电曲线](battery-cycle.png)", ""]
    (output / "analysis.md").write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    analyze(args.csv, args.output)
