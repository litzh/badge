# 设备语音问答

`badge-0.9.0`：ES7210 录音 → MiniMax `asr-1.0` → DeepSeek `deepseek-flash`（服务端搜索）→ MiniMax `speech-2.8-turbo` → ES8311 扬声器播放。设备直接通过 Wi-Fi 访问服务，无需电脑中转。

## 构建参数

```sh
# 在 badge 目录运行；密钥自动从 ../.env 和 .env 读取。
BADGE_VOICE_ID=male-qn-qingse \
BADGE_SYSTEM_PROMPT_FILE=prompts/voice.txt \
bash scripts/build.sh
```

也可在构建进程环境中设置 `MINIMAX_API_KEY`、`DEEPSEEK_API_KEY`，无需写入源码。环境变量优先；同名变量在 `badge/.env` 中的配置优先于上一级 `.env`。

| 参数 | 默认值 / 说明 |
| --- | --- |
| `MINIMAX_API_KEY` | MiniMax 密钥 |
| `DEEPSEEK_API_KEY` | DeepSeek 密钥 |
| `BADGE_VOICE_ID` | `male-qn-qingse`；未设置时兼容 `MINIMAX_VOICE_ID` |
| `DEEPSEEK_MODEL` | `deepseek-flash` |
| `MINIMAX_TTS_MODEL` | `speech-2.8-turbo` |
| `BADGE_SYSTEM_PROMPT_FILE` | `badge/prompts/voice.txt`；相对路径按调用构建命令的工作目录解析 |
| `BADGE_VOICE_MAX_SECONDS` | 30；允许 1–60 秒 |
| `BADGE_VOICE_VOLUME` | 首次启动默认 60；允许 0–100，已有设备保存值优先 |

提示词以 UTF-8 文本读取，构建时嵌入固件。修改 TXT 或参数后须重新构建、烧录。生成的 `voice_defaults.h` 权限为 0600，并已加入 Git 忽略；构建日志仅显示模型、音色和提示词摘要。固件本身含密钥，勿公开分发构建产物。

两个密钥都未提供时云端语音关闭，其他功能仍可用；只提供一个密钥时构建报错。提示词不存在、为空或超过 16 KiB 时构建失败，并删除旧生成头文件，避免误用旧配置。

## 设备操作

- BOOT 短按：空闲时唤醒并录音，录音时结束提交，处理/播放时取消；按住不重复。
- 主页有相同行为的可见触屏按钮：按下高亮，松开执行，滑出取消。屏保或熄屏时首次触摸仅唤醒。
- Settings 页调整音量和亮度，保存至 NVS；音量 0–100，每次 10，0 静音，播放中也可调整。
- PWR 短按控制亮屏/熄屏，不修改亮度或取消问答。手动熄屏后，问答阶段变化不会自动亮屏。
- Wi-Fi 配网入口移到 Settings，BOOT 长按不再配网；电池保护保持原有行为。
- 录音到达上限自动提交。屏幕显示阶段、录音计时和 BOOT 当前动作；中文问答正文通过 `GET /voice` 查看。
- 录音与播放互斥，对话期间屏保采音暂停；屏幕已亮时暂停待机计时，结束后重新计时。

详细按键和页面说明见 [设备交互](interaction.md)。

默认系统提示词与本地原型一致：普通问答直接回答，时效信息或明确要求查证时使用服务端 `web_search_20260209`，每轮搜索预算为 2 次。回答通常 40–100 字符、最多 160 字符；超长时尝试一次无搜索缩写，仍超长或响应不完整则报错，避免朗读半句。设备保留最近 6 轮成功完成的问答，重启或 reset 后清空。

## HTTP 调试

接口位于已有的 80 端口，POST JSON 带 `Content-Type: application/json`。请只在可信局域网使用，接口与现有设备 HTTP API 一样没有身份认证。

| 接口 | 用途 |
| --- | --- |
| `GET /voice` | 状态、问题、回答、错误阶段、耗时、搜索次数、提示词 SHA256；不返回密钥 |
| `POST /voice/volume` | `{"volume":60}`，设置并保存 0–100 整数音量；成功 200、无效值 400、存储失败 500；不要求语音空闲 |
| `POST /voice/start` | 开始录音；可选 `{"mode":"chat"}`（默认）、`echo`（识别原文复述）、`loopback`（录音原声回放，不调用云端） |
| `POST /voice/stop` | 结束录音并提交；非录音状态返回 409 |
| `POST /voice/cancel` | 请求取消；不会清空上一轮成功的上下文 |
| `POST /voice/reset` | 空闲时清空上下文；忙碌返回 409 |
| `POST /voice/ask` | `{"text":"一加一等于几？"}`，跳过录音和 ASR，问答后播放；可选 `"speak":false` 仅返回文字，不合成/播放、不写入会话历史 |
| `POST /voice/say` | `{"text":"你好，设备语音测试。"}`，直接 TTS 播放，最多 160 字符 |
| `GET /voice/recording.wav` | 空闲时下载最后一次录音；重启或下次录音覆盖；忙碌返回 409 |

