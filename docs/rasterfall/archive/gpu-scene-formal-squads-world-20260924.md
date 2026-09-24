# GPU Scene 八名正式队员 WORLD 现场（2026-09-24）

> 状态：历史现场归档；正常帧离屏 Scene WORLD 诊断，不是 Scene 正常呈现验收

Campaign session 的两支正式小队共有八名非 hired 模块化队员。reset 时逐名登记来源 epoch；每帧按来源 ID 查找当前 slot，分别冻结动作展示时钟和角色 sidecar。snapshot 按 slot 顺序压紧，pose 提取按索引消费该冻结值，八个独立 Scene actor GPU 持有者将 body、被动装备和 AK 武器加入同一 WORLD color/depth 提交。hired、remote、其他角色类别和动态敌人尚未覆盖。

逻辑回归验证双角色登记顺序与提交顺序不同、独立动作时钟、重复登记拒绝和失败事务不改来源及输出；Campaign session 验证八名 actor 的 pose/identity。Windows 原生 `near 0` 两帧为 1099 draw，其中 120 actor draw、979 地图与静态实例 draw，次帧地图资源 979 cache hit。`actor-standard` 与 `actor-assault` 两镜头都在 Scene 读回中看到四名队员；与 mixed 截图对照的六个无遮挡角色 RGB 采样点完全一致。截图与日志位于 `tmp/scene-multi-actor-captures-20260924/`。

Scene target 仍是正常 mixed present 后的离屏审计。天空、旗杆、HUD/viewmodel 和其余可见层尚未进入统一 Scene。`tmp/scene-multi-actor-sync-20260924/manifest.json` 记录 17 项原生回归、RTX 3050（vendor 10de）与 validation/sync 均为 PASS。
六个像素点已固化到 `tools/gpu_scene_native.ps1`，后续原生套件会保存两组 BMP/PPM 并执行精确 RGB 比较。
像素门禁复跑保存在 `tmp/scene-multi-actor-pixels-sync-20260924/`：17 项、RTX 3050 和 synchronization validation 再次 PASS；两组捕获文件均由该套件保存。随后补入 pose 列表的 world/frame/身份 preflight，`tmp/scene-multi-actor-preflight-smoke-20260924/` 的 RTX 3050 两帧 `actor-standard` 回归仍为 1076 draw、120 actor draw、次帧 956 map cache hit，日志无 validation 错误。
