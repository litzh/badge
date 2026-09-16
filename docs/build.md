# 构建与烧录

## 环境

已验证（2026，Linux）：

- Arduino CLI 1.5.1
- ESP32 Arduino Core **3.3.2-cn**（官方 CI 验证的是 3.3.11，3.3.2 需打一个库补丁，见下）
- Python 依赖用 `uv run --with pyserial` 等方式临时运行

## 构建

```sh
bash scripts/build.sh
```

FQBN 取自官方 CI（`scripts/discover_examples.py`）：

```
esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,FlashMode=qio,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc
```

产物在 `build/`。

## 烧录

```sh
arduino-cli upload \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,FlashMode=qio,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc' \
  --port /dev/ttyACM0 --input-dir build firmware/badge
```

Linux 下用户需在 `dialout` 组。进不了下载模式时：按住 BOOT，点按复位，开始烧录后松开 BOOT。

## 串口日志

注意：部分 Linux 环境下 `arduino-cli monitor` 读不到该板 USB CDC 的输出（打开成功但无数据），此时
用 pyserial 代替：

```sh
uv run --with pyserial python -c "
import serial
s = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
while True:
    print(s.read(256).decode(errors='replace'), end='')
"
```

固件使用 `USBMode=hwcdc`，日志走原生 USB CDC；开机早期日志可能在串口工具 attach
之前发出，建议在固件里加周期心跳来确认存活。

## GFX 库与 core 3.3.2 的兼容补丁

vendor 的 `libraries/GFX_Library_for_Arduino`（1.6.4）在
`src/databus/Arduino_ESP32SPI.cpp` 和 `Arduino_ESP32SPIDMA.cpp` 中，用
`ESP_ARDUINO_VERSION >= 3.3` 判断决定是否使用双参数
`spiFrequencyToClockDiv(spi, freq)`，但该 API 实际是 **3.3.3 才引入**的，
在 3.3.2(-cn) 上编译报错。已把判断条件修正为 `>= 3.3.3`（带 PATCH 版本与
defined 保护）。升级到 core ≥ 3.3.3 后此补丁无副作用，无需回退。
