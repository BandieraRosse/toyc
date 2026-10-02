# GPU 渲染架构

> 状态：当前
> 所有者：Rasterfall Core Host、Scene owner、Vulkan graphics
> 最近核对：2026-09-26

Rasterfall 的渲染入口为 CPU 软件渲染和独立 GPU Scene。GPU Compute Raster、mixed executor 及 Draw/Raster bridge 已退役。旧实现和诊断合同见[退役归档](../archive/gpu-compute-retirement/README.md)，不能作为当前设计依据。

GPU 实时阴影、动态灯与 PBR/HDR 管线见[GPU 光照架构](gpu-lighting.md)，CPU 保留静态烘焙且不承担高级功能。两后端内容与游戏内控制终端的配置边界由[渲染 baseline 清点](../reference/rendering-baseline.md)定义；后端的绘制原语和资源优化可以不同。

## 帧所有权

`rasterfall/src/rf_core_host.c` 管理窗口、输入、固定步长循环的宿主边界，以及 CPU 或 Scene 模式的初始化、呈现和关闭。`rasterfall/src/rf_game_runtime.c` 组织 session、只读 Scene 来源冻结和帧提交。玩法真值由 Game/session 持有；渲染和 HUD 不写入玩法状态。

CPU 模式把 `toy_raster_cmd` 交给软件 renderer，在 CPU surface 上完成分层画面。该命令是 CPU 渲染合同，不属于已退役的 GPU Raster stream。

GPU 模式使用 `--renderer gpu-scene`（与 `--gpu-scene-play` 等价）。独立 Scene owner 冻结 world、角色、地图、天空、透明层、特效、viewmodel 和 HUD 来源，准备 graphics 资源并直接向 Vulkan Scene graphics 提交。`gpu/src/rf_gpu_vulkan_graphics.inc` 持有 Scene mesh/texture、skinning、render pass 与 swapchain present；`gpu/src/rf_gpu_vulkan_backend.c` 持有 Vulkan device、队列和通用 GPU service。Scene 帧不经过 CPU RasterCmd lowering、GPU Compute Raster、mixed executor 或桥接拷贝。

## Scene 资源与同步

天空已脱离 CPU canvas，由独立 compute pass 求值大气与远景体积云，随后在 SKY 层合成。
太阳共享、低分辨率缓冲与 slot 生命周期见[GPU 天空](gpu-sky.md)。

Scene world 资源由 `rasterfall/src/rf_gpu_scene_world_gpu.c` 准备，角色与动态来源从只读 snapshot 提取。graphics resource cache 按 generation 更新和退休；正在提交的帧持有资源，完成后才能释放。角色 pose、IK、socket 与武器放置由 CPU 计算，GPU skinning 处理顶点。render-finished semaphore 和 swapchain image 的生命周期由 Vulkan presenter 管理。

实验区投影、机器屏幕、信标和 HUD 的无纹理几何使用 20 字节颜色顶点；颜色与可选逐三角形透明度
随顶点上传，相邻且其余状态相同的三角形合为一次绘制。透明段保持来源顺序，不按材质或深度重排。
`texture[2]=256` 仅在颜色顶点上选择逐面透明度，复用不再参与光照的 `light_q8` lane；资源校验要求
其值为 0–255 且三角形三个顶点一致。零透明度模式仍表示不透明并写深度，不与透明绘制合并。
纹理层继续使用完整顶点格式；自发光文字、HDR 前背景与色调映射后的 HUD 保留各自语义。

动态展示层在上传前保守拒绝完全位于相机同一裁剪平面之外的三角形，跨近面的三角形留给硬件裁剪。
此入口仅处理不投射阴影的混合几何。graphics owner 另外按资源包围盒剔除主视图绘制，并在每张
阴影图各自的光空间独立剔除；镜头外的模型仍可投射镜头内阴影。GPU 蒙皮资源不使用 bind bounds
剔除。没有距离隐藏、小物件删除或模型降面。静态纹理过滤设置在每帧准备开始时读取一次。

Scene layer workspace 持有不随时间变化的显示几何缓存：完整 `toy_map_draw` 值作为失效键，包含文本、
颜色、像素间距、朝向、样式及世界坐标。缓存保留原始 quad 顺序和实际包围盒；相机改变只重新判断裁剪。
屏幕/文字变化立即重建，动态光束、旋转信标和机器活动仍走原逐帧路径。
每个 owner 最多缓存 65,536 个 quad，分配失败或超预算回退直接生成；owner 关闭统一释放。
共享生成器位于 `render/rf_display_geometry_cache.h`，该缓存不持有玩法状态、相机或 GPU resource。

