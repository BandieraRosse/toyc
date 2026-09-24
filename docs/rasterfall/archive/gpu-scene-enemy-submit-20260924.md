# 敌人几何与提交绑定优化现场

> 状态：历史测量现场；不代表正式 FPS 签收
> 日期：2026-09-24
> 当前合同：[GPU 架构](../architecture/gpu-rendering-architecture.md)
> 当前工作：[活动计划](../plans/README.md)

本轮保留全部敌人、三角形、draw 和层序，消除单个身体内重复索引的 CPU skinning、
精确世界坐标重复光照采样，以及同一 render pass 内连续 draw 的重复绑定。
缓存不跨帧；资源 pin、同步 fence 退休和目标生命周期没有变化。

## 测量

Windows native、RTX 3050 Laptop GPU、1280×720、near 固定 tick，0/30/60 敌人。
每项三轮交替 A/B，每轮 32 帧，排除前 8 帧；表中为三轮中位数的中位数，单位 ms。
这是带逐帧日志的归因采样，不能作为正式五轮产品 FPS 验收。

| 单独切换的优化 | 敌人数 | 指标 | 对照 | 优化 |
| --- | --- | --- | --- | --- |
| 几何缓存 | 0 | geometry | 2.606 | 1.992 |
| 几何缓存 | 30 | geometry | 7.267 | 5.492 |
| 几何缓存 | 60 | geometry | 12.390 | 9.207 |
| 几何缓存 | 60 | enemies prepare | 17.751 | 14.587 |
| 几何缓存 | 60 | whole loop | 42.510 | 39.234 |
| 绑定复用 | 60 | submit/present | 12.607 | 4.671 |
| 绑定复用 | 60 | retire | 1.706 | 0.895 |
| 绑定复用 | 60 | GPU draw | 1.732 | 0.934 |
| 绑定复用 | 60 | whole loop | 47.642 | 40.592 |

两组分别只切换一个变量，其他优化开启，收益不能直接相加。两组 report 均验证同包哈希及逐帧
draw、敌人、剔除、程序角色和补充模块化来源数量一致。零敌人仍有程序角色，因此 geometry 不为零。
提交大段成本包含命令录制、acquire 和 present，不能全部归因于 fence；独立 retire 数据说明剩余等待较小。

原始日志与报告位于工作区 `tmp/enemy-opt-prep-ab/` 和 `tmp/enemy-opt-bind-ab/`，不入版本控制。
候选包 SHA-256：`F58A457F4BF651EB0B558483CF33C13EE8D7CA1B1A8D9345228EAABBB6DB4A67`。
复现使用 `tools/gpu_scene_cost.ps1 -Experiment EnemyPrep` 或 `-Experiment DrawBind`，
配合 `-Frames 32 -Rounds 3 -OutputDirectory <新目录>`。

## 验证范围

原生 build/package 与文档检查通过。两种模式分别启用 Khronos validation/sync，
near 60、enemy-special、enemy-death、enemy-fade、enemy-tongue、actor-procedural、frame-effects
各运行 4 帧，均无 validation/sync 错误；第 4 帧的七组 Scene PPM 全部逐字节一致。
画面与日志保存于 `tmp/enemy-opt-capture-legacy/`、`tmp/enemy-opt-capture-optimized/`，
比较结果为 `tmp/enemy-opt-verify.log`。普通帧零 readback，只有指定捕获帧离屏读回。

`tmp/enemy-opt-native/` 记录 120 帧 native fixture 的 resize、资源增长、world retirement、
五种 present 故障注入、六种感染体 pose/replay 和完整逻辑回归通过；normal-native 三帧进程也正常退出。
聚合脚本随后因历史固定 draw 断言失败：预期 1241，当前实际 1529，新增部分为四个程序角色的 288 draw。
关闭本轮两个优化后复核同样得到 1529，三帧覆盖像素及 cache 计数也相同，日志为
`tmp/enemy-opt-legacy-normal.out`。本轮保留该既有脚本断言，不能声称完整聚合矩阵通过，
后续 normal fixture 分支没有执行。

后续仍需评估剩余几何、顶点上传和角色准备；本轮没有引入多帧流水。
