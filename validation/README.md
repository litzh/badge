# 验证

以下命令在 badge 根目录运行。Python 统一通过 uv 执行。

## 0.9.0 设置与延迟关机验证（2026-09-19）

- `uv run validation/test_device_tools.py` 通过：严格参数校验、主循环读写、取消/过期写请求丢弃、等待回答完成、10 秒截止、重复调度、失败/取消和 millis 溢出。
- 原有交互与语音协议原生测试通过；完整构建成功，Flash 1,596,331 bytes（50%）、静态 RAM 71,808 bytes（21%）。已刷入 badge-0.9.0。
- `uv run validation/device_settings.py http://DEVICE_IP` 实机静默验证通过：两次工具将亮度设为 50%（128）、音量设为 30%；三次工具完成先读取再调节，亮度升到 60%（153）、音量降到 20%。
- 关机请求执行一次工具，回答完成后进入 10 秒倒计时，取消接口成功撤销。原音量 100、亮度 255 已恢复；全程录音/ASR/TTS/播放耗时为 0，不追加对话历史。
- 真正播放后倒计时、BOOT/触屏取消和 AXP2101 实际切电由用户手动体验；自动测试没有播放、录音或切断电源。

## 0.8.0 本地工具验证（2026-09-19）

- 主机测试通过：真实 cJSON 参数与消息格式、读取字段筛选、过期/缺失/非有限测量、姿态计算条件、取消与旧响应隔离、主线程执行和 2 秒等待上限。
- 原交互、语音 WAV 协议、构建配置测试通过。最终构建应用 1,589,983 字节（50%）、静态 RAM 71,784 字节（21%）。
- 实机首次静默问答执行 1 次本地工具，返回全部六组状态；读到 78% 电量、约 2.3° 倾角、约 41.6℃ 芯片温度、音量 100、亮度 255、RSSI -50 dBm，模型正确说明芯片温度不是室温，模型阶段约 2.4 秒。
- 同轮本地读取 + 服务端搜索各 1 次通过，模型阶段约 3.2 秒；未发生工具消息配对错误。
- 两次静默测试的录音、ASR、TTS 和播放耗时均为 0，会话历史和音量亮度保持不变；真人提问及朗读由用户体验。
- 当前样机 IP 为 `192.168.1.25`，USB `/dev/cu.usbmodem31201`；IP/端口可能变化，应先核对身份。

## 0.7.0 交互验证记录（2026-09-18）

- BOOT 消抖/长按/上电按住/时间回绕、触摸唤醒/松开/滑出取消/切页取消、圆屏按钮边界及手动熄屏状态测试通过。
- 原屏保计时与语音 WAV 协议测试通过；离线检查主页、录音高亮、设置和信息页布局（非实机截图）。
- 固件编译、USB 写入及哈希校验通过；应用 1,577,063 字节（50%）、静态 RAM 65,464 字节（19%）。
- 实机 `/status` 确认 `badge-0.7.0`，保留用户音量 100、亮度 255；显式唤醒返回主页，10 秒后恢复屏保及采音。
- 本轮没有主动录音、播放或调用云服务。实体 BOOT/PWR、触摸按下反馈及播放中熄屏仍需用户手动体验。

## 无需设备

`uv run validation/test_device_tools.py`：字段校验、结果筛选、样本过期/失败、倾角有效性、工具 ID 和云端搜索块保留，以及主线程队列、取消、旧响应隔离、超时。
首次会将固定版本 cJSON 1.7.19 下载到 `.cache` 并校验 SHA256，仅用于主机测试；之后可离线执行。
静默实机联调：`uv run validation/device_tools.py http://DEVICE_IP [--search]`，调用 DeepSeek，不录音或播放，详见语音文档。

`clang++ -std=c++17 -Wall -Wextra -Werror validation/interaction.cpp -o /tmp/badge-interaction-test && /tmp/badge-interaction-test`：
BOOT 消抖/长按/上电按住/计时回绕，触摸唤醒/松开/滑出取消，圆屏按钮边界，问答期间手动熄屏保持。
`uv run validation/render_ui.py`：使用固件按钮坐标与实际 ASCII 字体生成离线布局预览；不是实体屏幕截图。

