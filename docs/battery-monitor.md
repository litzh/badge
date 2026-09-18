# 屏保与电池曲线

## 10 秒屏保

固件在最后一次交互的 10 秒后进入屏保。v0.4.0 使用麦克风、IMU 和温度驱动的
[80 粒子动画](visualizer.md)；v0.3.0 的 7 个移动光点仍作为内存不足时的降级画面。
没有常亮文字或边框，亮度临时限制为
`min(正常亮度, 128)`（v0.4.2），唤醒恢复原值；不反复写 NVS。
v0.5.0 限制屏保最多显示 5 分钟，随后面板休眠并停止采音/动画，查询接口仍在线。
低电保护与电量显示见 [电池管理](power-management.md)。

触摸、BOOT 按下、PWR 短按、成功的 `/echo` 或亮度写入、BLE 配网提交以及串口
`provision` 都会重置计时并唤醒。PWR 短按继续循环亮度，BOOT 长按继续配网。
HTTP `/status`、`/display`、`/visualizer`、串口 `status`、后台传感器更新与网络重连均不唤醒。
因此持续查询电池不会使屏幕停留在静态状态页。

此功能降低固定像素长期高亮的风险，不能保证消除 OLED 老化。
Wi-Fi、HTTP、触摸与传感器继续工作，**这不是 ESP32 深度睡眠模式**。

## 开始记录

电脑和 badge 连接同一局域网，地址取屏幕上的 IP。先安装 uv，无需手动安装 Python 库。
在 `badge` 目录运行（把 `DEVICE_IP` 替换为实际 IP）：

```sh
uv run scripts/battery_monitor.py http://DEVICE_IP --label '充电测试' --open
```

默认每 10 秒查询一次 `/status`，单次超时 3 秒，持续运行直到 Ctrl+C 或 SIGTERM。
请求绕过系统代理；仅查询接口，不发送 echo、不调亮度、不唤醒屏幕。
旧固件也能记录电压，新增字段显示为未知。

默认每次创建 `recordings/日期-时间/`，也可以指定一个**尚不存在**的目录：

```sh
uv run scripts/battery_monitor.py http://DEVICE_IP \
  --label '待机续航' --interval 10 --output recordings/standby-01 --open
```

自动记录 2 小时：

```sh
uv run scripts/battery_monitor.py http://DEVICE_IP --duration 7200 --label '充电两小时' --open
```

也可以设置 `BADGE_DEVICE=http://DEVICE_IP`，后续省略设备地址。
已有输出目录会被拒绝，避免覆盖之前的实验。

## 输出与曲线

- `samples.csv`：UTC 时间、经过秒数、电压、PMU 电量估计、USB/充放电状态、
  屏保、正常/实际亮度、设备 uptime、RSSI、请求耗时、故障与重启标记。
  v0.5.0 增加 `screen_off`、`display_mode`、`battery_low`、`shutdown_pending`、`shutdown_status`，
  用于区分熄屏与屏保、检查保护前状态；旧 CSV 仍可重绘，缺失字段显示未知。
- `raw.jsonl`：每次采样及设备原始状态 JSON，保留无法解码或未知字段的排查线索；
  请求失败保留错误说明，原始状态为 null。
- `battery.html`：完全本地、无 CDN 依赖的曲线，运行中每 10 秒自动刷新，鼠标悬停查看数据。
  电压与电量分别绘图，充电/放电/电池待机用不同颜色。停止后成为静态报告。
- `session.json`：本次设备地址、标签、开始时间及采样参数。

每条记录落盘并 fsync，HTML 采用临时文件替换。程序异常退出或断电后仍可以用 CSV 重建：

```sh
uv run scripts/battery_monitor.py --plot recordings/standby-01/samples.csv --open
```

生成 `samples-report.html`。如果原采样间隔不是 10 秒，重绘时传相同的 `--interval`，
用于识别采样间断。曲线缺口表示请求失败、缺失/过期电池数据、设备 uptime 回退或采样间断，
不会补成 0V 或连成虚假的连续曲线。读取失败后按原间隔继续重试。

## 充电和续航实验

1. **充电**：接好电池和 USB 电源后开始记录，观察电压、PMU 百分比及充电状态变化。
   `vbus_present=true` 只表示外部供电，不代表电池正在充电；以 `power_state` 为准。
