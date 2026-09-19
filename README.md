# badge

面向 Waveshare ESP32-S3-Touch-AMOLED-1.75C 的固件项目，Arduino C++ 开发。

## 当前状态

固件 `badge-0.9.0`（参考 RLCD 项目移植）：Wi-Fi 配网（NVS / 构建期默认 /
BLE）、HTTP API、状态屏均已验证。板载设备全部在线：CO5300 AMOLED、
CST9217 触摸（中断驱动）、QMI8658 IMU、AXP2101 电池/电源键、ES8311/ES7210
音频（ES7210 麦克风采集、ES8311 扬声器播放）。

新增设备语音问答：短按 BOOT 开始/结束录音，处理或播放时短按取消，经过 MiniMax ASR、DeepSeek Flash
（按需服务端搜索）和 MiniMax TTS 后播放简短回答。密钥、音色与 TXT 系统提示词在构建时注入，
PWR 短按亮屏/熄屏；触屏按钮按下高亮、松开执行。默认音色 `male-qn-qingse`；Settings 页 `[-] / [+]` 调节音量并保存，重启保留。详见 [语音问答与构建参数](docs/voice.md)。

对话可调用本地工具查询传感器、调节并保存音量/亮度，以及在回答结束 10 秒后关机（BOOT 或屏幕按钮可取消），见 [本地工具](docs/voice.md#本地设备工具)。

每次连上 Wi-Fi 后自动通过 `ntp1.aliyun.com` / `ntp1.tencent.com` 校时，
状态页顶部显示北京时间；联网期间每小时同步，失败每 60 秒交换服务器优先级重试。
本板没有独立 RTC，已校时后普通断网仍由 ESP 系统时钟继续走时，重启后等待重新校时。
校时和查询不唤醒屏幕、不延长待机计时。状态字段见 [HTTP API](docs/api.md)。

10 秒无交互后进入黑底、低亮度粒子屏保：声音驱动起伏，倾斜和旋转改变运动，
IMU 芯片温度缓慢改变配色。触摸或按键唤醒并停止采音，见 [三源融合屏保](docs/visualizer.md)。
屏保最多显示 5 分钟，随后面板休眠并停止采音和动画；Wi-Fi 查询保持在线。
状态页显示电量估计和充电状态；低电节能与持续低电压关机见 [电池管理](docs/power-management.md)。
本机电池记录器持续采集电压、PMU 电量估计、USB/充放电状态，保存 CSV、
原始 JSONL 和可离线查看的实时曲线，见 [屏保与电池测试](docs/battery-monitor.md)。
首轮实测及阈值依据见 [2026-09-18 电池分析](docs/battery-analysis-20260918/analysis.md)。

按键、触摸、屏保和蓝牙配网操作见 [设备交互](docs/interaction.md)，
本版变更见 [更新记录](CHANGELOG.md)。

**屏幕是正圆形**：四角不可见，所有文字按所在行的弦长裁剪并居中绘制
（见 `firmware/badge/app.cpp` 的 `maxCharsForRow` / `drawRow`）。

## 目录

- `firmware/badge/` — 固件（`app.cpp` 主逻辑，`board_io.cpp` 触摸/IMU/电池/按键）
- `libraries/` — vendor 的官方 Arduino 库（GFX_Library_for_Arduino、Mylibrary 引脚定义）
- `examples/01_HelloWorld/` — 官方显示示例，作参考
- `scripts/build.sh` — 构建脚本（自动加载 `.env`，支持 `BADGE_WIFI_*` 和旧的 `RLCD_WIFI_*` 默认网络）
- `scripts/battery_monitor.py` — 零第三方依赖的电池采集与曲线程序，用 uv 运行
- `web/provision.html` — BLE 配网页面（与 RLCD 同一套 BLE 协议）
- `docs/hardware.md` — 引脚映射、I2C 地址、芯片协议要点、官方资源链接
- `docs/build.md` — 构建/烧录/串口调试说明（含 core 3.3.2 兼容补丁说明）
- `docs/api.md` — HTTP API 与配网/按键行为
- `docs/voice.md` — 语音问答、构建参数、录放音与调试接口
- `prompts/voice.txt` — 构建时读取的语音问答系统提示词
- `docs/interaction.md` — 设备交互与状态切换（不含 HTTP）
- `docs/visualizer.md` — 三源融合屏保的输入映射、资源占用与验证
- `validation/` — 主机测试、渲染检查及实机验证脚本
- `docs/ESP32-S3-Touch-AMOLED-1.75C-schematic.pdf` — 官方原理图

## 快速开始

```sh
bash scripts/build.sh        # 构建
# 烧录与串口调试命令见 docs/build.md；HTTP 接口见 docs/api.md
uv run scripts/battery_monitor.py http://DEVICE_IP --label '充电测试' --open
```
