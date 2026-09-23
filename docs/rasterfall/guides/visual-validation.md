# 视觉验收

> 状态：当前
> 所有者：Rasterfall 固定场景、截图和人工视觉复核
> 最近核对：2026-09-23
> 事实入口：`build/rasterfall --help`、`src/dev-tests/rasterfall_visual_capture.inc`

视觉验收使用固定输入、固定 camera 和正常 renderer 路径生成 BMP，再按任务查看原图或组图。CLI 的完整
参数始终以当前 executable 的 `--help` 为准；本页只维护工作流和输出合同。

## 基本流程

```powershell
.\windows\NativeCodex.ps1 build
.\windows\NativeCodex.ps1 test
.\windows\NativeCodex.ps1 run --help
```

在 Linux/self 辅助环境也可先 `make rasterfall`，但 Windows native GPU、presenter 或性能结论仍按对应
GPU 指南验收。capture 应写入新建的 `tmp/` 子目录；生成物不提交仓库。

固定 capture 的数据流为 options → main 诊断早退 → named fixture/fixed setup → 正常 renderer/helper →
`toy_renderer_flush()` → BMP dump。fixture 不建立第二套角色、地图、光照或材质实现。

## 通用与环境场景

Desktop/Application 原型只在隔离 fixture 中验收；先运行 `build/rasterfall --logic-test`，再用
`build/rasterfall --visual-capture desktop-v1 --visual-output <bmp>` 检查固定画面。
正常运行的 feature gate 与数据所有权见 [Application Runtime](../architecture/application-runtime.md)。

```text
--visual-capture <scene> --visual-output <bmp>
--environment-capture <output-dir> --textures
```

常用 named scene 包括 architectural family/alley/hall、campus corner/asset、procedural humanoid、Hurd squad
和 lighting props。建筑、校园和环境 scene 的具体名称以 `--help` 为准；环境 capture 加载正式 Campaign，
复用 normal scene、actor、flag、depth 和固定 seed，但不推进 simulation。

组图入口按专题使用 `tools/architecture_round.py --capture`、`tools/environment_sheet.py`。组图只拼接
原始 BMP，不改变渲染路径；像素异常必须回到原图判断。

## 角色验收

```text
--character-acceptance <model> <output-dir>
--character-world-capture <output-dir> [--character-world-model <model>]
--squad-acceptance <model-dir> <output-dir>
--rigid-attachment-acceptance <model-dir> <output-dir>
--eula-animation-acceptance <model-dir> <output-dir>
```

- Character acceptance：固定 bind/rifle idle/rifle aim、多视角和 near/mid/far，并包含共享 resource 的
  two-instance isolation。legacy calibration 仅验证旧 carrier，不代表 modular teammate 的握持来源。
- World capture：加载正式地图和 Character Test Strip，生成 near/mid/far、old/idle/aim/motion，另保留
  edge-entry 与 near-crossing 原图验证组合 bounds、视口边缘和近裁剪。
- Squad acceptance：两个四人 roster、三视角、共享 body/gear 与独立 instance，验证 palette isolation、
  attachment transform 和 instance ownership。
- Rigid attachment：共享 humanoid 与附件 resource 的 bind/turned instance，验证 full transform/socket。
- Eula animation：比较 Full、LOD1、compact LOD2、Hybrid 的固定动作与 deformation pose。

角色组图使用 `tools/character_lab_sheet.py`、`tools/character_world_sheet.py` 和
`tools/eula_animation_acceptance_sheet.py`。private asset 缺失必须明确报告 SKIP，不能换用不同 carrier
后仍宣称同一资产通过。

## 判定方法

同一 build、fixture 和输入的确定性 capture 可以用 `cmp` 做字节回归；但字节一致只证明输出未变化，
不能代替人工判断目标是否正确。人工复核至少观察：

- near/mid/far 的轮廓、遮挡、材质和可读性；
- front/three-quarter/side/back 的朝向、pivot、attachment 和武器握持；
- idle/move/fire/reload/hit、上下身组合和循环相位；
- edge-entry、near-crossing、thin geometry 与透明边缘；
- world light、form light、character material floor 和 normal fog-free 画面；
- HUD、viewmodel、world effects 与 overlay 的层序和 coverage。

修改并行录制时同时比较单 worker/多 worker 的 BMP、depth/hash 和统计。修改 GPU consumer 时先跑相关
CPU/GPU differential，再按 [GPU 验收与诊断](gpu-validation.md) 扩大到 native capture/acceptance。

## 证据记录

报告 build/package identity、完整命令、fixture、输入资产、输出目录和人工结论；若使用组图，同时保留
原始 BMP。截图只证明所覆盖的固定姿态与视角，不把单张图片外推为所有动画、距离或运行时生命周期通过。
