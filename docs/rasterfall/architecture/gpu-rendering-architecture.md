# GPU 渲染架构

> 状态：当前
> 所有者：Rasterfall Core Host、Scene owner、Vulkan graphics
> 最近核对：2026-09-26

Rasterfall 的渲染入口为 CPU 软件渲染和独立 GPU Scene。GPU Compute Raster、mixed executor 及 Draw/Raster bridge 已退役。旧实现和诊断合同见[退役归档](../archive/gpu-compute-retirement/README.md)，不能作为当前设计依据。

两后端默认功能的描述、高级功能支持状态及未来游戏内控制终端的配置边界由[渲染 baseline 清点](../reference/rendering-baseline.md)定义；后端的绘制原语和资源优化可以不同。

## 帧所有权

`rasterfall/src/rf_core_host.c` 管理窗口、输入、固定步长循环的宿主边界，以及 CPU 或 Scene 模式的初始化、呈现和关闭。`rasterfall/src/rf_game_runtime.c` 组织 session、只读 Scene 来源冻结和帧提交。玩法真值由 Game/session 持有；渲染和 HUD 不写入玩法状态。

CPU 模式把 `toy_raster_cmd` 交给软件 renderer，在 CPU surface 上完成分层画面。该命令是 CPU 渲染合同，不属于已退役的 GPU Raster stream。

GPU 模式使用 `--renderer gpu-scene`（与 `--gpu-scene-play` 等价）。独立 Scene owner 冻结 world、角色、地图、天空、透明层、特效、viewmodel 和 HUD 来源，准备 graphics 资源并直接向 Vulkan Scene graphics 提交。`gpu/src/rf_gpu_vulkan_graphics.inc` 持有 Scene mesh/texture、skinning、render pass 与 swapchain present；`gpu/src/rf_gpu_vulkan_backend.c` 持有 Vulkan device、队列和通用 GPU service。Scene 帧不经过 CPU RasterCmd lowering、GPU Compute Raster、mixed executor 或桥接拷贝。

## Scene 资源与同步

Scene world 资源由 `rasterfall/src/rf_gpu_scene_world_gpu.c` 准备，角色与动态来源从只读 snapshot 提取。graphics resource cache 按 generation 更新和退休；正在提交的帧持有资源，完成后才能释放。角色 pose、IK、socket 与武器放置由 CPU 计算，GPU skinning 处理顶点。render-finished semaphore 和 swapchain image 的生命周期由 Vulkan presenter 管理。

窗口 resize、swapchain 重建及错误注入必须按 graphics owner 的完成/退休顺序处理。失败时传播帧错误，不把残缺 Scene 帧解释为成功。实现边界与复现入口见[Scene 工作流](../guides/gpu-scene-fixture.md)和[Windows Native](../guides/windows-native.md)。

## 当前验证

Windows 原生 package 是实机判断入口。`windows/NativeCodex.ps1 gpu-test` 提交 120 帧硬件 Scene 并打印 frame audit；`acceptance` 另做 normal-frame 和视觉 capture。`SCENE-SOURCE`、`SCENE-NATIVE`、`SCENE-WORLD-COST` 可检查提交来源、绘制、上传、队列同步与 present。旧命令、mixed draw、bridge 和常规 CPU framebuffer 读回应为零。离屏测试用于定位问题，不能代替实机 present、resize、窗口生命周期和长帧结论。

具体命令和结果判定见[GPU 验收与诊断](../guides/gpu-validation.md)。
