# 验证

以下命令在 badge 根目录运行。Python 统一通过 uv 执行。

## 无需设备

```sh
c++ -std=c++11 validation/screensaver.cpp -o /tmp/badge-screensaver-test
/tmp/badge-screensaver-test
c++ -std=c++11 validation/display_frame.cpp -o /tmp/badge-display-test
/tmp/badge-display-test
uv run validation/render_frame.py
uv run validation/test_battery_monitor.py
bash -n scripts/build.sh scripts/setup_native_ctags.sh
git diff --check
```

- `screensaver.cpp`：10 秒计时、亮度限制、唤醒恢复、毫秒回绕。
- `display_frame.cpp`：全帧和奇数高度条带覆盖、源数据偏移及每次写入大小。
- `render_frame.py`：编译执行 `visualizer.cpp`，测试音频频段、直流抑制、反相双麦、
  长时间粒子运动、边界及缓冲区哨兵；输出 `validation-output/lumina.png`。
- `test_battery_monitor.py`：4 项测试，包含本机回环 HTTP 服务；验证失败恢复、
  有效电量 0%、重启识别、转义、数据落盘、重绘及防止覆盖。

## 编译和实机

```sh
bash scripts/build.sh
uv run validation/device_visualizer.py http://DEVICE_IP
uv run validation/device_screensaver.py http://DEVICE_IP
uv run --with pyserial validation/read_serial_status.py /dev/cu.usbmodemXXXX
```

烧录步骤见 [构建文档](../docs/build.md)。两个实机测试应顺序运行：会清空 echo、
唤醒设备，亮度测试会临时改变亮度后恢复；结束保持屏保。不要在续航采集期间运行。
串口脚本只读取状态，输出中过滤 Wi-Fi 名称和完整配置。

软件帧数、非黑像素统计和离线图像不能替代实际面板检查。目视确认：
文字更新不闪烁、静置 10 秒可见粒子、触摸唤醒、声音与倾斜影响画面。
实体 BOOT/PWR 动作需人工检查，温度和 PMU 电量不是经过独立仪器校准的测量。