语音配置与协议测试：`uv run validation/test_voice_config.py`；
`clang++ -std=c++17 -Wall -Wextra -Werror validation/voice_protocol.cpp -o /tmp/badge-voice-protocol && /tmp/badge-voice-protocol`。
覆盖密钥不打印、构建校验/旧配置删除、提示词编码、WAV 边界/格式与 UTF-8 回答长度。
语音实机脚本见 [语音验证](../docs/voice.md#验证)。

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -Ivalidation/time_sync_stubs validation/time_sync.cpp -o /tmp/badge-time-sync-test
/tmp/badge-time-sync-test
c++ -std=c++11 validation/screensaver.cpp -o /tmp/badge-screensaver-test
/tmp/badge-screensaver-test
c++ -std=c++11 validation/battery_policy.cpp -o /tmp/badge-battery-test
/tmp/badge-battery-test
c++ -std=c++11 validation/display_frame.cpp -o /tmp/badge-display-test
/tmp/badge-display-test
uv run validation/render_frame.py
uv run validation/test_battery_monitor.py
bash -n scripts/build.sh scripts/setup_native_ctags.sh
git diff --check
```

- `time_sync.cpp`：未同步时间隐藏、NTP 超时切换、北京时间跨日、断网走时、重连与每小时同步、异常年份拒绝、毫秒回绕。模拟网络和系统时间，不访问 USB。
- `screensaver.cpp`：10 秒屏保、5 分钟显示上限、低电跳过屏保、亮度和唤醒、毫秒回绕。
- `battery_policy.cpp`：连续低电压、百分比误差、USB、采样故障/过期/间断、恢复迟滞和毫秒回绕。
- `display_frame.cpp`：全帧和奇数高度条带覆盖、源数据偏移及每次写入大小。
- `render_frame.py`：编译执行 `visualizer.cpp`，测试音频频段、直流抑制、反相双麦、
  长时间粒子运动、边界及缓冲区哨兵；输出 `validation-output/lumina.png`。
- `test_battery_monitor.py`：5 项测试，包含本机回环 HTTP 服务；验证新旧 CSV 兼容、熄屏字段、失败恢复、
  有效电量 0%、重启识别、转义、数据落盘、重绘及防止覆盖。

## 编译和实机

只读校时检查：`uv run validation/device_time_sync.py http://DEVICE_IP`，确认 badge 固件身份、
指定服务器、北京时间偏移及走时，并检查轮询期间待机计时继续增长；运行期间不要触摸按键。
该检查不唤醒或修改设备，与下方会改变显示状态的测试不同。

```sh
bash scripts/build.sh
uv run validation/device_visualizer.py http://DEVICE_IP
uv run validation/device_screensaver.py http://DEVICE_IP
uv run validation/device_screen_off.py http://DEVICE_IP
uv run --with pyserial validation/read_serial_status.py /dev/cu.usbmodemXXXX
```

烧录步骤见 [构建文档](../docs/build.md)。实机测试应顺序运行：会清空 echo、
唤醒设备，亮度测试会临时改变亮度后恢复；结束保持屏保。不要在续航采集期间运行。
串口脚本只读取状态，输出中过滤 Wi-Fi 名称和完整配置。
熄屏测试需要静置约 5 分半钟，持续检查 HTTP、面板模式和音频/动画计数。
它不触发低电关机；关机分支用主机模拟输入验证，实际 PMU 断电需后续充放电实验确认。

软件帧数、非黑像素统计和离线图像不能替代实际面板检查。目视确认：
文字更新不闪烁、静置 10 秒可见粒子、触摸唤醒、声音与倾斜影响画面。
实体 BOOT/PWR 动作需人工检查，温度和 PMU 电量不是经过独立仪器校准的测量。

## 0.5.1 校时验证记录（2026-09-18）

- 保留开始任务时已有的 0.5.0 未提交改动，增量增加时间模块、状态页时间行及 API 字段。
- 烧录前串口与 HTTP 均确认原固件为 `badge-0.5.0`；USB 序列号/MAC 为
  `80:45:6B:34:1F:30`，本次端口 `/dev/cu.usbmodem31201`，IP `192.168.8.121`。
  端口可能变化，后续操作需重新核对。本次未操作 RLCD。
- 校时主机测试、现有屏保和电池策略主机测试通过；Core 3.3.2-cn 编译通过，
  应用 1,356,347 字节（43%），静态 RAM 58,504 字节（17%），USB 烧录哈希校验通过。
- `/status` 确认 `firmware=badge-0.5.1`、`time_sync.status=synced`、`time_valid=true`、
  `last_error=null`，服务器列表与配置一致；最近同步为 `2026-09-18T03:34:29Z`。
- 连续读取北京时间 `11:34:34`、`11:34:39`、`11:34:45`，与主机差值约
  0.68、0.89、0.07 秒；待机时间增长，设备保持屏保，没有被状态查询唤醒。
- 电池、触摸、IMU 状态正常，麦克风/粒子屏保 ready，原亮度 255 保留，屏保实际亮度 128。
- 断网走时、重连、服务器失败切换、每小时同步、异常年份和 millis 回绕由主机模拟验证；
  未人为中断现场网络，未重新执行耗时的 310 秒熄屏实验，未将 API 数据等同于实体屏幕目视验证。
