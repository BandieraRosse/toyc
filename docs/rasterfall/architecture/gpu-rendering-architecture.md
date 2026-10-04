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
动态层在每个既有 chunk 内分别紧凑打包颜色和纹理顶点，仅上传实际消费的格式；常驻显示 packet
的顺序标记不进入动态顶点缓冲。draw 保存各自流的索引偏移，原来源顺序、透明叠加顺序和 chunk
容量不变。没有纹理或颜色来源的 chunk 跳过对应资源更新，后续重新出现时按当前数据更新。

动态展示层在上传前保守拒绝完全位于相机同一裁剪平面之外的三角形，跨近面的三角形留给硬件裁剪。
此入口仅处理不投射阴影的混合几何。graphics owner 另外按资源包围盒剔除主视图绘制，并在每张
阴影图各自的光空间独立剔除；镜头外的模型仍可投射镜头内阴影。GPU 蒙皮资源不使用 bind bounds
剔除。没有距离隐藏、小物件删除或模型降面。静态纹理过滤设置在每帧准备开始时读取一次。
资源包围盒保存真实局部 min/max，中心和半径按顶点相同的旋转、缩放、竖直 pivot 变换并保留
2 RFU 裕量，避免将偏心模型强行扩展至局部原点另一侧。动态上传成功后原子替换边界；主视图和
各阴影图共享该保守边界，但分别判定可见性。屏幕空间和 GPU 蒙皮来源继续跳过此资源级剔除。

Scene layer workspace 将不随时间变化的显示几何保留为 GPU 资源及有序 draw 段：完整 `toy_map_draw`
值作为失效键，包含文本、颜色、像素间距、朝向、样式及世界坐标。相机改变只更新包围盒裁剪与 draw
相机参数，不再逐三角形生成、遍历、打包和上传。内容变化重新生成该对象的几何，容量足够时原位
更新颜色顶点，复用索引、缓冲及描述符；仅容量增长时重建 GPU 资源。相对动态来源的顺序、局部
坐标原点和硬件近裁剪保持一致。动态光束、旋转信标和机器活动仍逐帧更新。
每个 owner 的 GPU 显示缓存最多保留 131,072 个三角形，按实际分配容量而非当前可见数量计费；
CPU 缓存分配失败或超预算时回退原 CPU
quad 路径（65,536 quad 上限，继续不足时直接生成）。GPU 资源创建失败仍传播帧错误。
共享生成器位于 `render/rf_display_geometry_cache.h`；GPU packet 由 layer workspace 持有，owner 关闭
时统一释放。缓存属于渲染资源，不能引用玩法状态，也不按地图名称或实验区身份决定是否生效。

敌人和程序角色的完整冻结值、world generation、顶点格式及 CPU 光照模式决定是否复用几何和 draw
段；只有输入完全相同且无需 CPU 顶点光照时跳过提取和上传，相机参数仍逐帧更新。动画采样和表现
历史照常推进，主视图与阴影仍各自剔除。GPU 蒙皮仅在 bind 未变、palette 和顶点数完全一致且上次
提交成功时跳过重复上传与 dispatch；取消的更新不能成为有效缓存。可映射的 bind/palette 缓冲保持
映射至资源销毁，非 coherent 内存仍显式 flush。没有跨实例共享姿态或放宽角色包围盒规则。

Runtime 持有 world freeze cache，以 Runtime Map/level owner、world generation 和完整 authored draw/prop
值校验静态结果，跳过重复 projection 查找、静态校验及重建。终端通道文本和 prop presentation 每帧
刷新；消费者仍得到独立值快照，不持有缓存或地图指针。WORLD/prop ID 唯一性使用有界哈希表和完整
字符串冲突比较。缓存随 runtime 关闭释放，世界身份改变后失效，分配失败回退无缓存冻结。

当前 Scene 仍为单槽：上一帧退休后才能改写资源；蒙皮批次提交后等待完成，native present 后立即
retire 等待帧 fence。本节的复用不引入跨帧在途资源或多帧 pipeline。

启动环境切到 GPU Scene 时保留 Win32/SDL 窗口句柄，先释放 SDL 硬件呈现器并建立软件呈现器，再为同一窗口创建 Vulkan surface；启动页仍由软件画布呈现，进入游戏后由 GPU Scene 接管。窗口 resize、swapchain 重建及错误注入必须按 graphics owner 的完成/退休顺序处理。失败时传播帧错误，不把残缺 Scene 帧解释为成功。实现边界与复现入口见[Scene 工作流](../guides/gpu-scene-fixture.md)和[Windows Native](../guides/windows-native.md)。

正常交互帧由 Game Runtime 以 120 FPS 节流。其 Vulkan swapchain 优先选 immediate，其次 mailbox，均不可用时退回必备的 FIFO；前两者允许在 60 Hz 显示器上继续采样并提交更多帧，immediate 可能出现画面撕裂。固定帧诊断仍用 FIFO。该呈现选择不改变 GPU service、资源退休或固定逻辑步。玩法状态的双 tick 展示插值见[运行时架构](runtime.md)。

## 通讯镜头、单位镜头与设备预览

