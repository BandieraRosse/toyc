# Hardware Graphics：AI frontend checkpoint

> 状态：历史
> 归档原因：阶段完成或已由当前文档取代
> 当前入口：[GPU 渲染架构](../../gpu-rendering-architecture.md)

> 文档更新：2026-09-20
> 源码核对基线：当前工作区；模块化角色 finalized pose/skinned vertex、被动 gear 与 active weapon actor-local placement 均绑定 pose generation，body/gear/weapon 实际变换边界已接入组合剔除；Windows package、`--logic-test`、角色 world capture、0/10/30/60 actor benchmark、20 帧 Campaign strict native audit，以及 `edge-entry.bmp` / `near-crossing.bmp` 专用 capture 均已通过。本 checkpoint 签收。

## 当前范围

本 checkpoint 位于 HG-3 与 HG-4 之间，只优化角色 presentation frontend，不改变玩法、actor、动画
时钟、武器选择或网络状态所有权。目标是先减少生成后丢弃的角色命令，以及重复的姿态、蒙皮与装备准备。

首个增量在 `rasterfall_modular_actor_runtime` 中按 actor instance 缓存 finalized pose。缓存键包含
actor identity、character identity、lower/upper/additive action、三个动作时间和当前武器；完全一致时
复用上次已经完成 action composition 与左手 IK 的 instance pose，任一字段变化即重新求值。缓存不跨
instance，不缓存 gameplay 数据，也不改变 body、gear 或 weapon 的绘制顺序。

`--frame-audit` 的 `ai-triage` 新增 `pose_cache_hit` / `pose_cache_miss`。两项只统计模块化角色的
presentation pose 求值，不代表 skinning cache、可见像素或 GPU 时间。

第二个增量为每个模块化 actor instance 分配独立 model frontend。每次 finalized pose miss 都推进非零
`pose generation`；frontend 只有在已蒙皮 generation 与当前 generation 完全相同、且 pose key 命中时
才复用 skinned vertex。actor 槽位切换、动作/时间/武器变化均会使 generation 前进并强制重新蒙皮，
避免默认 frontend 在多个角色之间共享可变 vertex cache，也避免命中旧 pose 的蒙皮结果。

第三个增量把被动 gear 的 socket/mount 组合缓存为 actor-local transform，并以 pose generation 为唯一
失效键；actor 的位置、朝向与 scale 仍在每帧轻量组合，避免把 world transform 错绑进 pose cache。
模块化角色在 body skinning 与 gear 三角形提交前合并 body 与逐件 gear 的变换后 AABB；active weapon
也以同一 finalized `WEAPON_R + PRIMARY_GRIP` placement cache 同时驱动三角形提交与实际模型 AABB，
不再使用 held-weapon envelope。actor 世界位置、朝向与缩放仍逐帧组合，不进入 pose cache key。
`ai-triage` 同步报告 combined-bounds、bounds cache 与 gear transform cache。

当前 Windows 验证中，角色 world capture 的 near/mid/far 与 old/idle/aim/motion 全部生成成功；固定
world benchmark 的 normal 结果为 near 0/10/30/60 actor 约 23.137/26.043/34.806/54.414 ms，mid
约 29.559/34.254/44.653/60.395 ms。`--gpu-wave-repro --legacy-map` 的 20 帧 Campaign strict native
audit 为 20/20 GPU 帧、零 fallback；现场出现 `combined_bounds_culled=1`，证明组合边界进入正常路径。
这些数值是本机单次 12-iteration 观测，不写入稳定性能承诺。

## 最终验收

`--character-world-capture` 在原有 near/mid/far 与 old/idle/aim/motion 之后追加两个正常模块化 AI fixture：

- `edge-entry.bmp`：角色位于右侧视口边缘，body、被动 gear 与 AK 均通过实际组合边界保留；
- `near-crossing.bmp`：角色包围盒跨越 WORLD near plane，剔除器保守保留并由正常 near clipping 生成画面。

两张图均记录非零 body/gear/weapon command，`combined_bounds_culled=0`，人工检查未见装备、武器或身体因
组合边界而提前消失。`--logic-test` 另固定“body 单独完全出屏但加入 gear/weapon 范围后仍入镜”和
“包围盒跨 near plane 不得剔除”两项纯逻辑合同。Windows package、逻辑测试与完整 17 张 character world
capture 均正常退出；既有单/多 worker 与性能回归保持此前通过状态。

AI frontend checkpoint 已签收；HG-4 仍需单独开始，不因本次签收自动视为已实施。
