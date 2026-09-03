# 资源、模型与动画

> 文档更新：2026-09-03
> 源码核对基线：`75a10cd`（将项目协作说明转向 Rasterfall）

## 运行时资源

公开资源位于 `rasterfall/assets/{maps,textures,audio,models}`。Linux 默认从文件系统读取，内嵌目标
通过 `scripts/embed-assets.py` 生成资源对象；Windows 包以 EXE 目录为资源根。统一加载 API 在
`include/toy_assets.h` / `lib/assets.c`，因此路径或打包问题先区分“资源不存在”和“格式解析失败”。

## 模型与动画模块

- `rasterfall_model.c` / `.h`：RMESH/RFM2 加载、材质/蒙皮数据、骨骼层级、姿态采样、IK、grant、
  root motion、附件变换和诊断。它是模型运行时的主要状态所有者。
- `rasterfall_vmd.c` / `.h`：VMD 读取、骨骼映射、关键帧转换和诊断。
- `rasterfall_humanoid_basis.c`、`rasterfall_humanoid_retarget.c`：人形静止基底、解剖验证和跨骨架旋转重定向。
- `rasterfall_animation.h`：通用 clip/track/player 数据和采样辅助。
- `rasterfall_actor_animation.h`、`rasterfall_animation_composition.h`：玩法动作到角色姿态、持枪和叠加规则。
- `rasterfall_character.c`：actor/class 到角色资产选择；实际加载与绘制在 render。
- `app/glb_inspect.c`：既是 GLB 检查器，也以 `RASTERFALL_GLB_LIBRARY` 编入游戏提供 GLB 动画加载。

完整求值顺序、格式扩展点和回归要求见
[`animation-architecture.md`](animation-architecture.md)。

## 工具链定位

- GLB 转 RMESH：`app/glb2rmesh.c`；GLB 检查：`app/glb_inspect.c`。
- PMX 转换：`app/pmx2rmesh.c`、`tools/import-pmx-model.sh`。
- Blender 角色导出：`tools/blender/export_rasterfall_character.py`。
- LOD：`tools/rmesh_lod.py` 和 Makefile 的 `lod-*` 目标。
- 资源许可与发布边界：`asset-sources.md`；历史实验现场仅在 `archive/` 中追溯。

## 修改提示

坐标系、单位和 bind pose 问题经常跨越转换器、模型加载、动画采样和渲染。先确定错误首次出现在哪个
阶段，不要只用渲染补偿掩盖资产空间问题。新增骨骼字段或动画语义时，同时核对 CPU 蒙皮、IK/grant、
附件、LOD 模型和诊断工具；纯外观校准优先进入 calibration/profile，而不是污染玩法状态。
