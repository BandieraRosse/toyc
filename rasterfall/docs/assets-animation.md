# 资源、模型与动画

> 文档更新：2026-09-10
> 源码核对基线：工作区（V2 final convergence、CHR1 socket 的 identity-rest bind 烘焙、RFCHAR 双手握持；Eula actor walk clip lazy-load；PMX compatibility path）

新建或生成 Blender 人形资产必须先读 [`character-assets.md`](character-assets.md)。它冻结
Blender source → Character GLB → importer → runtime character asset → humanoid animation 主线；
本文继续说明既有运行时模块。

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
- `rasterfall_prop.h` / `rasterfall_prop.c`：静态 prop asset profile、分类 RMESH 路径与 `512/232`
  presentation 缩放；`rasterfall_render_static_prop()` 负责实例变换和共享模型提交，地图 parser
  独立消费 RFU 碰撞 profile 生成 gameplay primitive。
- `app/glb_inspect.c`：既是 GLB 检查器，也以 `RASTERFALL_GLB_LIBRARY` 编入游戏提供 GLB 动画加载。

完整求值顺序、格式扩展点和回归要求见
[`animation-architecture.md`](animation-architecture.md)。

VMD/PMX 开发者预览属于旧资产兼容路径，仅在显式传入 `rasterfall` 的 legacy VMD
参数时启用；程序正常启动不显示 Eula 的私有 VMD 开发者预览。正式 Eula gameplay
actor 若存在，会按角色 profile 懒加载 walk clip 作为移动表现。新角色优先走
RFCHAR GLB → RFM2 v14 → stable role/attachment API，并使用角色验收和世界截图入口观察。

## 工具链定位

- 统一离线入口、manifest 与完整性验证：`tools/assets/import_asset.py`；runtime 不读取 manifest。
  RFCHAR importer 的附件位置/旋转必须转换到 SKN1 identity-rest 基底，不能直接保存 GLB local TRS；
  `test_rfchar_pipeline.py` 校验附件全局 bind 变换。持枪仍从稳定角色/附件 API 获取接触位置。
- 静态 GLB 转 RMESH：`app/glb2rmesh.c`；skeletal RFCHAR 转换：
  `tools/assets/rfchar_import.py`；GLB 检查：`app/glb_inspect.c`。
- PMX 转换：`app/pmx2rmesh.c`、`tools/import-pmx-model.sh`。
- Blender 角色导出：新资产遵循 `character-assets.md` 输出 canonical GLB；RF Humanoid V1.1
  的对照生成器为 `tools/blender/generate_rasterfall_humanoid.py`，V2 body 生成器为
  `tools/blender/generate_rasterfall_humanoid_v2.py`；现有
  `tools/blender/export_rasterfall_character.py` 是 Mixamo FBX→PMX/RFM2 兼容桥，不是新标准。
- 官方正向 fixture 为 `tools/blender/generate_rfchar_fixture.py`，runtime 验证器为
  `app/rfchar_runtime_test.c`；新资产使用 stable role/attachment API，历史资产才按骨名推断。
- LOD：统一入口编排 `tools/rmesh_lod.py`；Makefile 的既有 `lod-*` 目标继续可用。
- 资源许可与发布边界：`asset-sources.md`；历史实验现场仅在 `archive/` 中追溯。

## 修改提示

坐标系、单位和 bind pose 问题经常跨越转换器、模型加载、动画采样和渲染。先确定错误首次出现在哪个
阶段，不要只用渲染补偿掩盖资产空间问题。新增骨骼字段或动画语义时，同时核对 CPU 蒙皮、IK/grant、
附件、LOD 模型和诊断工具；纯外观校准优先进入 calibration/profile，而不是污染玩法状态。