启动环境切到 GPU Scene 时保留 Win32/SDL 窗口句柄，先释放 SDL 硬件呈现器并建立软件呈现器，再为同一窗口创建 Vulkan surface；启动页仍由软件画布呈现，进入游戏后由 GPU Scene 接管。窗口 resize、swapchain 重建及错误注入必须按 graphics owner 的完成/退休顺序处理。失败时传播帧错误，不把残缺 Scene 帧解释为成功。实现边界与复现入口见[Scene 工作流](../guides/gpu-scene-fixture.md)和[Windows Native](../guides/windows-native.md)。

正常交互帧由 Game Runtime 以 120 FPS 节流。其 Vulkan swapchain 优先选 immediate，其次 mailbox，均不可用时退回必备的 FIFO；前两者允许在 60 Hz 显示器上继续采样并提交更多帧，immediate 可能出现画面撕裂。固定帧诊断仍用 FIFO。该呈现选择不改变 GPU service、资源退休或固定逻辑步。玩法状态的双 tick 展示插值见[运行时架构](runtime.md)。

## 当前验证

角色提交支持目录中的无附件 body 预览，与正式队员共用 `rf_gpu_scene_actor_gpu_prepare`、
WORLD 深度、GPU skinning 和退休链。三角形展开数据以 65,535 个顶点为一块，最多 16 块；
跨块 primitive 拆成同材质 draw，骨架 palette 保持完整，材质和双面标志不改变。
这保留现有 graphics 顶点容量，不提高设备入口上限。当前每个实例分别缓存 GPU bind 数据，
同内容跨实例的 GPU buffer 共享仍未实现；逐帧只重新打包 palette 和 draw，资源 generation
或 bind-normal 策略变化才重建静态顶点。角色材质仍只支持不透明分色，纹理/MASK/toon 扩展待实施。

Scene 默认将整数存储转换为浮点后执行模型/相机变换，WORLD（包括静态建筑模型）统一使用 D32 原生 reversed Z（64/z），
不再对倒数深度取整；旧整数兼容 draw 已拒绝。角色 body 消费 RFM2 `position_scale`，局部高精度坐标和骨骼保持同一单位，
在 graphics 顶点阶段换为 RFU；compute skinning 仍在局部存储格上舍入。屏幕空间层保留独立深度语义。graphics draw 增加第八个 16 字节 push-constant lane，保持 Vulkan 的 128 字节最低保证。

可选角色材质从已导出的 visual role 选择平滑、柔和或无光照，透视插值逐顶点法线；只允许顺序展开
三角形资源使用该法线读取约定。普通角色使用平滑 PBR，不按私有角色名猜测。
渲染终端拥有用户请求，Scene preparation 在下一帧消费 presentation-only 设置；开关只选择新光照下的材质响应，不恢复旧烘焙。
静态纹理另有可选 bilinear/repeat，与角色材质开关独立。固定相机、分色、旧量化及绘制顺序诊断见
[角色保真](../guides/character-fidelity.md)。

Host Rack V2 的风扇、活动灯、实时负载条和槽位数字由 prop presentation 生成 WORLD 几何。
独立 Scene layer 消费冻结的 prop 值和平台采样，CPU 使用同一槽位规则。静态模型不随指标更新，
未启用候选柜在 Scene 资源准备时跳过；机柜数量和编号见 [Host Rack V2](../reference/host-rack-v2.md)。

Windows 原生 package 是实机判断入口。`windows/NativeCodex.ps1 gpu-test` 提交 120 帧硬件 Scene 并打印 frame audit；`acceptance` 另做 normal-frame 和视觉 capture。`SCENE-SOURCE`、`SCENE-NATIVE`、`SCENE-WORLD-COST` 可检查提交来源、绘制、上传、队列同步与 present。旧命令、mixed draw、bridge 和常规 CPU framebuffer 读回应为零。离屏测试用于定位问题，不能代替实机 present、resize、窗口生命周期和长帧结论。

具体命令和结果判定见[GPU 验收与诊断](../guides/gpu-validation.md)。
