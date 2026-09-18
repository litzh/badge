# 设备语音问答

`badge-0.6.1`：ES7210 录音 → MiniMax `asr-1.0` → DeepSeek `deepseek-flash`（服务端搜索）→ MiniMax `speech-2.8-turbo` → ES8311 扬声器播放。设备直接通过 Wi-Fi 访问服务，无需电脑中转。

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

- 屏保或熄屏时，第一次触摸仅唤醒。
- 点击 `[-] Volume: 60 [+]` 两侧按钮调整音量，每次 10，范围 0–100，0 为静音；播放时也可调整，不会取消当前问答。音量立即保存至 NVS，重启及普通固件更新后保留。
- 正常状态页底部 `Tap: ask a question` 区域：点击开始录音，再次点击结束并提交；到达录音上限自动提交。
- 识别、思考、合成、下载或播放期间，底部显示取消操作。点击后取消当前轮，不会自动开始下一轮。
- 录音与播放互斥。对话期间保持屏幕唤醒，结束后重新开始待机计时。
- BOOT 长按 3 秒配网、PWR 亮度/电源操作和电池保护保持原有行为。
- 保护关机条件成立时会取消当前语音任务。

状态页使用现有 ASCII 字体显示阶段；中文问题和回答可通过 `GET /voice` 查看。长按或滑动不会反复触发录音按钮，每次新的按下才触发动作。

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
| `POST /voice/ask` | `{"text":"一加一等于几？"}`，跳过录音和 ASR，问答后播放 |
| `POST /voice/say` | `{"text":"你好，设备语音测试。"}`，直接 TTS 播放，最多 160 字符 |
| `GET /voice/recording.wav` | 空闲时下载最后一次录音；重启或下次录音覆盖；忙碌返回 409 |

启动成功返回 202。设备繁忙、缺少配置、未联网、尚未校时或输入不适合当前操作时返回 409，并可从状态查看原因；格式错误返回 400，超大 JSON 返回 413。调试接口的调用会消耗相应云端额度并可能播放音频。

串口命令：`voice` 开始问答录音，`voice-stop` 结束，`voice-cancel` 取消，`voice-reset` 清空上下文；`status` 包含 `voice` 对象。

状态流程：`starting → recording → recognizing → thinking → [shortening] → synthesizing → downloading → playing → done`。异常进入 `error` 并保留 `failed_stage` 与错误码；取消进入 `cancelled`。网络阶段的取消要等待当前有界网络读写结束，TLS 建连最长约 10 秒；播放期间通常在下一个音频块停止。

状态的 `volume` 是当前音量，`default_volume` 是构建默认值，`volume_error` 单独报告音量设置失败，不覆盖问答结果。仅值变化时写入 NVS；保存失败保留原音量。音频任务在播放块之间应用变化，UI/HTTP 不直接操作正在播放的编解码器。

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