启动成功返回 202。设备繁忙、缺少配置、未联网、尚未校时或输入不适合当前操作时返回 409，并可从状态查看原因；格式错误返回 400，超大 JSON 返回 413。调试接口的调用会消耗相应云端额度并可能播放音频。

串口命令：`voice` 开始问答录音，`voice-stop` 结束，`voice-cancel` 取消，`voice-reset` 清空上下文；`status` 包含 `voice` 对象。

状态流程：`starting → recording → recognizing → thinking ↔ [reading_device] → [shortening] → synthesizing → downloading → playing → done`。异常进入 `error` 并保留 `failed_stage` 与错误码；取消进入 `cancelled`。网络阶段的取消要等待当前有界网络读写结束，TLS 建连最长约 10 秒；播放期间通常在下一个音频块停止。

状态的 `volume` 是当前音量，`default_volume` 是构建默认值，`volume_error` 单独报告音量设置失败，不覆盖问答结果。仅值变化时写入 NVS；保存失败保留原音量。音频任务在播放块之间应用变化，UI/HTTP 不直接操作正在播放的编解码器。

## 本地设备工具

`read_device_state({"fields":["battery","audio"]})` 在 badge 上执行；DeepSeek 仍在云端推理，工具结果会作为当前轮上下文发送给 DeepSeek。
仅接受 `fields` 字段及白名单中的 1–6 个不重复值，不接受任意代码、URL 或设备写操作：

| 字段 | 内容与限制 |
| --- | --- |
| `battery` | 电量估计、电压、USB 供电、充放电状态；最长样本年龄 7.5 秒，不推算准确续航 |
| `motion` | 即时三轴加速度（g）、角速度（度/秒）；近似静止时给出屏幕朝上为 0° 的倾角，运动时倾角为 null；不提供朝向南北、位置或移动历史 |
| `chip_temperature` | QMI8658 芯片内部温度（℃），最长样本年龄 5 秒；不是室温或体温 |
| `display` | 保存/实际亮度（0–255）、屏幕状态和手动熄屏标志 |
| `audio` | 音量（0–100）及静音状态 |
| `network` | Wi-Fi 是否连接和 RSSI（dBm）；不含密码、SSID、IP 等网络配置 |

传感器数据有 `status` 和 `sample_age_ms`，无效/过期测量为 null。`ok:true` 表示成功取得快照，不代表每组传感器都有效；读取时必须检查各组状态。
由主循环构造指定字段的快照，语音线程通过队列请求，不在后台线程直接访问共享 I²C；队列等待最多 2 秒，支持取消并隔离旧响应。

模型可以在一轮里同时使用云端搜索和本地读取，程序保留完整工具消息并按调用 ID 回传结果。每轮最多实际执行 4 次本地调用、6 次模型请求（含续接和缩写），模型阶段总预算 90 秒；网络阻塞按当前有界超时退出。
未知工具、无效字段、超时等以 `ok:false` 和错误码返回，模型不能把失败当作测量值。工具中间文字不朗读，最终回答仍最多 160 字符。

`GET /voice` / `/status.voice` 增加 `local_tool_calls`、`last_tool`、`last_tool_result`，后者只保留最近一次工具结果。屏幕短暂显示 `READING DEVICE`，BOOT 可取消当前轮，PWR 手动熄屏行为保持不变。

可以直接问：“还有多少电？”、“现在音量多少？”、“设备倾斜了吗？”、“芯片温度多少？”、“Wi-Fi 信号好吗？”。
设备没有环境温湿度、定位或指南针能力。

修改工具同样在主循环执行，不允许模型访问任意寄存器、路径或代码：

| 工具 | 参数 | 行为 |
| --- | --- | --- |
| `set_volume` | `{"percent":30}`，整数 0–100 | 先保存 NVS，成功后应用；0 静音 |
| `set_brightness` | `{"percent":50}`，整数 1–100 | 换算成 0–255 并保存，50% 对应 128；低电保护仍限制有效亮度 |
| `schedule_shutdown` | `{"delay_seconds":10}`，仅支持 10 | 等本轮回答成功播完后开始 10 秒倒计时；静默请求则等待文字回答完成 |

