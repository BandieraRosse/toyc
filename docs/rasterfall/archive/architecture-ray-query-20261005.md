# 静态建筑硬件 Ray Query（2026-10-05）

> 状态：历史现场；静态建筑接入及限定原生验证完成
> 当前合同：[GPU 光照](../architecture/gpu-lighting.md)、[验证指南](../guides/gpu-lighting.md)

用户要求先提交三层布光，再将建筑可见性查询交给硬件光追。布光基线提交为 `9327ee6`。
本轮增加 Vulkan 1.2 能力协商、KHR Ray Query、设备地址内存、静态 BLAS/TLAS 和独立片元变体；
保留软件 GPU 查询供不支持设备使用及性能对照。光源布局、亮度、动态阴影和 CPU renderer 不变。

前哨建筑输入 52297 个图元，真实 BOX 转表面三角形后为 53738 个三角形。
RTX 3050 Laptop 上结构保留 3763072 字节（约 3.59 MiB）；五次固定视图进程的建筑构建
约 13.5–17.0 ms，另有空 TLAS 初始化。这是加载开销，正常帧不重建。

## 原生结果

- Windows native build 成功，无编译 warning/error；最终 EXE SHA-256：
  `778AE58B5DCD519DD2C883861BF34B1FFB72D6A476E0577B96CEB8666F77FD25`。
- RTX 3050 上强制 hardware 和 software 的 `--gpu-lighting-test` 均通过。
  覆盖 point、超过动态阴影名额的 spot、太阳、有限射线、空世界、双面、resize 后保留，
  以及拒绝无效替换后原结构仍可用；三个 BOX/三角形对照 changed=0、max_error=0。
  日志为 `tmp/ray-query/hardware-10de-regression.log` 和 `software-10de-regression.log`。
- AMD Radeon 集显 `auto` 实际选择 software，光照回归通过；强制 hardware 明确拒绝，退出码 3。
  见 `auto-1002-regression.log`、`hardware-1002-regression.log`。没有静默冒充硬件。
- 五个固定视图的 hardware/software 捕获均正常退出，分别保存在
  `tmp/ray-query/hardware/` 和 `software/`。B1、楼梯、研究翼逐像素相同；一层 9 个像素不同，
  二层 13 个像素不同，最大单通道差分别 59、52（每图 921600 像素）。差异位于左上建筑斜边，
  与两种求交的边界容差相关；未宣称逐位一致或所有视角无误差。五张硬件截图均人工查看。
- 临时下载并使用 MSYS2 Khronos validation layer 1.4.363.0 和配套 SPIRV-Tools，未安装到系统。
  `validation-lighting.log` 与 `validation-outpost.log` 证明 layer 和 Synchronization 启用；
  光照回归及前哨固定视图 12 帧正常呈现通过，没有 VUID、SYNC-HAZARD 或 Validation Error。
  12 帧同步诊断不用于性能结论；性能测量另行关闭 validation。

## 1080p 成本

同一 EXE/map、RTX 3050 Laptop、一层固定视图、1920×1080、quarter 天空、冻结 sky time、
正常实时时钟及 120 FPS 上限；每次预热 120 帧、采样 240 帧。无截图、无 validation。
`tools/gpu_outpost_perf.ps1 -CompareArchitecture` 按软件/硬件、硬件/软件、软件/硬件交替三轮。
配置与逐次 stdout/stderr、报告见 `tmp/ray-query/perf/`。

| 路径 | 整帧 P50，三轮 ms | 整帧 P95，三轮 ms | GPU P50，三轮 ms |
| --- | --- | --- | --- |
| 软件 BVH | 25.319 / 25.583 / 25.493 | 26.653 / 29.397 / 26.738 | 17.949 / 18.091 / 18.212 |
| 硬件 Ray Query | 17.837 / 17.714 / 17.871 | 19.362 / 18.812 / 18.803 | 10.715 / 10.720 / 10.791 |

按三轮各自 P50 的中位数，GPU 耗时下降约 40.7%，整帧下降约 30.0%。这些是此机位的结果，
不是全游戏基线；整帧仍超过 16.67 ms，尚未稳定达到 60 FPS。旧太阳和两盏 spot 的 shadow pass
仍然运行，动态物体未纳入硬件结构；本轮未实现 GI、软阴影或动态 BLAS。

## 综合诊断的既有失败

扩大验证时发现两项失败，未将其计为通过：

- `--gpu-scene-native-fixture` 在 frame 1 的 `scene_skin_diff` 断言退出 3，
  12 个位置分量相差 1 RFU，法线/UV 无差异；不是 Vulkan validation 报错。
- `--gpu-world-cycle-test` 完成前哨→Campaign（frame 30），进入 WHU 后在第 61 帧
  `SCENE-WORLD-GPU normal audit failed`，退出 1；硬件结构已在 Campaign 重建成功，未见同步错误。

将 `9327ee6` 的完整 GPU 源码/头与 shader 从 Git 导出到临时目录，重编后端，和未改动的
游戏对象链接成同 package 下的对照程序；两项失败均在相同位置复现。证据为
`tmp/ray-query/baseline-native.log`、`baseline-world.log`，新路径为
`validation-native.log`、`validation-world.log`。本轮未放宽断言或修改这些原有失败，
不能据专项通过宣称全部综合诊断通过。
