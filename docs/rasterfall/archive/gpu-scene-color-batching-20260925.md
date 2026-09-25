# Scene 唯一顶点变换与逐面颜色合批现场

> 状态：历史测量现场；2026-09-25 单次设备诊断，不作为稳定契约或正式 FPS 签收
> 当前入口：[活动计划](../plans/README.md)、[GPU 架构](../architecture/gpu-rendering-architecture.md)
> 复现：[Scene 工作流](../guides/gpu-scene-fixture.md)

## 实现范围

- 普通感染体按顶点索引复用单次身体提取中的世界位置与旋转法线；stamp 不跨身体复用。
- Scene owner 保留敌人 CPU 工作区，逐身体只清活动计数和光照缓存。
- 显式 Scene color resource 在既有 56 字节顶点布局内传递 Q8 光照与 RGB24；同一三角形必须同色。
  敌人和程序角色按连续双面策略合批，三角形及透明来源顺序不变。普通资源的 UV、蒙皮和 mixed ABI 不变。
- 新增来源/冻结/pose/地图缓存/prepare 余项，以及 record/acquire/queue-submit/present 墙钟。
- 同包诊断开关和报告支持 VertexTransform、ColorDraws、EnemyPipeline。颜色合批按三角形和来源核对 workload。

## 设备与方法

Windows native，RTX 3050 Laptop GPU，默认 1280×720，required native independent Scene。
正常地图及密集组件矩阵各做三轮 AB/BA，每次 64 帧，排除前 8 帧。表中每个统计量均先按单轮计算，
再取三轮中位数；不是合并样本分位数，不是低扰动五轮产品签收。
采样前后软件温度降频计数均为 `4818212944 us`，未增长，状态 Not Active；温度约 77–81°C。

最终成本与生命周期包 SHA256：
`DF60016B75D43680AF3D6F67AE1AFD8B3F37A2A28D3CF4C4F6C0C16021BFC062`。
EnemyPipeline 的 legacy 同时开启逐角点变换和旧颜色 draw，optimized 同时关闭；两侧均保留 CPU 工作区复用。
该对照不能量化工作区复用本身的收益，也不能把上一轮不同设备负载下的帧时间作为此次 baseline。

## 测量

单位 ms，旧路径 → 优化路径；这里“旧路径”仅指同包上述两个诊断开关，不是 CPU/mixed renderer。

| 场景 | 循环 median | P95 | P99 |
| --- | ---: | ---: | ---: |
| near 0 | 16.438 → 16.506 | 17.247 → 17.647 | 49.848 → 49.915 |
| near 30 | 18.120 → 16.050 | 20.972 → 18.480 | 23.816 → 19.295 |
| near 60 | 26.138 → 21.421 | 34.043 → 28.146 | 38.180 → 31.646 |
| 密集组件 near-heavy 60 | 24.035 → 21.855 | 27.579 → 26.688 | 29.423 → 29.398 |

near 60 每帧平均 draw 从 23212.839 降为 1555.607，敌人及程序角色三角形均为 75523.357；
来源数量、组件 draw 和逐帧三角形序列一致。密集地图保持 134 个 object、128 个组件 prop draw，
60 敌人中包含六个 Tank、六个 Charger。没有删敌人或几何来获得收益。

near 60 的分段均值（三轮均值的中位数）：

| 分段 | 旧路径 | 优化路径 |
| --- | ---: | ---: |
| 敌人/程序角色准备 | 12.589 | 11.426 |
| 几何提取（属于上项） | 7.696 | 7.424 |
| 资源更新（属于上项） | 3.994 | 3.857 |
| draw 构造/预检（属于上项） | 0.837 | 0.093 |
| 分层准备 | 1.368 | 0.624 |
| native record/acquire/submit/present/retire | 3.732 | 1.421 |
| 命令预检/录制（属于上项） | 2.501 | 0.541 |
| GPU 整个 Scene draw（不可再加到墙钟） | 0.930 | 0.596 |

独立的唯一顶点变换三轮实验中，几何提取均值 7.563 → 7.007 ms，循环 median 28.295 → 26.736 ms，
但 P95/P99 没有改善。组合实验不能直接叠加这些数字。
密集特感场景的几何提取均值 6.965 → 7.458 ms，没有改善；其整体收益主要来自合批，不能泛称所有 CPU 段均下降。

新增细分把 near 60 优化后的 `other` 约 5.50 ms 定位为：循环前段约 2.88 ms（其中 update 约 2.61 ms）、
动态来源约 0.55 ms、冻结约 1.40 ms、pose 约 0.53 ms、地图缓存约 0.008 ms、prepare 余项约 0.066 ms。
这些是各轮均值的中位数，不能要求相加严格等于另一统计量；中间日志与编排还有少量余量。
轻负载约 40 ms 的长等待仍位于 retire，acquire/present API 墙钟较小，根因尚未确定。
仍需优化几何/光照和全量动态顶点更新；当前不宣称稳定 60 FPS。

## 验证与证据

- 原生 package 构建、完整 `--logic-test` 通过。
- `gpu-graphics-test` 默认图形回归通过 24728 checks；`--depth-gate` 通过，零失败用例。
  新增合批与分拆 draw 的 color/depth 对照，覆盖更新、近裁剪、透明及非法模式/颜色/光照拒绝。
- near、enemy-special、enemy-death、enemy-fade、enemy-tongue、actor-procedural、frame-effects、thin-far：
  两侧第 4 帧 PPM 逐字节一致；near 使用 60 敌人。near、fade、effects、procedural 各 8 帧 native validation/sync 通过。
  此组截图包为 `92F6D0BF0DF2B04389111FFD1B6C3310D894157CC40F3C6337877990E57B1A22`；
  最终包只在此后补正 EnemyPrep 诊断开关应同时禁用新变换缓存，两侧截图模式的执行行为未改变。
- 两套性能矩阵均要求零 bridge/readback/mixed execute，且逐帧来源及三角形 workload 一致。
- 最终包 `gpu_scene_play.ps1 -Stage All -Frames 160` 加载 validation/sync 通过：outpost 4 帧、
  真实输入/resize 80 帧、world-cycle 120 帧、西向死亡 100 帧、特效容量 4 帧、真实波次 160 帧，
  以及 acquire/record/submit/present 五类故障。注入 record/submit 失败按预期非零退出，其余正常恢复。
- `git diff --check` 与文档检查通过；本轮不运行编译器测试或更新 bootstrap。

原始现场留在本地，不提交生成物：

- `tmp/scene-transform-20260925`：唯一顶点变换单项与截图。
- `tmp/scene-color-20260925`：图形、逻辑、截图、同步与 GPU 状态日志。
- `tmp/scene-enemy-pipeline-20260925`：正常地图三轮对照、`report.json`、`compact.txt`。
- `tmp/scene-enemy-pipeline-dense-20260925`：密集组件及特感三轮对照。
- `tmp/scene-enemy-lifecycle-20260925`：最终包的原生生命周期专项。
