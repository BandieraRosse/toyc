# Rasterfall 活动计划

> 状态：当前
> 所有者：Rasterfall 项目优先级
> 最近核对：2026-09-23

本目录是 Rasterfall 当前工作顺序的唯一入口。架构文档不得声明另一条“当前唯一执行链”。

## Active plan（当前唯一活动计划）

[下一代统一 GPU 渲染器计划](gpu-scene-renderer.md)

冻结三件套已接入独立 Scene native 提交、pin/退休和生命周期专项；RTX 3050 validation/sync 已通过。CPU upload backing 已在退休后复用，Scene WORLD draw 与 present blit 已按帧采集 GPU 时间戳。正常帧审计冻结 Runtime Map render、分区地面、object、活动旗帜、动态投射物、交互物及八名正式模块化队员各自的 pose/光照值。十一类生成地图网格（含六种导入感染体展示模型）、可见静态 RMESH、旗帜几何与双面字形、bomb/molotov 模型、全部 45 个 Campaign 交互物、八名队员的 body、被动装备和 AK 武器在同一独立 Scene WORLD color/depth 绘制并读回验证。角色 pose 与 GPU 打包按模块化配方处理 body、装备和颜色；各自的身份及展示时钟在 session 来源处冻结。V2 光照 bake 代际与模型类的冻结 V1 诊断光照进入资源复用键。下一步补齐其余 WORLD 与角色类别，再将整帧接入正常 Scene 呈现。主线开发与签收以 RTX 3050 为准。正常呈现仍走 mixed 路径，完整帧性能 A/B 从阶段 3 开始。

阶段 2 已补齐静态 RMESH 超出旧整数投影范围时的 Scene 硬件裁剪选择；角色镜头中的数值暂缓已清零。
动态敌人新增 Smoker、Charger、Tank 存活身体的同帧冻结和独立 Scene WORLD 提交。
普通感染体六种 Block/Humanoid recipe 的存活身体已接入同帧 Scene WORLD，冻结步态后独立求姿态与几何。
下一切片先补程序角色，网络玩家随后复用其表现路径；Smoker 舌头保留最小等价表现。
死亡、阴影与其余附属表现及完整 WORLD 画面合同仍待补齐；阶段 2 尚未退出。

计划中的当前切片、前置条件、完成门槛和下一决策点以该文档顶部为准。已完成 checkpoint、撤销实验和
单次设备测量进入 [`../archive/`](../archive/)，不得继续充当优先级来源。

## 更新规则

- 开始或切换主线时，先更新本页，再更新活动计划。
- 同一时间只列一个“当前唯一活动计划”。
- 稳定后的所有权和数据流移入架构文档；复现步骤移入 guide；固定格式移入 reference。
- 活动计划完成或被替代后整体归档，不在入口保留并行的旧执行链。
