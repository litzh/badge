# Badge HTTP API

固件 `badge-0.9.0`。所有接口在 80 端口，JSON 请求体**必须带
`Content-Type: application/json`**，否则 ESP32 WebServer 会按表单解析导致
`plain` 体为空（错误信息会有误导性）。

## GET /status

```json
{
  "firmware": "badge-0.9.0",
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
    "percent": 83, "charger_status_code": 2, "percent_source": "pmu_estimate",
    "low": false, "shutdown_pending": false, "shutdown_status": "idle",
    "low_voltage_v": 3.5, "shutdown_voltage_v": 3.35, "shutdown_confirm_ms": 15000
  },
  "display": {
    "brightness": 160, "effective_brightness": 128, "screensaver_brightness_limit": 128,
    "screensaver": true, "screen_off": false, "mode": "screensaver",
    "idle_ms": 15000, "screensaver_timeout_ms": 10000, "screensaver_max_display_ms": 300000
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

`low` 表示已确认低电节能；`shutdown_pending` 表示正在累计持续低电压证据，
不代表已经关机。`shutdown_status` 为 `idle` / `requested` / `write_failed` / `cancelled`，
最后两项表示关机寄存器写入失败或最后检查取消，保持熄屏，符合条件时最多每 5 秒重试。
实际关机后 HTTP 不再可达。策略与迟滞见 [电池管理](power-management.md)。

`imu.temperature_c` 是 QMI8658 内部芯片温度，不是环境温度。约每秒采样一次；
读取失败时 `temperature_status=read_failed`、温度为 null。
`temperature_sample_age_ms` 是最后一次成功读取的年龄，无成功样本时为 null。

`microphone.active` 表示采音线程正在工作，状态页和熄屏时停止采音，屏保期间启动。
`sample_count` 为累计双通道采样帧数。`level/low/mid/high` 为归一化的总能量和
三个重叠宽频段能量（0–1），不是精确 FFT 频谱；`dbfs` 为当前音频块的 RMS dBFS，
不是校准的声压级。停止采音或样本超过 500ms 时，这五个数值为 null。
屏保原始采样不保存、不上传。语音问答期间音频由语音任务独占，
此时 `microphone.voice_owned=true`，屏保特征采样暂停。
问答录音保存在 PSRAM 并提交 ASR，空闲时可下载最后一次录音，见 [语音接口](voice.md)。

`/status.voice` 与 `GET /voice` 返回相同状态，含阶段、问题/回答、错误、耗时、
搜索次数和构建配置摘要，不含 API 密钥。`volume` 为当前音量，`default_volume` 为构建默认值，
`volume_error` 为独立的音量设置错误。`POST /voice/volume` 接受 `{"volume":60}`（0–100 整数），
保存后重启保留，播放期间可调整。`local_tool_calls`、`last_tool`、`last_tool_result` 报告本地读写工具调用。
`POST /voice/ask` 可添加 `"speak":false` 静默验证问答与工具，不写入历史；设置工具仍会实际修改设备，关机工具仍会调度关机。启动/停止/取消/文本问答等接口见
[语音 HTTP 调试](voice.md#http-调试)。

## 网络时间（`GET /status` 的 `time_sync` 字段）

```json
{
  "status": "synced",
  "timezone": "Asia/Shanghai",
  "servers": ["ntp1.aliyun.com", "ntp1.tencent.com"],
  "sync_interval_seconds": 3600,
  "time_valid": true,
  "time": "2026-09-18T10:00:00+08:00",
  "last_ntp_sync": "2026-09-18T02:00:00Z",
  "last_error": null
}
```

- `status` 为 `waiting_for_wifi` / `syncing` / `retrying` / `synced`，表示网络同步状态。
- `time_valid` 表示本次启动已收到有效 NTP 时间，且当前系统时间有效；未校时为 false，`time=null`。
- `time` 是实时系统时间，明确带北京时间偏移 `+08:00`。普通断网后仍走时，此时 `status=waiting_for_wifi`、`time_valid=true`。
- `last_ntp_sync` 是本次启动最近一次成功同步的 UTC 时间（`Z`）；初始为 null。
- `last_error` 为 null、`ntp_timeout` 或 `ntp_time_out_of_range`，成功后清除。

每次 Wi-Fi 连接/重连均发起后台校时；正常每小时同步，60 秒未成功则交换服务器
优先级重试。已同步的时钟在 DNS/UDP 123 不可用时继续走时。无独立 RTC，断电或
重启后重新等待 NTP，不显示 1970 年或编译时间。接收到异常年份时停止展示该系统时间并重试。
状态页顶部显示日期时间；未同步显示等待 Wi-Fi / 正在校时。校时不会调用交互唤醒，
屏保与熄屏策略仍生效。SNTP 使用 [ESP-IDF 系统时间机制](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/system_time.html)。

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
  "screen_off": false, "mode": "screensaver", "screensaver_max_display_ms": 300000,
  "storage_ready": true, "error": null
}
```

