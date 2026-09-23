# Architectural Environment V1 生成与截图

> 状态：当前操作指南

套件尺寸、端口和表面规范见[建筑套件合同](../reference/architectural-environment-v1.md)，原型及接力现场见[历史设计记录](../archive/architectural-environment-v1-design-record.md)。

## 套件复现与离屏截图

下列命令用于套件生成与离屏诊断。Windows 原生构建、实机 GPU 与 package 验收按
[Windows Native 指南](windows-native.md)执行；离屏截图不能代替这些签收。

```sh
make rasterfall
python3 tools/architecture_round.py --generate --capture --deterministic
make test-asset-pipeline
build/rasterfall --logic-test
build/rasterfall --help
git diff --check
```

脚本只编排现有 Blender/importer/Visual CLI，并使用 Pillow 拼图，不引入第二套资产 IR。
首次生成构建转换器一次，后续 importer 使用已有 `--no-build`；不要让多个 Linux make 同时
操作同一个 build 目录。`--generate` 显式重建/安装当前 suite，旧探索应使用另一个 output 目录。
GLB 两次独立生成、原型两次渲染逐字节比较；Blend 自身不承诺字节确定性。

默认 `tmp/architecture-v1/` 保留源 Blend/GLB、两轮 GLB、逐件四视图、原型 BMP/PNG、
组图、import 日志与 runtime SHA256。源 GLB 和 Blend 同时保存在本地 industrial source 目录；
公开的 generator/manifest 和 RMESH 足以重建，私有目录与 tmp 不提交。
既有模型预览会自动对每件归一构图，只有组合原型提供真实尺度；不得把逐件预览当通行验证。
