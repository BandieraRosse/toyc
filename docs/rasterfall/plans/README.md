# Rasterfall 活动计划

> 状态：当前
> 所有者：Rasterfall 项目优先级
> 最近核对：2026-09-23

本目录是 Rasterfall 当前工作顺序的唯一入口。架构文档不得声明另一条“当前唯一执行链”。

## Active plan（当前唯一活动计划）

[下一代统一 GPU 渲染器计划](gpu-scene-renderer.md)

WORLD 的地图、静态实例、角色、敌人、附件、旗帜、投射物与交互物已进入独立 Scene。
阶段 3 首个 `--gpu-scene-world-preview` 入口已把真实 WORLD 接到 native present，跳过 mixed
执行和离屏读回；near/mid/thin-far/Campaign 各四帧通过 RTX 3050 validation/sync。
下一步接透明、特效、独立 VIEWMODEL、HUD/OVERLAY 与天空，再形成完整正常帧候选。
默认完整呈现仍为 mixed；WORLD preview 不作为完整画面或产品 FPS 结论。

阶段 2 已补齐静态 RMESH 超出旧整数投影范围时的 Scene 硬件裁剪选择；角色镜头中的数值暂缓已清零。
动态敌人新增 Smoker、Charger、Tank 存活身体的同帧冻结和独立 Scene WORLD 提交。
普通感染体六种 Block/Humanoid recipe 的存活身体已接入同帧 Scene WORLD，冻结步态后独立求姿态与几何。
程序角色、不透明死亡、legacy 敌人、blob shadow 和 Smoker 舌头已接入同帧离屏诊断。
网络玩家按用户决定仅验证逻辑路径，实机联机问题延后到 GPU 完成后的网络专项，不阻塞当前迁移。
2026-09-24 用户决定：阶段 2 按迁移范围收尾，整图光照/光栅差异只记录，重构后修复，
不再以 CPU/mixed 画面一致性或数值容差审批阻塞主线。此决定不表示旧画面合同已经通过。
当前进入阶段 3：接入硬件 Scene 正常帧；首个切片将真实 WORLD 从离屏读回改为独立 native
提交，随后接透明、特效、VIEWMODEL、HUD/OVERLAY。未接齐的实验入口必须明确标为 WORLD preview。

计划中的当前切片、前置条件、完成门槛和下一决策点以该文档顶部为准。已完成 checkpoint、撤销实验和
单次设备测量进入 [`../archive/`](../archive/)，不得继续充当优先级来源。

## 更新规则

- 开始或切换主线时，先更新本页，再更新活动计划。
- 同一时间只列一个“当前唯一活动计划”。
- 稳定后的所有权和数据流移入架构文档；复现步骤移入 guide；固定格式移入 reference。
- 活动计划完成或被替代后整体归档，不在入口保留并行的旧执行链。
