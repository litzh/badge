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

构建脚本自动通过 uv 加载上一级 `.env` 和项目根目录 `.env`（项目内同名值优先，进程环境优先于文件），读取 `BADGE_WIFI_SSID` /
`BADGE_WIFI_PASSWORD`；如果两者均未提供，则兼容旧的 `RLCD_WIFI_SSID` /
`RLCD_WIFI_PASSWORD`。两套变量不会交叉拼接。生成的 `wifi_defaults.h` 和 `.env`
均被 Git 忽略；构建产物包含默认网络凭据，不应公开分发。NVS 保存的网络仍优先。

语音密钥 `MINIMAX_API_KEY` / `DEEPSEEK_API_KEY` 同样由构建环境读取，音色默认
`male-qn-qingse`，系统提示词默认读取 `prompts/voice.txt`。例如：

```sh
BADGE_VOICE_ID=male-qn-qingse BADGE_SYSTEM_PROMPT_FILE=prompts/voice.txt bash scripts/build.sh
```

生成的 `voice_defaults.h` 被 Git 忽略，固件包含服务密钥。更多构建变量与约束见
[语音问答](voice.md#构建参数)。修改提示词或音色后需要重新构建、烧录。

macOS Apple Silicon 若 Arduino 自带的 Intel ctags 报 `Bad CPU type in executable`，
可在安装 Xcode Command Line Tools 后运行：

```sh
bash scripts/setup_native_ctags.sh
bash scripts/build.sh
```

脚本从 Arduino ctags 固定版本构建本机工具至 `.cache/arduino-ctags`，
修正旧内部宏名与现代 macOS SDK 的冲突；`build.sh` 自动使用它，不覆盖系统工具。
v0.4.2 已在 Apple Silicon、ESP32 Arduino Core 3.3.2-cn 上编译并刷入验证。
macOS 串口通常为 `/dev/cu.usbmodem*`，烧录时替换下方的 Linux 端口名。

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
