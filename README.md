# badge

面向 Waveshare ESP32-S3-Touch-AMOLED-1.75C 的固件项目，Arduino C++ 开发。

## 当前状态

固件 `badge-0.2.0`（参考 RLCD 项目移植）：Wi-Fi 配网（NVS / 构建期默认 /
BLE）、HTTP API、状态屏均已验证。板载设备全部在线：CO5300 AMOLED、
CST9217 触摸（中断驱动）、QMI8658 IMU、AXP2101 电池/电源键、ES8311/ES7210
音频（I2C 在线，固件尚未启用）。

**屏幕是正圆形**：四角不可见，所有文字按所在行的弦长裁剪并居中绘制
（见 `firmware/badge/app.cpp` 的 `maxCharsForRow` / `drawRow`）。

## 目录

- `firmware/badge/` — 固件（`app.cpp` 主逻辑，`board_io.cpp` 触摸/IMU/电池/按键）
- `libraries/` — vendor 的官方 Arduino 库（GFX_Library_for_Arduino、Mylibrary 引脚定义）
- `examples/01_HelloWorld/` — 官方显示示例，作参考
- `scripts/build.sh` — 构建脚本（支持 `BADGE_WIFI_SSID` / `BADGE_WIFI_PASSWORD` 内置默认网络）
- `web/provision.html` — BLE 配网页面（与 RLCD 同一套 BLE 协议）
- `docs/hardware.md` — 引脚映射、I2C 地址、芯片协议要点、官方资源链接
- `docs/build.md` — 构建/烧录/串口调试说明（含 core 3.3.2 兼容补丁说明）
- `docs/api.md` — HTTP API 与配网/按键行为
- `docs/ESP32-S3-Touch-AMOLED-1.75C-schematic.pdf` — 官方原理图

## 快速开始

```sh
bash scripts/build.sh        # 构建
# 烧录与串口调试命令见 docs/build.md；HTTP 接口见 docs/api.md
```
