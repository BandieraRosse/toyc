# Lenovo Legion R7000 2021 设备配置

## 已知信息

**声卡：**
- Card 0: NVIDIA GPU → 4× HDMI 输出 (C0D3p, C0D7p, C0D8p, C0D9p)
- Card 1: AMD Audio CoProcessor → **模拟输出**（旧 alsa_devices 只查 controlC0，看不到）

**键盘：**
- AT Translated Set 2 keyboard (内置, event3)
- ITE Tech. Inc. ITE Device(8910) Keyboard (USB, event10)
- INSTANT USB GAMING MOUSE Keyboard (游戏鼠标上的键盘接口, event9)

**鼠标：**
- INSTANT USB GAMING MOUSE (USB 游戏鼠标, event8)
- ELAN06FA:00 04F3:31DD Mouse (触摸板, event17)
- ELAN06FA:00 04F3:31DD Touchpad (触摸板, event18)

## 对比新扫描器

新 `tlibc_audio_scan` 会扫描 `/dev/snd/controlC0` 和 `/dev/snd/controlC1`，
因此能发现 card 1 的 AMD 模拟输出，评分会高于 HDMI。

## 待验证

在 Legion 上运行以下命令并提交结果：
```sh
build/device_snapshot > device-Legion-R70002021.txt
```
