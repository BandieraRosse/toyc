# 正常帧角色 Scene WORLD 接入现场（2026-09-24）

> 状态：历史现场归档；离屏 Scene WORLD 诊断，不是正常帧 Scene 呈现验收
>
> 日期：2026-09-24

## 范围

正常帧 `--frame-audit` 从真实 session 的首位非 hired RF rifleman 冻结 finalized body palette、
rigid HEAD 变换和 V2 actor 光照值。独立角色 GPU owner 复用 Scene fixture 的资源解析、
CPU backing、GPU skinning、frame pin/退休和 draw preflight，将 body/HEAD 的 8 个 draw
与真实地图及静态实例提交到同一个离屏 Scene WORLD color/depth。审计在 mixed present 后执行，
显式读回 color/depth；正常呈现仍由 mixed executor 完成。

## Windows native 证据

主设备为 NVIDIA RTX 3050（vendor `10de`）。`NativeCodex.ps1 build` 与 `test` 通过，
`tools/gpu_scene_native.ps1` 使用 Khronos validation layer 及 Synchronization 验证通过。
证据在 `tmp/scene-actor-world-final-sync-20260924/manifest.json`，结果为
`PASS - available gates`、`validation_sync: PASS`；该次可执行文件 SHA-256 为
`ADEE0772A8FF5AF869E3EB16DC4F1B097CB20828F2D3F10F78FB392518A4A618`。

Campaign `near 0` 首帧 `SCENE-WORLD-GPU` 为 965 draw（其中角色 8）、524,824
个有效像素、地图 GPU cache 上传 937 和 hit 20；次帧地图上传 0、hit 957。
`actor-rifleman 0` 镜头首帧为 957 draw（角色 8）、530,439 个有效像素、
地图上传 933、hit 16；次帧上传 0、hit 949。该镜头的静态实例有 4 个数值预检暂缓。
日志的 `scene_light=256` 来自同一帧冻结的 V2 actor 光照。定向 BMP/PPM 在
`tmp/scene-normal-actor-lit-20260924/`，可见 body/HEAD 与地图深度遮挡；
胸口抽样 mixed RGB `(80,93,65)`、Scene RGB `(92,109,80)`，角色材质亮度规则尚未对齐。

## 后续边界

离屏审计还没有接入其余角色、武器、完整 WORLD、透明/effects、viewmodel、overlay、
正常 swapchain 呈现或完整帧性能验收。该结果是资源与深度域接入证据，不能作为阶段 2
退出或画面合同通过的证据。
