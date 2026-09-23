# Scene 1B 同步验证与人物绕序修复现场

> 状态：历史现场归档；单次 Windows 设备证据，Intel 门禁未完成，不代表阶段 1B 全部签收
> 日期：2026-09-24
> 当前入口：[活动计划](../plans/README.md)、[复现指南](../guides/gpu-scene-fixture.md)

## 同步与设备

从 [MSYS2 官方仓库](https://packages.msys2.org/packages/mingw-w64-x86_64-vulkan-validation-layers)下载并解压 Khronos layer 到 `tmp/scene-validation-tools/mingw64/bin`，不改系统安装。
最终使用 validation-layers 1.4.350.1、SPIRV-Tools 1.4.357.0，配合本机 MinGW runtime。
最初 1.4.341.0 layer 与本机 libstdc++ TLS 符号不兼容，loader 未插入 layer，脚本拒绝签收。
新版 layer 使用 `Khronos Validation Layer Active` / `Current Enables: VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`
输出，已兼容该明确启用证明，仍要求 loader 插入 Khronos layer。

最终证据目录为 `tmp/scene-sync-final-20260924/`；原图、日志、逐项退出码、layer DLL/JSON 哈希见 manifest。
package executable SHA256：`706DA443F1D878DE90A5D3F9D818080162EE8D8E059B5F89F21431DDDCC6FA78`。
设备 RTX 3050 Laptop，vendor/device `10de/25e2`；Vulkan 原始 driver `2584608768`、API `4211039`。

- 120 帧独立 Scene：三件套顶点/遮挡对照，增长、两次 resize、world 失效与 fence 后退休。
- 五类故障：acquire-out-of-date、record-failure、submit-failure、present-out-of-date、present-suboptimal。
  record/submit failure 退出 3，其他退出 0；失败时三份 pin 保留到 drain，清理后 live/retired/pinned 为零。
- pose、logic 与旧 normal native 回归；运行过程要求无 Validation Error、SYNC-HAZARD 或 VUID。

Vulkan 枚举只有上述 NVIDIA 与 AMD Radeon `1002/1638`（驱动返回多个 AMD 条目），没有 Intel。
`tmp/scene-intel-gate/` 保存指定 `8086` 的拒绝证据：退出 3，`required vendor=8086 unavailable; no device substitution`。
这只证明设备门禁拒绝错误设备，**不证明 Intel native 通过**。不反复探测；未推进 CPU backing 复用、Scene GPU 时间戳或阶段 2。

## 人物透背根因

正式 CPU renderer 的面积定义是 `(c-a) × (b-a)`，负值可见；GPU 原先使用 COUNTER_CLOCKWISE，
与屏幕 Y 向下的正高度 viewport 不一致。单面人物会剔除前表面而显示后表面。
现在普通与 integer-compatible graphics pipeline 都使用 CLOCKWISE，不改资产、骨骼、材质或深度比较。
独立 graphics 测试原先使用相反叉积却沿用 CPU 符号，现修正 reference，并验证正面、背面、双面与变换。

graphics、depth gate（含 near clipping 与 bridge）、hosted mixed executor 已验证。
depth gate 还暴露旧 bridge oracle 仍按 RGB 换序及 pre-LOAD 快照比较的问题；当前共享颜色保留 RGBA8，
诊断导出的是后续绘制结果。测试按现有消费者合同修正，未修改 bridge shader 或深度语义。

`tmp/scene-culling-before/` 与 `tmp/scene-culling-after/` 保存固定 near/mid、30 humanoid-infected、tick、首帧的
CPU PPM / GPU BMP、PNG 原图、RGB 差图及 executable 哈希。CPU 哈希不变；near 变化像素由 10369 降到 10026，
mid 由 390320 降到 390310。它们用于记录本次改动，不把仍存在的完整帧差异当作视觉基线通过。
Scene body/head/map 单独图与组合遮挡另见同步 suite。未扩角色或材质，未运行正式性能 A/B。
