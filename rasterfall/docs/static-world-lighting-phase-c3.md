# Static World Lighting V2 — Phase C3

> 文档更新：2026-09-14
> 源码核对基线补充：Phase D 冻结保持本页 consumer/ownership 契约，见最终冻结记录。
> 源码核对基线：Phase C2 后的 Runtime Lighting Consumer Cleanup；核对默认 world adapter、正常帧内 scene scope、diagnostic V1 owner、固定 capture 与 world-light source logic regression。

Static World Lighting V2 is the sole normal-runtime world-light source.
本阶段仅清理 consumer 与所有权，不新增 lighting 能力，不改参数、field、bake/ray、细分或 bilinear。
后续 Phase D 已完成冻结，最终状态与验收边界见 [Phase D](static-world-lighting-phase-d.md)。

## Consumer audit 与正常路径

C1/C2 已接入 ground/architecture、static RMESH、players/AI/RFCHAR、感染体、世界武器、
投掷物和 viewmodel。审计发现：32×24 V1 cache 仍挂在正常 render context 中，启动仍 bake；
没有 scene override 的 triangle helper 隐式回退 V1，使 sign、交互物、辅助 world geometry
与诊断共用旧 truth。`fixed_floor_lighting` 同时命名正常地面提交策略和固定诊断亮度。

| 正常 consumer | V2 入口 |
| --- | --- |
| ground / floor paint、primitive architecture、boundary wall | 默认 `world_brightness_at()` 与原 planar vertex scope |
| static RMESH，包括建筑与设施构件 | `render_static_props()` 每实例一次 world position sample |
| local/remote players、AI teammates、RFCHAR/modular/skeletal/procedural 回退 | 原 C2 root sample，单一 scene override |
| normal / special infected，包括死亡主体 | 原 C2 enemy root sample，单一 scene override |
| held world weapons / rigid equipment | 继承 owner scene override |
| weapon pickups / bomb / molotov body | 原 C2 entity position sample |
| viewmodel | 共用 local player sample，保留原可读性 clamp |
| signs、交互物及其他普通 world geometry | 默认 adapter 查询 V2，无隐式 V1 fallback |

`active_scene_light_override_q8` 是唯一 model/form/primitive scene factor 入口；
本地 sample cache 和 viewmodel 入参是该因子的传递值，不是独立 light truth。
原 form、material floor、fog、整数舍入与 owner sample 频率保留。
`active_world_light_v2` 仅控制既有 planar subdivision/vertex submission，不能选择默认 sampler 的版本。

## Diagnostic exceptions

- model gallery / isolated model tests / actor benchmark：原固定 scene 与 form 消融。
- Character Acceptance / lighting-props / procedural humanoid / arch/campus fixture：
  `diagnostic_fixed_lighting` 或显式 scene override；固定 regression lighting 保留。
- enemy acceptance，包括 rigid specials 的 world/distance views：显式 diagnostic V1 scope（原 planar 背景继续使用 V2），
  silhouette 和 isolated views 继续专用固定 lighting。
- Campaign MODEL_DISPLAY、Eula、humanoid debug 与 Character Test Strip：renderer-only diagnostic fixture，
  显式 V1 scope；不代表 gameplay actor。
- C2 fixed-input baseline：显式 diagnostic V1 scope；V2 mode 保留正常 C2 scope。
- Character world capture：正常环境使用 V2，测试带保留上述 diagnostic lighting。
- VFX、blob shadow、muzzle flash：专用效果语义，不作为独立 world-light truth；
  没有 scene override 的 world helper 默认使用 V2。

V1 bake/sample/compose 均以 `rasterfall_diagnostic_world_light_*` 命名，
缓存类型为 `rasterfall_diagnostic_world_lighting_v1`，不属于正常 render context/session。
独立 renderer diagnostic owner 按需 bake；world load/switch 失效，bind 关闭旧 scope。
V1 原算法测试保留，因为仍有真实 diagnostic consumer。
正常地面 `floor_submission` 仅保留无雾/深度提交策略；固定亮度只由明确 diagnostic flag 决定。

## 回归与跨层入口

`--logic-test` 除既有 world-light / scene×form / material 回归，还调用
`rasterfall_render_world_light_source_logic_test()`：冲突 V1/V2 值下，默认 adapter 在 planar
scope 开关两种情况下均返回 V2。测试无需 dependency framework。

改动涉及 `rasterfall_world_light.h/.c`、`rasterfall_render.h/.c`、logic test、
`src/dev-tests/` capture scopes 与启动日志；不新增编译单元或资源。
Linux/self 与 Windows 的既有 world-light source 规则继续适用。
验证日志、BMP、临时验收输入与性能记录放在 `tmp/static-world-lighting-v2-phase-c3/`，不提交。
