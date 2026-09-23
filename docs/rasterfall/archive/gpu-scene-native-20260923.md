# Scene 1B 三件套 native 提交现场

> 状态：历史现场归档；单次 Windows 设备证据，非完整阶段签收
> 日期：2026-09-23
> 当前入口：[活动计划](../plans/README.md)、[复现指南](../guides/gpu-scene-fixture.md)

本轮贯通冻结 `opaque_box`、RF Humanoid V2 body 和 rifleman HEAD 的独立 Scene slot。正常帧未切换。
Windows 原生 build、pose 回归、logic 回归、旧路径 required-native 3 帧均通过。

可复跑 suite 为 `tools/gpu_scene_native.ps1`；本次原始日志和图像保存在本地
`tmp/scene-1b-final-package/`，不提交生成物。manifest 记录 executable SHA256 和逐次参数、退出码。
设备为 NVIDIA GeForce RTX 3050 Laptop GPU，vendor `10de`、device `25e2`。

## 本次观测

- 120 帧 native 提交成功，实际 Raster bridge roundtrip/transfer 计数始终为零。
- body GPU 顶点对照：position、normal、UV mismatch 全为零，最大 position/normal delta 均为零。
- 四次离屏绘制比较 composite 与 map/body/head 单独结果，color/depth mismatch 为零。
  三件均有可见像素，map/body、map/head、body/head 均有交叠。此诊断显式 readback，不计入 native 零回读断言。
- 第 21 帧 skin backing 从 5100 增长到 10200 顶点，新增顶点不被 draw 引用；替换发生于退休后。
- 第 41/61 帧真实窗口 resize，extent 从 960×540 到 784×441 再回到 960×540。
- 第 81 帧 submit 后 invalidate world：三份旧资源仍 pinned/resolvable；fence 完成后才释放并进入新 world generation。
- 第 2 帧注入 record/submit failure：退出码 3，成功帧数 1，三份 pin 保留到 GPU teardown drain。
  acquire-out-of-date、present-out-of-date、present-suboptimal 均恢复并完成 4 帧，退出码 0。
- 所有测试退出后 registry live/retired/pinned 均为零。

## 接线发现

passive gear 的 `model_to_world` 仍需按资源 `position_scale` 转换到 RFU；HEAD 资源与 body 的单位不同。
consumer 已按原 rigid renderer 的换算执行一次，不能直接把 raw vertex 当 RFU。
另一个差异是 RFANIM finalized pose 的 bind-normal 策略：只上传矩阵会使 GPU 额外旋转法线。
现将策略显式冻结进 pose 值，使用已有 skin shader flag，未修改 shader 或正常 renderer。

## 未完成门禁

本地既有 validation layer 路径和常见安装位置未发现 Khronos layer，故 validation/sync **未运行**。
Windows doctor 的系统 GPU 枚举被拒绝；本次 Vulkan 实测只覆盖上述 NVIDIA 设备，Intel **未验证**。
没有把环境变量设置或零错误文本当作 layer 加载证明。

仅记录 snapshot、pose、pack、prepare、submit/present、retire 的 CPU 分段成本；GPU pass timestamp 尚未独立
接入 Scene。这些数据不构成完整产品帧 A/B。CPU pose/pack backing 仍逐次分配，完整材质、透明、HUD 等不在范围内。
