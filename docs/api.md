# Badge HTTP API

固件 `badge-0.4.2`。所有接口在 80 端口，JSON 请求体**必须带
`Content-Type: application/json`**，否则 ESP32 WebServer 会按表单解析导致
`plain` 体为空（错误信息会有误导性）。

## GET /status

```json
{
  "firmware": "badge-0.4.2",
  "uptime_seconds": 117,
  "free_heap_bytes": 227708,
  "free_psram_bytes": 8372404,
  "wifi": {
    "state": "connected",            // starting/connecting/connected/waiting_for_ble
    "ssid": "my-ssid",
    "source": "default",             // none/default/saved/provisioned
    "last_disconnect_reason": 0,
    "ip": "192.168.1.100",
    "rssi_dbm": -37,
    "provisioning": false,           // BLE 配网是否开放
    "provisioning_result": "idle"    // idle/connecting/saved/connection_failed/...
  },
  "battery": {
    "status": "ok", "voltage_v": 4.109, "sample_age_ms": 1199,
    "present": true, "vbus_present": true, "power_state": "charging",
    "percent": 83, "charger_status_code": 2
  },
  "display": {
    "brightness": 160, "effective_brightness": 128, "screensaver_brightness_limit": 128,
    "screensaver": true, "idle_ms": 15000, "screensaver_timeout_ms": 10000
  },
  "touch": {
    "controller": "CST9217",
    "status": "ok",
    "pressed": false,
    "x": 212, "y": 326,              // 已做镜像校正，屏幕坐标系
    "last_touch_age_ms": 32633
  },
  "imu": {
    "controller": "QMI8658",
    "status": "ok",
    "accel_g":  { "x": 0.025, "y": 0.043, "z": 1.009 },
    "gyro_dps": { "x": 3.53,  "y": 2.13,  "z": 0.14  },
    "sample_age_ms": 5,
    "temperature_status": "ok",
    "temperature_c": 40.5,
    "temperature_sample_age_ms": 313
  },
  "microphone": {
    "ready": true, "active": true, "sample_rate_hz": 16000,
    "sample_count": 2868736, "sample_age_ms": 1, "error": null,
    "level": 0.04, "low": 0.03, "mid": 0.03, "high": 0.003,
    "dbfs": -59.76
  },
  "visualizer": {
    "mode": "lumina", "ready": true, "particle_count": 80,
    "frame_count": 2305, "last_draw_ms": 71, "last_frame_period_ms": 76,
    "lit_pixels": 8287, "peak_channel_6bit": 62,
    "audio_energy": 0.21, "temperature_filtered_c": 40.45,
    "gravity_x": 0.04, "gravity_y": -0.01, "spin": 0, "shake": 0
  },
  "buttons": {
    "boot": { "pressed": false },
    "pwr":  { "short_press_count": 0 }  // AXP2101 POWERON 短按计数
  }
}
```

电池每 5 秒采样；`status` 为 `ok` / `read_failed` / `not_connected`。
电池不存在或读取失败时 `voltage_v=null`，不会用 0V 表示故障。
`present` 为电池检测，`vbus_present` 为 PMU 检测到有效 VBUS 输入；
读取失败为 null。`power_state` 为 `charging` / `discharging` / `standby` /
`unknown`，没有电池或读取失败时为 null；`standby` 表示电池既未充电也未放电，
与屏幕是否待机无关。`percent` 是 AXP2101 的电量估计，未知/无效为 null，
不是由电压线性换算，也不是已校准的容量测量。
`charger_status_code` 保留 PMU 0x01 寄存器的低 3 位原始值。
读取 `/status` 不重置待机计时。

`imu.temperature_c` 是 QMI8658 内部芯片温度，不是环境温度。约每秒采样一次；
读取失败时 `temperature_status=read_failed`、温度为 null。
`temperature_sample_age_ms` 是最后一次成功读取的年龄，无成功样本时为 null。

`microphone.active` 表示采音线程正在工作，正常状态页停止采音，屏保期间启动。
`sample_count` 为累计双通道采样帧数。`level/low/mid/high` 为归一化的总能量和
三个重叠宽频段能量（0–1），不是精确 FFT 频谱；`dbfs` 为当前音频块的 RMS dBFS，
不是校准的声压级。停止采音或样本超过 500ms 时，这五个数值为 null。
原始录音不保存、不上传，也没有录音下载接口。

## GET /visualizer

返回 `/status.visualizer` 对象，不唤醒屏幕。`frame_count` 是本次启动以来累计绘制帧数；
`last_draw_ms` 为上一帧计算与传屏耗时，`last_frame_period_ms` 为两次绘制开始的间隔。
唤醒后这些值保留最后一帧，是否正在屏保以 `/display.screensaver` 为准。
`ready=false`、`mode=simple_fallback` 表示粒子缓冲区分配失败，改用原有 7 光点屏保。
温度、重力、声音等字段是平滑后的动画输入，映射见 [visualizer.md](visualizer.md)。
`lit_pixels` 是非黑像素数，`peak_channel_6bit` 为最高颜色分量（0–63），
每 16 帧更新一次，用于区分空画面和显示亮度问题；这些统计不能替代面板目视确认。

## POST /echo

`{"message":"..."}`，ASCII（可含 `\n`），最长 240 字节。显示到屏幕上。

## GET /display

```json
{
  "brightness": 160, "effective_brightness": 128, "screensaver_brightness_limit": 128,
  "screensaver": true, "idle_ms": 15000, "screensaver_timeout_ms": 10000,
  "storage_ready": true, "error": null
}
```

`brightness` 是保存的正常亮度；`effective_brightness` 是当前实际面板设置。
屏保时取正常亮度与 128 的较小值，不修改 NVS。读取本接口不唤醒。
v0.4.2 将上限从 24 调整为 128，修复实际面板上粒子几乎不可见的问题。

## PUT /display/brightness

`{"value":0-255}`，立即生效并保存到 NVS（跨重启）。AMOLED 通过 CO5300
面板命令调亮度，没有背光 PWM。
成功设置会唤醒并重新计时；有效 `/echo` 请求也会唤醒。

## 配网

- 优先级：NVS 保存 > 构建期默认（`BADGE_WIFI_SSID` / `BADGE_WIFI_PASSWORD`）
  > BLE 配网。
- BLE 服务 UUID 与 RLCD 项目相同，设备名 `BADGE-xxxx`，配网页面
  `web/provision.html`（Mac Chrome 打开）。
- 凭据**连接成功才保存**，失败不覆盖旧配置。
- 运行中长按 BOOT 3 秒重新开放 BLE 配网。
- 串口控制台命令：`provision` / `status` / `scan`。

## 按键

- **BOOT**（GPIO0，按住为低）：长按 3 秒开放 BLE 配网。
- **PWR** 短按：循环切换屏幕亮度（64 → 160 → 255）。
- PWR 长按为硬件电源路径控制（AXP2101），不由固件接管。

触摸、BOOT 按下、PWR 短按均唤醒屏保；PWR 短按仍执行亮度循环。
屏保与测试程序说明见 [battery-monitor.md](battery-monitor.md)。