`rf_gpu_scene_layers_input.aux_view` 提交通讯/设备视图，`unit_view` 提交 RTS 单位视图，两路可同时显示。
每路分别保存稳定来源 ID、generation、镜头变换、视频矩形、内部尺寸和刷新频率；UI 布局只改变最终合成矩形。
`render/rf_gpu_scene_aux.inc` 按槽调度，Scene probe 的 `aux[2]` 各自持有持久子 owner、状态、刷新时刻与重试次数。
默认刷新为 12 Hz，最多 15 Hz，子视图清除两路嵌套请求，不递归生成辅助视图。
通讯直接按 240×360 的 2:3 竖画面渲染，合成矩形保持同一比例，禁止把横画面非等比拉伸进竖框；
设备预览默认从 320×180 开始，再按预览矩形调整内部高度。实际性能签收依实机采样，不把这些值视为性能保证。
槽 0 在设备打开时优先显示设备预览，否则显示剧情；槽 1 独立显示 RTS 单位，不能抢占剧情画面。
单位镜头复用剧情取景参数、
240×360 竖画面及低频调度，来源身份切换后等新帧再标记 live，具体见 [RTS 核心指挥](rts-command.md)。

WORLD 模式使用本帧同一份 world、actor、敌人、设备和 effects 冻结值，以实体镜头重新准备完整
小画面；不二次推进动画历史，不缓存背景图像。天空、动态光照、制造动画、角色和特效继续更新。
主玩家手电保留原玩家镜头的位置和方向。`rf_gpu_scene_enemy_aux_camera` 与 `rf_gpu_scene_enemy_unit_camera`
在来源冻结前保留任一路镜头附近的敌人；普通 AI 来源本身不按主镜头裁掉，FPS 玩家额外身体只供辅助镜头消费。实体外壳按
同一镜头轴向生成，在主场景可见，对其自身镜头排除；窗口收起时外壳仍可保留。

WEAPON 模式复用制造展示的真实武器模型、物理适配和材质，按完整资产 bounds 居中取景，旋转只改变
展示变换，不创建成品或修改制造任务。初始采用侧面略偏三分之四的视角，按变换后的八个 bounds 角点
适配横纵视域和近裁剪；独立均匀补光与 `rf_gpu_graphics_scene_background` 的暗蓝灰线性 HDR 清屏色
只属于模型展示。WORLD 通讯镜头仍绘制真实现场背景。模型与世界几何按各自资源 owner 缓存；隐藏/恢复不重复创建
大型 GPU 资源。当前不提供任意角色、纹理模型或通用场景编辑器预览合同。

Vulkan 的 `rf_gpu_graphics_scene_offscreen` 只写 device-local 颜色/深度/HDR attachment，不申请
swapchain 图像，不读回 CPU。`rf_gpu_graphics_scene_video_at` 独立绑定或清除两个槽，原 `scene_video` 保留为槽 0 接口。
主视图 tone map 后、HUD 之前以 GPU blit 顺序合成两路小画面；清除一路不能影响另一路。
UI 必须为各矩形保留透明内容区。诊断 capture 才按需分配 staging readback buffer。
子镜头与主 owner 遵守现有单槽退休合同；辅助提交同步等待完成，正常帧没有 Raster bridge 或 CPU
framebuffer 往返。销毁先排空主 owner，再释放其仍可能引用的子镜头图像。

收起/关闭立即解除合成并停止辅助绘制，恢复和来源代际变化要求新帧。失败清空视频，按一秒间隔最多
重试三次，状态为 `UNAVAILABLE`，不把旧画面标作实时；重新打开可以重新尝试。每槽的 `status` 提供状态、
累计刷新次数、最近真实刷新时间、CPU 墙钟、GPU 时间戳和上传字节。CPU 后端应显示视频不可用并继续文本会话。

`gpu/src/rf_gpu_graphics_test.c` 的 `RF_GPU_AUX_TEST=1` 分支验证 GPU 图像合成、真实目标更新、HUD
覆盖顺序、双路共存与独立隐藏、目标/几何复用以及辅助 owner 零读回和零 bridge；完整 graphics 回归也包含该项。
它不代替近处/远程 NULL、窗口布局与 native 生命周期的实机验收。

## 当前验证

角色提交支持目录中的无附件 body 预览，与正式队员共用 `rf_gpu_scene_actor_gpu_prepare`、
WORLD 深度、GPU skinning 和退休链。三角形展开数据以 65,535 个顶点为一块，最多 16 块；
跨块 primitive 拆成同材质 draw，骨架 palette 保持完整，材质和双面标志不改变。
这保留现有 graphics 顶点容量，不提高设备入口上限。当前每个实例分别缓存 GPU bind 数据，
同内容跨实例的 GPU buffer 共享仍未实现；逐帧只重新打包 palette 和 draw，资源 generation
或 bind-normal 策略变化才重建静态顶点。

角色与附件可使用不透明基础色纹理及粗糙度、金属度常量。RFCHAR 的显式 RFM2 v15/MAT1
合同见[角色资产](../reference/character-assets.md)；旧 v14 仍可读，MASK/toon 与完整角色包尚未实现。
每个 mesh 最多八张 1024² RGB/RGBA 基础色图。Scene 资源首次上传时由 CPU 在线性光空间生成
完整 mip 链，再上传到 device-local texture set；shader 做 clamp/bilinear/trilinear，纹理与基础色
因子在线性空间相乘后进入统一 PBR。RGBA 的 alpha 在本轮 OPAQUE 合同中忽略。
同一实例的几何 chunks 以引用计数共享纹理缓冲，稳定帧不重复上传；资源更新和释放遵守退休规则。
这不等于跨实例共享纹理缓存。静态世界既有 repeat 过滤路径保持独立。

独立蒙皮衣物复用身体冻结后的完整 palette 和 body-to-world，先验证骨架 rest/顺序/角色映射一致，
再与身体一同提交。被衣物覆盖的 body material 只在该实例绘制中隐藏，不改共享资源。

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
