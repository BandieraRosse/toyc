# 设备表 — Device Tables

本目录存储各台机器的设备配置快照，用于验证 `tlibc_audio_scan()` 的设备发现和自动选择逻辑。

## 格式

文件名: `<hostname>.txt`

使用 `build/device_snapshot` 工具生成：

```sh
build/device_snapshot > compiler-tests/device-tables/$(hostname).txt
```

## 当前设备表

| 文件 | 机器 | 音频设备 | 键盘 | 鼠标 |
|------|------|----------|------|------|
| lu-SFA16-71.txt | SFA-16 (本机) | HDA Intel PCH ALC269VC Analog | YICHIP Wireless | YICHIP Wireless Mouse |

## 新增设备表

在其他机器上运行：

```sh
git clone <repo> && cd toyc
make lib && make app-audio/device_snapshot 2>/dev/null
# 注意：build/device_snapshot 需要运行
build/device_snapshot > device-$(hostname).txt
```

将生成的 `.txt` 文件放入本目录并提交 PR。

## 验证方法

目前为手动对照。未来可添加自动测试：

```sh
# 检查当前系统配置是否与已知快照一致（TODO）
# 把 device_snapshot 输出与对应 hostname 的快照对比
```
