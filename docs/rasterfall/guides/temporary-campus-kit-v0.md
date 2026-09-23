# Temporary Campus Kit V0 生成与验收

> 状态：当前操作指南

资产与拼接规格见[参考合同](../reference/temporary-campus-kit-v0.md)；首次清点和合成场景审阅见[历史记录](../archive/temporary-campus-kit-v0.md)。

```sh
make rasterfall
python3 tools/campus_kit_round.py --generate --capture --deterministic --audit
make test-asset-pipeline
build/rasterfall --logic-test
build/rasterfall --help
git diff --check
```

`tools/campus_kit_round.py` 先构建转换器一次，再以 importer `--no-build` 导入；避免并发运行多个 make。`--generate` 两次独立生成 GLB，`--deterministic` 对新增截图逐字节比较，Blend 不承诺字节确定性。完整性检查覆盖 manifest、RMESH/纹理引用、v2 布局、索引、量化边界与面数预算。

输出在 `tmp/campus-kit-v0/`：`kit-sheet.png` 为 12 件独立视图；`campus-corner.png` 和 `campus-ground.png` 是组合场景；`existing-assets.png`、`existing-architecture.png` 用于已有库存审阅；`integrity-and-fingerprints.json` 记录指纹与实际拼接边界。同目录保留 capture、构建、导入、测试和 CLI 日志。逐件预览使用归一取景，必须结合真实尺度组合场景审阅。合成街角不能充当真实校园拓扑或性能签收。Windows 原生验收按 [Windows Native 指南](windows-native.md)执行。
