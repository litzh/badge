# Badge HTTP API

固件 `badge-0.2.0`。所有接口在 80 端口，JSON 请求体**必须带
`Content-Type: application/json`**，否则 ESP32 WebServer 会按表单解析导致
`plain` 体为空（错误信息会有误导性）。

## GET /status

```json
{
  "firmware": "badge-0.2.0",
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
  "battery": { "status": "ok", "voltage_v": 4.109, "sample_age_ms": 1199 },
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
    "sample_age_ms": 5
  },
  "buttons": {
    "boot": { "pressed": false },
    "pwr":  { "short_press_count": 0 }  // AXP2101 POWERON 短按计数
  }
}
```

## POST /echo

`{"message":"..."}`，ASCII（可含 `\n`），最长 240 字节。显示到屏幕上。

## GET /display

`{"brightness":160,"storage_ready":true,"error":null}`

## PUT /display/brightness

`{"value":0-255}`，立即生效并保存到 NVS（跨重启）。AMOLED 通过 CO5300
面板命令调亮度，没有背光 PWM。

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
