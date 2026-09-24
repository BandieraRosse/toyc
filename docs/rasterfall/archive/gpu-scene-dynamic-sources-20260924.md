# 独立 Scene 动态来源接线记录

> 状态：历史现场归档，不作为当前设计或完整帧签收
> 日期：2026-09-24
> 当前入口：[活动计划](../plans/README.md)、[角色表现](../architecture/character-presentation.md)

独立预览直接收集普通/特殊/LEGACY 敌人、死亡变换、阴影/舌头、程序与倒地角色、补充模块化
角色及网络展示输入；来源不调用旧 draw producer。普通感染体和特感各持独立展示历史。
程序及补充角色不做旧 CPU 屏幕裁剪，数量不能直接与旧 producer 的可见 actor 数比较。

## 本轮验证

- Windows 原生 build/package 成功；`--logic-test` 和 `--gpu-scene-pose-test` 均退出 0。
- 来源回归覆盖只读输入、重复采样拒绝、world 重置、死亡变换、舌头、步态，及补充模块化、倒地、host/client 展示值。
- RTX 3050 Laptop GPU、Khronos validation/sync：near、mid、thin-far、Campaign、enemy-special、
  enemy-death、enemy-tongue、actor-procedural 各四帧通过；逐帧无旧 RasterCmd/mixed draw、无 bridge/mixed execute。
- 显式首帧捕获同一冻结 Scene 批次并 native present，首帧 `readback=1`，后续帧为零。
  near 首帧冻结 30 个敌人、18 个程序角色；程序职业 fixture 冻结 22 个程序角色，包含镜头外项。
- 另跑 near 四帧不启用捕获，所有帧零读回；validation/sync 通过。
- 已查看 Scene PNG，确认普通感染体、三种特感、舌头及职业角色进入独立画面。

逻辑日志在 `tmp/scene-dynamic-20260924/`；前四种镜头在 `tmp/scene-dynamic-preview-20260924/`，
定向四镜头在 `tmp/scene-dynamic-directed-20260924/`，零读回用例在
`tmp/scene-dynamic-zero-readback-20260924/`；后两目录保存可执行文件哈希。
首次定向运行误传 30 个普通敌人，数量断言失败；修正脚本为定向 fixture 传 0 后重跑通过。
原目录中失败的 `enemy-special` 不是通过证据。

## 边界与后续

这不是单人可玩完整帧：天空、有序透明与死亡渐隐、特效、VIEWMODEL、HUD/OVERLAY 和
展示站/编辑器专用角色尚未纳入。网络仅验证无 socket 的展示输入，未运行实机联机。
补充模块化角色身份限于本帧，动态资源仍逐帧重建并同步退休，未证明性能收益。
捕获是额外 Scene 离屏读回，不证明 swapchain 自身颜色；正式性能和完整生命周期验收仍未执行。

复现使用 `tools/gpu_scene_preview.ps1 -Independent -Capture`，可加本地
`-ValidationLayerDirectory tmp/scene-validation-tools/mingw64/bin`；操作合同见
[Scene 工作流](../guides/gpu-scene-fixture.md)。下一步按活动计划补完整单人帧。