可说“亮度调到百分之五十”“音量调小一点”“十秒后关机”。未指定幅度的相对设置先读取当前值，再增减 10 个百分点。
只有用户明确的操作请求才调用修改工具；失败不能声称成功。排队请求取消或超时后不会补执行；已开始保存的设置不能因对话取消而回滚，结果不确定时返回 `operation_outcome_unknown`，应先读取确认，不盲目重试。
关机计划只在内存中保存，绑定本轮对话；回答/播放失败、取消或新一轮对话都会撤销。收到成功工具结果且本轮正常结束后才开始倒计时，重复调度不延长计时。
屏幕显示 `POWER OFF IN 10` 并逐秒更新，BOOT 或 `Cancel shutdown` 按钮可取消（等待回答时也有效）。PWR 仍只控制屏幕；取消关机接口为 `POST /shutdown/cancel`，状态见 `/status.shutdown`。
到期通过 AXP2101 的关机寄存器请求断电，不重启、不进入屏保。写入失败或 2 秒后仍在运行会显示失败，不自动重复关机；USB 连接时的实际断电/再上电表现需实机体验确认。

静默联调：`uv run validation/device_tools.py http://DEVICE_IP`，增加 `--search` 同时验证服务端搜索。会调用 DeepSeek 并唤醒页面，但不录音、不合成、不播放、不修改音量亮度、不写入对话历史。
设置联调：`uv run validation/device_settings.py http://DEVICE_IP`，短暂更改并恢复音量/亮度，验证关机倒计时后立即取消，不测试实际切断电源。

## 资源与传输

- 录音：16 kHz / PCM16 / 单声道 WAV，30 秒约 960 KB，存 PSRAM，不写 Flash；采集时暂存双通道，30 秒峰值约 1.92 MB，结束后原地压缩为单声道。
- 忽略 ADC 启动约 200 ms 的瞬态，再按整段去直流 RMS 选择较强通道，整段保持一致，避免启动脉冲误选静音通道、反相相加或逐块切换造成失真。`mic_channel` / `mic_channel_rms` 用于检查选择结果。
- 屏保与语音任务共享一个音频硬件实例及互斥锁。语音任务持有音频期间，屏保采音暂停；结束后释放。
- ES7210/ES8311 共享 I2S 时钟，统一为 16 kHz、双槽 16 位；单声道 PCM 播放时复制到两个槽。
- HTTPS 使用 ESP-IDF CA 证书包验证服务端证书，未校时不发起云端请求；不使用 `setInsecure`。
- ASR multipart 分块上传 WAV。DeepSeek 使用 Anthropic 兼容接口的非流式消息响应，搜索由服务端执行；暂停任务最多续接两次。
- TTS 请求 WAV 下载地址，完整下载到 PSRAM（上限 3 MiB），校验格式/边界后播放。此版没有 MP3 解码，也不是边合成边播放。
- 云端请求在独立任务运行，屏幕、HTTP、触摸和电池轮询继续工作。网络错误不会自动重试。
- JSON 优先在 PSRAM 分配，避免搜索结果占满内部 SRAM。较长回复、断网、额度不足、证书验证失败都有明确失败状态。

## 验证

```sh
clang++ -std=c++17 -Wall -Wextra -Werror validation/voice_protocol.cpp -o /tmp/badge-voice-protocol
/tmp/badge-voice-protocol
uv run validation/test_voice_config.py
bash scripts/build.sh

# 下列实机会录音/播放或调用云端，请按需执行，不要与续航测试同时运行。
uv run validation/device_voice.py http://DEVICE_IP --say '你好，设备语音测试。'
uv run validation/device_voice.py http://DEVICE_IP --ask '一加一等于几？'
uv run validation/device_voice.py http://DEVICE_IP --loopback 2
uv run validation/device_voice.py http://DEVICE_IP --cancel
```

脚本核对设备身份，保存阶段/耗时结果到 `validation-output/voice/`，验证任务结束后音频所有权释放、屏保恢复。软件播放成功不等同于扬声器听感验证，触摸按钮和实际说话识别仍需人工体验。

2026-09-18：用户已确认 0.6.0 真人录音问答正常。0.6.1 编译、USB 写入及校验通过；实机检查音量 0/100/50 设置与读取、非法值拒绝、硬件重启后保留 50 均通过，结束恢复至 60。此次未自动录音或播放；音量按钮触摸与播放中调节的听感待人工体验。
