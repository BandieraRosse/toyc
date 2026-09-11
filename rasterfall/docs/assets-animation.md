# 资源、模型与动画

> 文档更新：2026-09-11
> 源码核对基线：工作区（Enemy Visual V2 六份公开 RFM2 / renderer-only family；Humanoid Action Composition V1.1 additive recoil；Model Resource / Model Instance V1；双手 RFANIM 持枪轨道；PRIMARY_GRIP weapon presentation；modular world strip）

新建或生成 Blender 人形资产必须先读 [`character-assets.md`](character-assets.md)。它冻结
Blender source → Character GLB → importer → runtime character asset → humanoid animation 主线；
本文继续说明既有运行时模块。

Enemy Visual V2 六份感染模型复用本页的 resource/instance 与 RFCHAR runtime。敌人 family、
轻量位移相位和基础站立/步姿仅属于 renderer；完整入口见 [enemy-visuals.md](enemy-visuals.md)。

## 运行时资源

公开资源位于 `rasterfall/assets/{maps,textures,audio,models}`。Linux 默认从文件系统读取，内嵌目标
通过 `scripts/embed-assets.py` 生成资源对象；Windows 包以 EXE 目录为资源根。统一加载 API 在
`include/toy_assets.h` / `lib/assets.c`，因此路径或打包问题先区分“资源不存在”和“格式解析失败”。

## 模型与动画模块

- `rasterfall_model.c` / `.h`：RMESH/RFM2 加载、材质/蒙皮数据、骨骼层级、姿态采样、IK、grant、
  root motion、附件变换和诊断。正式 RFCHAR 路径以 `rasterfall_model_resource` 独占 backing、纹理、
  mesh、静态 skeleton/IK/grant 与 CHR1 定义；每个 `rasterfall_model_instance` 独占骨骼局部姿态、
  global transforms、root motion、solver history/cache 与 attachment IK pole 状态。
- `rasterfall_vmd.c` / `.h`：VMD 读取、骨骼映射、关键帧转换和诊断。
- `rasterfall_humanoid_basis.c`、`rasterfall_humanoid_retarget.c`：人形静止基底、解剖验证和跨骨架旋转重定向。
- `rasterfall_animation.h`：通用 clip/track/player 数据和采样辅助。
- `rasterfall_action.h` / `.c`：RFANIM V1 的拥有者，保存 gameplay semantic action ID、
  `RF_HUMANOID_V1` 兼容标记、时长、循环、stable humanoid role track、关键帧和 step/linear 插值；
  `rasterfall_action_compose()` 固定按 LOWER_BODY、UPPER_BODY、ADDITIVE 层采样；lower 只写
  root/hips/legs，upper 只写 spine 到双手；RIFLE_IDLE/AIM/FIRE 必须包含左右手轨道，
  RIFLE_RECOIL 只写 spine/chest、双肩和双臂的局部旋转 delta。
  组合器 reset 一次、逐层写局部旋转、最终统一更新 bones，
  `model_instance` 仍是唯一最终 pose owner。
- `rasterfall_actor_animation.h`、`rasterfall_animation_composition.h`：玩法动作到角色姿态、持枪和叠加规则。
- `rasterfall_character.c`：actor/class 到角色资产选择；实际加载与绘制在 render。modular recipe
  的 HEAD/CHEST/BACK/HIP attachments 是被动 equipment；active weapon 不进入 recipe attachment 绘制，
  而由 finalized instance `WEAPON_R` 对齐 authored `PRIMARY_GRIP`，不读取 `pose_calibration_local`，
  再执行模块化左臂的 FOREGRIP attachment IK。旧 PMX/VMD 的 CHEST/校准路径仍仅用于兼容诊断。
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

`rasterfall_model_asset` 暂时保留为 evaluator/inspector compatibility view。resource 内的一份 asset
是加载后冻结的定义；instance 内的 asset 是现有求值器的逐实例 pose view，静态 backing 指针指回
resource。新 runtime 不得对 resource definition 调用 pose/IK API，且 resource 必须晚于全部 instance
释放。CPU skinning 与 stable socket 分别通过 instance API 查询。

## 工具链定位

- `build/rf_anim_info <action.rfanim>`（也可用 `build/rasterfall --action-info`）输出动作、兼容骨架、
  track、插值和全部关键帧。
- `build/rasterfall --action-preview <model.rmesh> <action.rfanim> <time-ms> <output.bmp>` 使用固定相机、
  Lighting V1、CPU skinning 和武器 socket 生成确定性 BMP。
- `build/rasterfall --pose-debug <model.rmesh> <action.rfanim> <time-ms> <humanoid-role>` 输出 stable bone ID、
  finalized model-space transform、挂在该骨骼上的人体 socket，以及 AK 的 `PRIMARY_GRIP`、`FOREGRIP`、
  `MUZZLE`、`MAGAZINE` canonical weapon-space socket。
- `--pose-debug <model> <lower.rfanim> <lower-ms> <upper.rfanim> <upper-ms> <role>` 输出 lower/upper/result，
  并从 finalized character `WEAPON_R` 与 canonical AK grip sockets 派生 weapon transform 和左右 hand target；
  target 是模块化左臂 attachment IK 的输入；右手仍由 `WEAPON_R`/`PRIMARY_GRIP` 作为枪械主挂点。
- `--pose-debug <model> <lower.rfanim> <lower-ms> <upper.rfanim> <upper-ms> <additive.rfanim> <additive-ms> <role>`
  额外输出 ADDITIVE 与 composed result；`--action-composition-capture` 用固定相机生成可重复 BMP。
- `--pose-debug` 现在还输出 `spine`、双肩、双手的 stable role→bone mapping、local rotation 和 finalized
  transform，并打印 `WEAPON_R`/`FOREGRIP`、weapon origin、canonical weapon sockets、`MUZZLE direction` 与
  hand/socket delta。upper action 为 `RIFLE_IDLE` 或 `RIFLE_AIM` 时会自动生成两者的骨骼 local/position 对照。
- `--action-runtime-debug` 在 modular action state 变化时输出同一份 finalized pose/socket 诊断，并额外打印
  当前实际 skeletal weapon placement 的来源（`WEAPON_R + authored PRIMARY_GRIP`）、`PRIMARY_GRIP`
  transform、weapon origin transform 和 `MUZZLE direction`；这些输出只用于定位，不改变 pose、IK、basis
  或 renderer 结果。

- 统一离线入口、manifest 与完整性验证：`tools/assets/import_asset.py`；runtime 不读取 manifest。
  RFCHAR importer 的附件位置/旋转必须转换到 SKN1 identity-rest 基底，不能直接保存 GLB local TRS；
  `test_rfchar_pipeline.py` 校验附件全局 bind 变换。持枪仍从稳定角色/附件 API 获取接触位置。
- 静态 GLB 转 RMESH：`app/glb2rmesh.c`；skeletal RFCHAR 转换：
  `tools/assets/rfchar_import.py`；GLB 检查：`app/glb_inspect.c`。
- 刚性角色附件复用静态 GLB→RMESH 转换器，但 manifest 类型为 `rigid_attachment`，并强制声明
  mount origin、canonical character orientation 与 meter units。runtime 产物没有 SKN1/CHR1。
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
