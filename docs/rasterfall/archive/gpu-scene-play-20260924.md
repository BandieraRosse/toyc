# 独立 Scene 单人入口与交互接线

> 状态：历史现场；不作为完整架构或性能签收
> 日期：2026-09-24
> 当前入口：[活动计划](../plans/README.md)、[Scene 工作流](../guides/gpu-scene-fixture.md)

新增 `--gpu-scene-play`，自动启用 required native GPU 与独立 Scene，允许正常单人输入、
时钟、前哨站落地和 Runtime Map 切换。旧 CPU/mixed 启动方式保留。

暂停设置、结算、Tab 计分板和武器散布准星改为共享 canvas 布局，独立 Scene 直接冻结其几何。
补入 AI 名字、血量、倒地和救援进度；射线改为随相机方向的世界空间 ribbon，由硬件近裁剪；
死亡碎片改为三维盒体，受击方向箭头读取 effect instance。
正常运行的 Console/GUI 本来受关闭 gate 限制，没有为本轮解除。

## 故障修复

首轮 `record-failure` 暴露 runtime 在 Scene 失败后提前返回，跳过音频/Game/Core 统一关闭，
进程未正常退出。已改为沿统一清理路径退出，并清空 Scene owner 的敌人资源指针，支持重复关闭。
修复后五类 present fault 在 `tmp/scene-play-acceptance/` 通过；录制/提交失败退出 1，
其他三类状态异常完成四帧并退出 0。该轮还通过真实窗口暂停/设置/恢复/Tab、resize 和
120 帧 world-cycle。该候选之后仅修改射线俯仰朝向及帮助文本，最终候选另行验证。

## 功能候选的已完成验证

- Windows native build/package、逻辑和 pose 回归通过。新增逻辑检查覆盖布局只读、设置变化、
  计分板/结算及 canvas 失败传播。日志为 `tmp/scene-play-final-logic-test.*`、
  `tmp/scene-play-final-gpu-scene-pose-test.*`。
- 七类固定镜头各两帧通过 validation/sync：`ui-pause`、`ui-scoreboard`、`ui-shop`、
  `ui-over`、`ui-won`、`frame-effects`、`enemy-fade`。首帧显式捕获，第二帧零读回；
  每帧零旧 producer/RasterCmd/mixed draw、零 bridge/mixed execute。PPM、PNG、日志与哈希
  位于 `tmp/scene-play-captures/`，七张图均已人工查看。
- `tmp/scene-play-release/` 的单人启动 4 帧、真实输入/resize 80 帧、world-cycle 120 帧、
  连续战役 160 帧及五类 fault 全部通过 validation/sync。窗口测试实际执行暂停、设置调整、
  恢复、Tab 和 W 移动；extent 从 1280×720 变为 984×611，最终玩家位置从 `(0,-3000)`
  变为 `(0,-264)`。连续战役明确到达 `alive=1`，不是只验证波次提示期。
  378 个成功 native 帧均检查零旧录制、零 mixed execute/bridge/readback。

该候选 package SHA-256：`BFB1BCC179108A3EF4EEC03857DA12A1279A575C513DEDF74D46E515D5A44558`。
设备为 RTX 3050 Laptop GPU。捕获来自同一 Scene 批次的额外离屏提交，不证明 swapchain 自身像素。

## 最后定向修正

复核清理跳转时修正了补充模块化队员 pose 追加分支的条件退出，并给 `actor-procedural`
固定场景加入一名正式 roster 之外的雇佣 rifleman。脚本逐帧要求 `supplemental_modular` 非零，
覆盖真实 runtime append/submit，防止仅有来源采样的逻辑检查漏掉整帧提前退出。

最终包在 `tmp/scene-play-last-captures/` 通过 `actor-procedural`、`frame-effects` 各四帧
validation/sync 和显式首帧捕获；逐帧 `supplemental_modular=1`，保持零旧录制、零 mixed 执行。
该包五类 fault 在 `tmp/scene-play-last-faults/` 再次通过，逻辑、pose 和旧 mixed 暂停菜单四帧
回归通过，日志为 `tmp/scene-play-last-{logic,pose,mixed}.*`。旧 mixed 的 fallback、readback、
CPU copy 均为零。最终包 SHA-256：
`39D20AD69D101567C8972D9088634FF73F3ADEF03E6309896419B90B3BD2B0A8`。
最后修正只涉及补充 pose 条件和诊断 fixture；80/120/160 帧的完整矩阵属于上面的功能候选，
没有将其冒称为最终包重跑的完整生命周期签收。

## 边界

此入口用于单人功能与内容验收。当前同步退休后重建动态资源，实际运行仍可能很慢，
没有 FPS 签收。展示站/编辑器专用表现、完整视觉规范与所有反馈细节仍需继续核对；
完整 Full/10,000 帧 soak、性能五轮和实机联机不在本轮结论中。
固定菜单画面不能证明购买、重启和全部输入组合，窗口交互专项与逻辑回归应分别引用。
