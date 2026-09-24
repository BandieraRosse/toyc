# 独立 Scene 首版分层接线记录

> 状态：历史现场，不作为完整可玩帧或性能签收
> 日期：2026-09-24
> 当前入口：[活动计划](../plans/README.md)、[HUD 与特效](../architecture/hud-effects.md)、[GPU 架构](../architecture/gpu-rendering-architecture.md)

在独立动态 WORLD 来源上接入 SKY、地图透明/死亡渐隐、EFFECTS、VIEWMODEL 和 OVERLAY。
天空和 HUD 共用无 framebuffer 的 canvas 布局；武器共享几何求值，通过独立 callback 输出，
不调用旧整帧 producer，不创建 RasterCmd 或 mixed recording。POST 保持 identity。

Scene 稳定分层后整批 preflight。透明 source-over、depth-test/no-depth-write；天空和 HUD 无深度；
VIEWMODEL 开始时清深度并保留世界颜色，未覆盖处继续显示世界。动态几何 backing 同步退休后重建，
这轮没有开展资源复用或性能优化。

## 验证

- Windows native doctor、build/package 通过；最终 package 的 `--logic-test`、`--gpu-scene-pose-test` 通过。
- GPU graphics 回归通过，新增测试分别验证天空不写深度、透明遮挡与不写深度、第一人称独立深度、
  HUD 不改深度及错误层序在提交前拒绝。RTX 3050 Laptop GPU，24,655 checks；日志
  `tmp/scene-layers-graphics.out`。
- 初轮十类镜头各四帧，共 40 帧通过 validation/sync，保存在 `tmp/scene-layers-matrix-20260924/`。
  画面复核发现爆炸闪光把 milli-scale 当作像素尺寸，不能将该轮特效图当作最终内容签收。
- 修正尺寸后，最终 package 重跑 `frame-effects`、`enemy-fade`、`near` 各四帧，共 12 帧，
  validation/sync 通过；逐帧零旧 producer、RasterCmd、mixed draw/execute 和 bridge。
  每个用例首帧显式 Scene capture 读回，之后零读回。日志、PPM/PNG 与哈希保存在
  `tmp/scene-layers-final-20260924/`；已查看最终特效及 HUD 图，死亡渐隐另经初轮图确认。
- 最终 package 另跑 near 四帧不捕获，全程零读回，validation/sync 通过；证据在
  `tmp/scene-layers-zero-20260924/`。
- 最终 package 的旧 mixed required native 路径另跑 near 四帧，退出 0，未发现 validation/sync
  错误；native 输出 color-readback 与 CPU framebuffer-copy 为零。日志为最终目录中的
  `mixed.out`、`mixed.err`；它验证共享布局/武器几何改动的回归，不作为独立帧内容证据。
- `tools/check_docs.ps1` 与 `git diff --check` 通过。

最终 package SHA-256：`ECBDB2A07009C8E5B9186637BCD3B9A748C4E5CB0B50268D255D9937DED28753`。
原生捕获仍来自同一冻结批次的额外离屏提交，不证明 swapchain 自身的像素。

## 边界

这轮交付的是五类内容的首版独立接线，不是完整单人可玩入口。特效碎片形体、射线近裁剪与
部分反馈细节、完整菜单/名字状态/scoreboard/console/GUI、展示站专用内容仍需收敛。
基础暂停/结算面板已经接线，但没有交互专项签收；不以本次固定镜头证明这些交互。
完整帧 resize/world-cycle/fault/连续运行与实机联机尚未执行，性能结论继续后置。

复现入口为 `tools/gpu_scene_preview.ps1 -Independent -Capture`；固定镜头增加 `enemy-fade`
与 `frame-effects`。`world_only=0` 表示含非 WORLD 层，不等于所有内容或阶段 3B 已完成。
