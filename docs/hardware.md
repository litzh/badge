# ESP32-S3-Touch-AMOLED-1.75C 硬件参考

整理自官方仓库 `waveshareteam/ESP32-S3-Touch-AMOLED-1.75C` 的
`examples/arduino/libraries/Mylibrary/pin_config.h`、README 与原理图
（原理图原件见本目录 PDF）。注意：**1.75C 与 1.75（无 C 后缀）引脚不同，不要混用资料**。

## 概览

| 部件 | 型号 / 接口 | 说明 |
| --- | --- | --- |
| MCU | ESP32-S3 | 8MB OPI PSRAM + 16MB Flash |
| 屏幕 | CO5300 QSPI AMOLED | 1.75 寸 466×466，16.7M 色；亮度用面板命令控制，**无背光 PWM** |
| 触摸 | CST9217 | I2C 电容触摸，带独立 RST/INT |
| 电源 | AXP2101 | 充放电管理 + 电池遥测，3.7V MX1.25 锂电池接口 |
| IMU | QMI8658 | 六轴（3 轴加速度 + 3 轴陀螺仪） |
| 音频输入 | ES7210 | 双数字麦克风 ADC |
| 音频输出 | ES8311 + 功放 | MX1.25 扬声器接口，PA 控制 GPIO46 |
| 按键 | PWR / BOOT | BOOT = GPIO0 |
| USB | Type-C 原生 USB | 烧录 + 串口日志（hwcdc） |

板载**没有** RTC、SD 卡槽、GNSS（这些是 1.75 的配置）。

## GPIO 引脚映射

| GPIO | 信号 | 说明 |
| ---: | --- | --- |
| 0 | BOOT | 按键 / 下载模式 |
| 1 | LCD_RESET | 屏幕复位（1.75 是 GPIO39，勿混） |
| 2 | TP_RST | 触摸复位（1.75 是 GPIO40，勿混） |
| 4–7 | LCD_SDIO0–3 | QSPI 屏幕数据 |
| 8 | I2S_DOUT | 到 ES8311（播放） |
| 9 | I2S_BCLK | ES8311/ES7210 共享位时钟 |
| 10 | I2S_DIN | 来自 ES7210（录音） |
| 11 | TP_INT | 触摸中断 |
| 12 | LCD_CS | 屏幕片选 |
| 14 | I2C_SCL | 共享 I2C 时钟 |
| 15 | I2C_SDA | 共享 I2C 数据 |
| 16 | I2S_MCLK | 音频主时钟（1.75 是 GPIO42，勿混） |
| 19/20 | USB D-/D+ | 原生 USB，勿复用 |
| 38 | LCD_SCLK | QSPI 屏幕时钟 |
| 45 | I2S_LRCK | ES8311/ES7210 共享帧时钟 |
| 46 | PA | 功放控制 |

## 共享 I2C 总线（SDA=15, SCL=14）已验证设备

烧录测试程序实测扫描到全部 5 个设备（2026 年本仓库验证）：

| 地址（7 位） | 设备 |
| ---: | --- |
| 0x18 | ES8311 音频编解码 |
| 0x34 | AXP2101 电源管理 |
| 0x40 | ES7210 麦克风 ADC |
| 0x5A | CST9217 触摸 |
| 0x6B | QMI8658 IMU |

部分 ESP 音频头文件用 8 位地址（ES8311=0x30，ES7210=0x80），传给期望 7 位地址的 API 时注意换算。

## 芯片协议要点（固件 board_io.cpp 已验证）

### CST9217 触摸

- 初始化：RST 拉低 10ms → 释放后等 30ms → 写 `[0xD1, 0x01]` 进命令模式 →
  等 10ms → 读 `[0xD1, 0xFC]` 4 字节，校验 checkcode 高 16 位为 0xCACA。
- 读点：**严格中断驱动**。TP_INT（GPIO11）下降沿表示有触摸事件；无事件时
  读数据返回无效帧（无 0xAB ACK）或 NACK。忙时 NACK 是正常的。
- 读点协议：写 `[0xD0, 0x00]`（STOP 后重新发起读，不用 repeated-start）→
  读 15 字节 → 回写 `[0xD0, 0x00, 0xAB]`。帧格式：`[0..4]` 触点 1，
  `[5]&0x7F` 触点数，`[6]` 应为 0xAB ACK。触点字节：`[0]&0x0F==0x06` 按下，
  `x=([1]<<4)|([3]>>4)`，`y=([2]<<4)|([3]&0x0F)`。
- 坐标需镜像校正（官方例 `setMirrorXY(true,true)`）：`x=465-x, y=465-y`。

### QMI8658 IMU

- WHO_AM_I（0x00）= 0x05。CTRL1=0x40（小端+地址自增）、CTRL2=0x15
  （±4g @250Hz）、CTRL3=0x55（±512dps @224Hz）、CTRL7=0x03（使能 accel+gyro）。
- 数据：加速度 0x35–0x3A（小端 int16，4/32768 g/LSB），陀螺仪
  0x3B–0x40（512/32768 dps/LSB）。

### AXP2101 电源

- 电池电压：0x34（高 5 位）/0x35（低 8 位），1mV/LSB；先置 0x68 bit0 使能检测。
- PWR 短按：INTEN2（0x41）bit3 使能，INTSTS2（0x49）bit3 读状态，写 1 清除。
- INTSTS/INTEN 寄存器组：0x40/0x41/0x42 使能，0x48/0x49/0x4A 状态。

## 官方资源

- 仓库：<https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C>
- 文档入口：<https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C>
- ESP-IDF BSP 组件：`waveshare/esp32_s3_touch_amoled_1_75c`（^3.0.0）
- 官方示例：仓库 `examples/arduino/examples/`（7 个）、`examples/esp-idf/`（5 个）
- 官方随附 Arduino 库：`examples/arduino/libraries/`（GFX、LVGL 8.4.0、SensorLib、XPowersLib、Mylibrary）