2. **续航**：先充电，再拔掉 badge 的 USB，保持 Wi-Fi 可达，确认记录为外部供电断开和放电。
   放置设备至少 10 秒，让 `screensaver=true`，避免频繁触摸改变功耗。
3. **电脑保持唤醒**：macOS 可用以下命令防止采集期间自动睡眠；不要合盖：

   ```sh
   caffeinate -i uv run scripts/battery_monitor.py http://DEVICE_IP --label '待机续航' --open
   ```

4. 断连后程序仍会记录并重试。最后一次在线和第一次离线可作为人工检查的时间参考，
   **不能自动认定为电池耗尽**，也可能是路由器、网络或设备重启。

测得的是“Wi-Fi 在线 + 动态屏保 + 持续轮询”的续航，不是无联网/关屏/深睡续航。
这是 v0.4.2 的测试条件；v0.5.0 会在 5 分钟屏保后熄屏，报告时应根据 `display_mode`
区分实际测试阶段，不能与旧版持续动态屏保直接等同。
v0.4.0 新增持续麦克风采样和全屏粒子渲染，v0.4.2 将屏保亮度上限由 24 提高至 128，
功耗条件与 v0.3.0 不同；比较续航时
请保持固件版本、亮度和声音环境一致，不能把原有测试结果直接视作新版续航。
电压不能直接换算剩余电量；PMU 百分比为未独立校准的估算。
本程序没有实测电流数据，不输出伪精确的 mA、mAh 或充电功率。
真实充满和续航时长需要一次完整实验才能得出。

首轮 [完整周期分析](battery-analysis-20260918/analysis.md) 已保留。
可用以下命令重新生成分析文件（只读原始记录，绘图依赖由 uv 管理）：

```sh
uv run scripts/analyze_battery.py recordings/20260917-180915-893121/samples.csv \
  --output docs/battery-analysis-20260918
```

## 开发验证

```sh
bash scripts/build.sh
c++ -std=c++11 validation/screensaver.cpp -o /tmp/badge-screensaver-test
/tmp/badge-screensaver-test
uv run validation/test_battery_monitor.py
```

本机验证覆盖 10 秒计时、唤醒与亮度恢复、毫秒回绕、空/失败/过期电池数据、
电量 0%、重启检测、请求失败后恢复、CSV/JSONL 落盘、历史曲线重建和防止覆盖。
采集集成测试使用本机回环 HTTP 服务，无需开发板。
实机需再检查 10 秒自动进入屏保、连续 GET 不唤醒、触摸和实体按键唤醒以及实际动画外观。

自动化实机接口检查（会清空 echo、临时调低亮度，最后恢复原亮度并进入屏保）：

```sh
uv run validation/device_screensaver.py http://DEVICE_IP
```

### 2026-09-17 验证记录

- 使用仓库本地 `.env` 的 Wi-Fi 值构建并刷入 v0.3.0。该文件使用旧的
  `RLCD_WIFI_SSID` / `RLCD_WIFI_PASSWORD` 名称，本次构建映射到 `BADGE_WIFI_*`；
  当时 `scripts/build.sh` 尚未自动加载 `.env`（v0.4.0 已加入自动加载和旧变量兼容）。
- Arduino Core 3.3.2-cn 构建通过，应用 1,277,903 字节（40%），静态 RAM 55,776 字节（17%），
  USB 写入哈希校验通过。设备联网地址为 `192.168.8.121`。
- 实机验证 echo 唤醒、10 秒自动进入屏保、连续 `/status` / `/display` 查询不唤醒、
  写入亮度唤醒、正常亮度低于 24 时屏保不提高亮度。结束恢复保存亮度 255，屏保实际亮度 24。
- 采集程序完成 45 秒实机联调，保存 9 个样本、CSV、JSONL 和 HTML 报告；
  本轮含亮度测试，标记为“实机联调（非续航实验）”，不能用来报告续航时长。
- 读数约 4.116–4.120V，PMU 电量估计 100%，有效 USB 输入，电池状态 standby。
  尚未进行完整充电/放电实验，也未人工确认触摸/按键唤醒和屏保实物观感。
- 本地计时测试和 4 项 Python 测试通过；浏览器工具拒绝访问本地文件 URL，
  HTML 已完成生成、脚本语法和结构检查，未取得浏览器截图验证。
