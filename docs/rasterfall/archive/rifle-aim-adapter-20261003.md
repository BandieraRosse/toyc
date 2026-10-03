# 肩托瞄准与武器物理尺度修订

> 状态：历史；本轮实现与定向验收记录；2026-10-03
> 当前合同：[动画架构](../architecture/animation-architecture.md)、[武器适配](../reference/weapon-model-adapter.md)

用户反馈扳机握持改善后仍有枪身穿胸和手部穿模，要求自然瞄准、关联实际射击方向，并明确加入
现实尺寸参考与模型文件到游戏呈现的适配层。本轮修改限定于展示、资产与诊断，玩法射线和伤害不变。

## 研究与实现

参考 [Epic Aim Offset](https://dev.epicgames.com/documentation/en-us/unreal-engine/aim-offset-in-unreal-engine)、
[Lyra 动画分层](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-in-lyra-sample-game-in-unreal-engine)、
[Unity Two Bone IK](https://docs.unity3d.com/Packages/com.unity.animation.rigging@1.2/manual/constraints/TwoBoneIKConstraint.html)
与 [MoCap Online Rifle](https://mocaponline.com/products/rifle) 的基础动作/瞄准/约束组织方式，采用原创
基础动作加有界程序瞄准、肩部约束和双臂 IK；没有下载、购买或导入商业动作素材。

新增 `rasterfall_rifle_pose.c`：冻结举枪权重、玩法 pitch、目标距离和反冲；脊柱/胸/颈/头分配姿态；
肩窝定位、胸部代理避让和可达投影后再求双臂。CPU、Scene 和目录动作台位共用同一过程。
举枪/放枪按模拟毫秒推进，重复采样不推进；近距离收敛有两米展示下限，最终 Euler 量化仍有小角度误差。

AK 以固定枪托 AKM 的 880 mm 总长为参考；当前网格经整数 `scale_milli` 边界后约为 881.0 mm。
adapter 统一源中心、轴向、物理长度及实际长度报告，枪体不会跟随角色展示缩放改变尺寸。
PRIMARY_GRIP、FOREGRIP、STOCK、SIGHT、MUZZLE 与 MAGAZINE 归武器 profile；SIGHT 本轮保留为
后续精细贴腮标定参考，尚未实现逐眼瞄具对齐。

RF-C01 V27 的肩到腕仅约 0.399 m，在现有身高、肩宽与等比例步枪下容易把支持臂拉直。
新 V28 显式迁移上臂/前臂 rest 与身体相应顶点，肩到腕约 0.499 m；手掌大小、肩关节位置、头眼口发、
身体拓扑、UV、权重和源动作保持。V27 单表面眼球方案保留。未通过运行时拉伸骨骼补偿比例。

私有源入口为 `authoring/assembly-body-v028.json` 和 `authoring/scripts/build_v028.py`，完整文件位于
`rasterfall/private-assets/source/characters/rf_c01/`；运行资产 `rf_c01_v028.rmesh` 与五张贴图已同步本地 package。
这些私有资产不进入 Git。验证报告明确 `rig_actions_unchanged=false`，单列源动作保留与 arm rest 变化，
不把显式骨架迁移描述为普通 body-only 发布。

## 本轮验证

- Windows native 主程序与资产工具构建通过，新增模块已加入根 Makefile、Windows 和适用 self 规则。
- `--logic-test`、`--gpu-scene-pose-test` 通过；后者包含 396 组方向/状态约束与 72 组动作采样，
  覆盖 Humanoid、等价高精度表示、RF-C01、近/远目标、护甲、举枪中间态和反冲。
- 动作采样最大握点分量误差约 1.271 RFU，最小腕部方向点积约 0.820；另检查完整握点朝向、
  reach clamp、胸部代理间隙、手指复位、sampler 幂等性、源尺度变化及 CPU/Scene 冻结一致性。
- V28 runtime validation 通过：v15、41,727 顶点、49 骨骼、两类权重、实例隔离和挂点运动。
- 12 组物理 GPU 原生捕获通过：瞄准左右三分之四与左右侧面、低持枪双侧、移动射击、
  上下极限、两米目标及 Heavy 两侧。均核对进程退出码、原生提交日志及生成图。

可复核证据位于 `tmp/aim-research/`：`build.log`、`logic-test.log`、`gpu-scene-pose-test.log`、
`runtime-validation.log`、`final/*/manifest.json` 和对应 PPM/PNG。原生复现脚本为 `final-native.ps1`；
Heavy 使用工具支持的 `quarter,side` 视角。临时证据和构建目录不进入 Git。

本轮改善了胸口、肩托和双手关系；胸部代理检查不证明所有网格三角形在任意动作中无接触。
没有新增逐指接触、自碰撞、换弹动作包或全园区性能签收。后续观感反馈仍沿当前动画与资产合同修订。
