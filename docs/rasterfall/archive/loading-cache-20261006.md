# 加载缓存与首次辅助镜头准备现场

> 状态：历史现场
> 日期：2026-10-06
> 原因：记录本次 Windows NVIDIA 硬件路径的限定实现和对照，不作为当前设计合同。
> 当前入口：[GPU 渲染架构](../architecture/gpu-rendering-architecture.md#入图预热与多视图资源所有权)、[光照指南](../guides/gpu-lighting.md#加载与首次辅助镜头准备)

用户报告前哨站启动约 5 秒，入图后明显卡顿一次，边缘站点疑似同样存在。现场分开管线创建、
地图预热与首次 WORLD AUX，不把缓存刷新 pass 或整个诊断进程的运行时间当成加载时间。

## 实现

- 主/AUX 的 graphics 和 compute 创建共用 device 级 `VkPipelineCache`，正常关闭持久保存。
  缓存检查设备、驱动、UUID、长度和校验和，损坏/失配回退空缓存，不可写仍可启动。
- 接收点投影完整一轮后位结果不变即结束，保持原最多 64 轮算法、拓扑和间距。
  `RF_GPU_RECEIVER_PROJECTION_FIXED=1` 为原算法对照。
- WORLD AUX 在 GPU 上复制主场已初始化的六个 GI/layout 缓冲，保留独立动态照明；仅静态
  建筑 AS/BVH 登记 reader 借用。源替换/关闭解除引用并使子 GI 失效，WEAPON 预览先解除借用。
  保留 compute→transfer→shader 屏障和已有同步退休。
- 预热计时分别报告主准备、光照准备、主 GPU、GI、receiver、AUX 创建及备用角色池。
  子区间有重叠，不能累加，完整启动仍由 `gpu-startup-total` 计量。

## 加载对照

同一 staged 资产、1280×720、正常时钟、关闭 validation，`--skip-boot --renderer gpu-scene`
指定地图并运行 24 帧。旧版是本轮加载改动前、已包含 128 B/sample 与蒙皮合并的 F9CF 基线，
新版本默认开启缓存和投影收敛退出。每地图两轮；以下只比较 `SCENE-PREWARM`。

| 地图 | 旧版预热两轮 | 缓存命中新版两轮 |
| --- | --- | --- |
| 前哨站 | 10.155 / 9.799 s | 2.796 / 2.755 s |
| 边缘站点 01 | 9.435 / 9.399 s | 2.580 / 2.664 s |

渲染器首次创建原为约 5.6–6.2 s；持久缓存命中后约 0.13–0.15 s。相同 device 的后续
AUX 创建约 0.07 s。缓存冷启动仍约 6 s 首次编译，退出生成约 0.73 MiB 驱动数据后再复用。
投影 CPU 准备在前哨站约 1.2→0.33–0.34 s，在边缘站点约 0.84→0.146 s；采样数量和间距不变。
边缘站点备用角色池还约 0.41–0.45 s，主资源准备及首个 native swapchain 也仍有开销。
这些是本机诊断阶段，不能声称用户的完整 5 秒加载已等比例缩短。

证据：`tmp/loading-baseline-v3/`、`tmp/loading-persistent/`、`tmp/loading-final/`、
`tmp/loading-timing-review.json`；过程总墙钟还包含 24 帧运行及关闭，没有用于加载结论。

## 首次镜头对照

同版本、同热管线缓存、前哨站 1280×720，正常时钟、剧情镜头打开、8 帧审计，单独切换
`RF_GPU_AUX_LIGHTING_CLONE=0/1`。首次出现镜头的第 3 帧从 681.249 ms 降至 84.237 ms；
其中 GPU GI 复制约 5.4 ms、31,252,528 B。关闭复用时 AUX 额外构建完整建筑及两场探针、接收
拓扑，并执行首次完整求解；开启时仅主场构建一次。这是限定的单次对照，未作多轮统计。
仍有首次动态几何、UI 和资源准备，不能声称所有入图卡顿消失。

正常双镜头工具在 1280×720 运行 120 帧预热和 120 样本，`UI-PERF valid=1`，两路各
21 次实际刷新，分别复制约 5.3 / 5.4 ms，未再构建 AUX 接收拓扑。稳态结果只证明正常运行，
不用于首次卡顿结论。证据：`tmp/loading-dual-cold-reference/`、`tmp/loading-dual-warm-clone/`、
`tmp/loading-real-dual/`。

## 验证与范围

Windows native 构建和 graphics fixture 的 `-Werror` 构建通过。Core + Synchronization
开启时完整光照回归通过；新增矩形房间、斜面案例的原 64 轮与收敛投影拓扑/位置逐位一致。
GPU clone 回归验证颜色/深度相同、重复调用不再提交、子灯关闭不影响主灯、无效源替换保留
绑定、源清除解除子绑定。完整 graphics 回归 7146 检查通过，没有 VUID 或同步 hazard。
实际通讯镜头 8 帧 native 同步检查也通过，独立桥接/读回/mixed 计数为零。
World cycle 120 帧及 acquire/record/submit/present 五种故障注入通过 Core + Synchronization
检查。证据：`tmp/loading-world-sync/` 与 `tmp/loading-faults-sync-v2/`。
另用正常 dual-view 工作流打开实际剧情与 RTS 单位两路 WORLD AUX，保持 Core + Synchronization
开启，确认两次独立 GPU clone 后继续运行 15 秒，再发窗口关闭事件。6 个 native 主帧正常返回，
关闭 exit=0，无 VUID/同步 hazard；覆盖首次双路初始化和关闭，未代替长时间稳态同步采样。
证据：`tmp/loading-dual-close-sync/`。早先控制脚本读活动日志的共享模式失败已修复，失败运行
不计作游戏验收。

正常管线缓存异常专项覆盖有效文件、截断、校验和损坏、驱动失配、缺少目标目录和仅内存缓存，
六次均正常呈现；不兼容数据拒绝读入，不可写保存报告 unavailable。证据：
`tmp/loading-lighting-sync/`、`tmp/loading-graphics-sync/`、`tmp/loading-dual-sync-clone/`、
`tmp/loading-cache-cases/`。

三个固定视角 `frontier-floor-2`、`frontier-stairs-upper`、`outpost-light-1f` 的 1280×720
native 画面与加载改动前 FP16 基线逐像素一致（最大通道差 0），保持同空间精度。
证据：`tmp/optimization-fp16-images/` 与 `tmp/loading-final-images/`。

性能数字只适用于本机 NVIDIA 硬件路径，不覆盖其他设备、驱动或首次清缓存启动。
共享源码的非 Windows 分支未作运行验证；本次 WSL 查询返回访问拒绝，未用其代替 native 验收。

## 交付

运行目录的 exe 已更新，已有默认管线缓存保留。zip 沿原 package 范围重新生成，只包含
`rasterfall.exe` 与 `rasterfall/`，不携带本机 `build/` 管线缓存；重新解压的新环境需要首次生成。
本轮已测试 exe SHA-256 为 `6560BD98B5E956576434B22BF5E7ADC3D7A5C754579CE7F3C4ED07266A032637`。
zip 内 exe 与运行目录逐字节匹配，检查记录为 `tmp/loading-package-final.json`。
