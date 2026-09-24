# GPU Scene 模块化角色配方现场（2026-09-24）

> 状态：历史现场归档；单 actor Scene 诊断，不是多 actor 正常帧验收

此前 pose 提取器只接受 RF Rifleman，GPU 打包固定 rifleman 头部和衣裤颜色。现按冻结角色 ID 解析共享模块化配方，冻结 body、装备/socket、衣裤颜色；GPU 资源准备验证同一配方，并按冻结颜色覆盖 body 材质。八种角色 profile 的 pose/附件逻辑回归通过。

`tmp/scene-modular-medic-native-20260924/` 记录 RTX 3050 上 105 帧原生 Scene 生命周期：第 101 帧把单一 actor 从 Rifleman 切换到 Squad A Medic，draw 从 11 增至 12；最终 `result=0 rendered=105 bridge=0`、`cleanup live=0 retired=0 pinned=0`。Khronos validation layer 的同步验证已启用，日志无 Validation Error、SYNC-HAZARD 或 VUID。

完整 15 项原生回归记录在 `tmp/scene-modular-recipe-sync-20260924/`；manifest 为 `PASS - available gates`、`validation_sync=PASS`、`requested_vendor=10de`。120 帧生命周期的第 101 帧同样从 11 增至 12 draw，末帧和资源清理通过。

正常帧审计仍只冻结并提交一名 rifleman。多 actor 身份、各自动作时钟与有序资源提交尚待接入。
