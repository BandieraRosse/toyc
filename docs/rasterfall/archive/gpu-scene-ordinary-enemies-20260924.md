# 普通感染体 Scene 存活身体接入

> 状态：历史现场；阶段 2 局部接入，不是阶段退出或性能签收
>
> 当前入口：[活动计划](../plans/gpu-scene-renderer.md)、[角色表现](../architecture/character-presentation.md)

六种 Block/Humanoid Common、Fast、Heavy 资源通过冻结 recipe、步态、变换和反馈色进入 Scene。
提取使用独立 scratch instance，复用不可变资源和原姿态算法；不读取旧 mutable pose 或推进 motion cache。
WORLD 沿用逐顶点 V2 光照、材质双面标志和共享深度。非 V2 分支保留材质亮度范围。

## 本轮证据

- Windows native package 构建退出 0；executable SHA256 为
  `350CA8680F6F5E369DEED3FF3F7A49B5FC4AB469A5187E2542692AE4B0864E68`。
- `--gpu-scene-pose-test`、`--logic-test` 均真实退出 0。新增资源回归覆盖六种 recipe、步态变化、
  冻结重放、旧 scratch 改写后隔离、非法 recipe 与 emit 失败传播。
- `tools/gpu_scene_enemies.ps1` 在 RTX 3050 Laptop GPU 上通过；Khronos layer 实际加载，
  Synchronization 开启，无 validation error、VUID 或 sync hazard。
- 特感首帧 2 帧与动作 12 帧回归保留；普通感染体 AUTO 首帧、第二帧动作、Block、Humanoid 各 2 帧。
  普通感染体均为 `items=30 deferred=0 culled=0`；AUTO 每帧 9282 个身体 draw。
- 同帧 mixed BMP 与 Scene PPM 的 42 个固定身体内部像素精确一致，包含 14 个原特感采样和
  28 个普通感染体采样。原图、PNG、像素记录、哈希和日志位于 `tmp/scene-infected-verified/`。

## 限制与后续

最初的普通感染体 12 帧同步审计在 60 秒进程预算内未结束，已停止并记录于
`tmp/scene-infected-check/ordinary-motion.*`。专项改为两个固定 tick，资源测试另验证步态变化。
同步审计每帧重建动态 GPU 资源并读回，不能用其墙钟或 draw 数推断产品性能。

画面仍存在硬件光栅与旧整数路径的边缘差异；局部精确像素不是完整 WORLD 容差审批。
死亡、阴影、显式 LEGACY、程序角色、网络角色及舌头仍待接入；正常呈现保持 mixed。
独立正常来源、资源复用与完整帧整合仍按活动计划推进。