`page` 为 `home` / `settings` / `info`；`manual_off` 标记 PWR 手动熄屏，
`touch_button_pressed` 表示当前捕获了一个可用触屏按钮。

`brightness` 是保存的正常亮度；`effective_brightness` 是当前实际面板设置。
屏保时取正常亮度与 128 的较小值，不修改 NVS。读取本接口不唤醒。
v0.4.2 将上限从 24 调整为 128，修复实际面板上粒子几乎不可见的问题。
`mode` 为 `status` / `screensaver` / `off`，后二者互斥；熄屏时 `screensaver=false`、
`screen_off=true`、`effective_brightness=0`，不能仅用 screensaver=false 推断状态页正在显示。
最后一次交互后 10 秒启动屏保，再运行最多 300 秒后熄屏，即正常情况下空闲总计 310 秒。
低电或等待保护关机时亮度最高 64，并在空闲 10 秒后直接熄屏；保存亮度不变。

## PUT /display/brightness

`{"value":0-255}`，立即生效并保存到 NVS（跨重启）。AMOLED 通过 CO5300
面板命令调亮度，没有背光 PWM。
成功设置会唤醒并重新计时；有效 `/echo` 请求也会唤醒。
已经满足保护关机条件时，写入不阻止关机，不保证唤醒；应接 USB 恢复供电。

## 配网

- 优先级：NVS 保存 > 构建期默认（`BADGE_WIFI_SSID` / `BADGE_WIFI_PASSWORD`）
  > BLE 配网。
- BLE 服务 UUID 与 RLCD 项目相同，设备名 `BADGE-xxxx`，配网页面
  `web/provision.html`（Mac Chrome 打开）。
- 凭据**连接成功才保存**，失败不覆盖旧配置。
- 运行中在 Settings 点击 Wi-Fi setup 重新开放 BLE 配网。
- 串口控制台命令：`provision` / `status` / `scan`。

## 按键

- **BOOT**（GPIO0，按住为低）：短按控制录音开始/提交/取消，按住不重复。
- **PWR** 短按：切换亮屏/熄屏，不修改亮度；亮度移至 Settings。
- PWR 长按为硬件电源路径控制（AXP2101），不由固件接管。

屏保/熄屏时首次触摸只唤醒，BOOT 可直接唤醒并开始录音。PWR 手动熄屏后问答继续，状态变化不自动唤醒。
屏保与测试程序说明见 [battery-monitor.md](battery-monitor.md)。

## 用户延迟关机

`GET /status` 的 `shutdown` 对象独立于低电保护：

```json
{"state":"countdown","remaining_seconds":10,"delay_seconds":10,"countdown_starts":"after_reply_completed","reason":""}
```

`state` 为 `idle`、`waiting_for_reply`、`countdown`、`cancelled`、`power_off_requested` 或 `failed`。
`remaining_seconds` 在等待回答时为 10，倒计时阶段逐秒递减；取消或失败后为 0。仅支持 10 秒延时。
`POST /shutdown/cancel`（可发送 `{}`）取消关机并返回此对象；等待回答时也会取消当前对话。重复取消返回当前状态，HTTP 200。
`POST /voice/cancel` 和 `/voice/reset` 同样撤销尚未执行的关机。
关机计划绑定发起它的对话；新对话自动撤销旧计划。`power_off_requested` 仅代表 PMU 写入成功；如果设备 2 秒后仍运行，转为 `failed`，原因 `power_off_not_completed`。
