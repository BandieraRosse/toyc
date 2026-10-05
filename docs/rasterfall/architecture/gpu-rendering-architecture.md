# GPU 渲染架构

RTS 楼层 V1 在 lighting upload 尾部增加建筑 X/Z bounds 与世界层顶 Y/amount；普通 Scene
fragment 按这份只读主镜头状态执行有序抖动 discard，天空和屏幕 HUD 跳过，地下层也保留完整 UI。shadow shader 不剖切，静态实例及
深度仍使用既有世界资源。AUX 的 world_only lighting 不复制主视图剖切参数，因此单位与通讯镜头
保留完整空间。细则见 [RTS 楼层](rts-command.md#建筑与楼层-v1)，生成 shader 同步维护 SPIR-V。

静态 ramp mesh 与 CPU 绘制共同消费 `thickness`、`steps`：悬空楼梯只生成有限厚度的踏步或斜板，
底面随坡面移动，不能把上层楼梯侧面延伸到地面。未设置厚度的旧地图保留原有实心坡道。

> 状态：当前
> 所有者：Rasterfall Core Host、Scene owner、Vulkan graphics
> 最近核对：2026-10-05

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
主 owner 的 GPU 显示缓存最多保留 131,072 个三角形；WORLD 子视图借用同一缓存。独立 owner 的上限也为 131,072 个三角形，按实际分配容量而非当前可见数量计费；
CPU 缓存分配失败或超预算时回退原 CPU
quad 路径（65,536 quad 上限，继续不足时直接生成）。GPU 资源创建失败仍传播帧错误。
共享生成器位于 `render/rf_display_geometry_cache.h`；GPU packet 由 layer workspace 持有，owner 关闭
时统一释放。缓存属于渲染资源，不能引用玩法状态，也不按地图名称或实验区身份决定是否生效。

敌人和程序角色的完整冻结值、world generation、顶点格式及 CPU 光照模式决定是否复用几何和 draw
段；只有输入完全相同且无需 CPU 顶点光照时跳过提取和上传，相机参数仍逐帧更新。动画采样和表现
历史照常推进，主视图与阴影仍各自剔除。GPU 蒙皮仅在 bind 未变、palette 和顶点数完全一致且上次
提交成功时跳过重复上传与 dispatch；取消的更新不能成为有效缓存。可映射的 bind/palette 缓冲保持
映射至资源销毁，非 coherent 内存仍显式 flush。没有跨实例共享姿态或放宽角色包围盒规则。

敌人几何和 GPU 容量槽按经过范围及严格递增校验的 `source_slot` 索引，避免前一来源消失后，
未变的后续 body 因压紧序号改变而搬入其他容量槽。程序角色使用 `RF_GPU_SCENE_ENEMY_CAPACITY`
加当前程序来源 ordinal 的独立区域；该区域内部仍可压紧，不将 ordinal 当作永久角色身份。
主视图与 `shared_parent` 查询使用相同索引，完整冻结键仍验证槽复用和世界变化。当前来源顺序、
透明顺序及裁剪不变；容量增长沿原接口重建，缩小时只更新活动顶点/索引数。借用资源仍属于创建
owner，全部 reader 退休后才能更新或释放；关闭路径遍历原有完整槽数组及 cached runs。
稀疏索引不增加数组或缓存结构，但可能比压紧序号保留更多已用而当前闲置的资源，沿现有 probe
关闭路径统一释放；仅 world generation 改变会使冻结键失效，不承诺释放全部容量槽。
每 owner 的顶点及索引理论 payload 上界为槽数乘现有最大三角形数乘
`3 * (sizeof(struct rf_gpu_scene_color_vertex) + sizeof(uint32_t))`：当前143槽、每槽4096三角形约40.22MiB。
这是理论 payload 界限，不是实测驻留量；还需另计可选 staging、cached runs、分配对齐、纹理和
描述符，独立拥有资源的子 owner 也各自计费。本合同不声明普通帧毫秒或 FPS 收益。

Runtime 持有 world freeze cache，以 Runtime Map/level owner、world generation 和完整 authored draw/prop
值校验静态结果，跳过重复 projection 查找、静态校验及重建。终端通道文本和 prop presentation 每帧
刷新；消费者仍得到独立值快照，不持有缓存或地图指针。WORLD/prop ID 唯一性使用有界哈希表和完整
字符串冲突比较。缓存随 runtime 关闭释放，世界身份改变后失效，分配失败回退无缓存冻结。

当前 Scene 仍为单槽：上一帧退休后才能改写资源；蒙皮批次提交后等待完成，native present 后立即
retire 等待帧 fence。本节的复用不引入跨帧在途资源或多帧 pipeline。

启动环境切到 GPU Scene 时保留 Win32/SDL 窗口句柄，先释放 SDL 硬件呈现器并建立软件呈现器，再为同一窗口创建 Vulkan surface；启动页仍由软件画布呈现，进入游戏后由 GPU Scene 接管。窗口 resize、swapchain 重建及错误注入必须按 graphics owner 的完成/退休顺序处理。失败时传播帧错误，不把残缺 Scene 帧解释为成功。实现边界与复现入口见[Scene 工作流](../guides/gpu-scene-fixture.md)和[Windows Native](../guides/windows-native.md)。

正常交互帧由 Game Runtime 以 120 FPS 节流。其 Vulkan swapchain 优先选 immediate，其次 mailbox，均不可用时退回必备的 FIFO；前两者允许在 60 Hz 显示器上继续采样并提交更多帧，immediate 可能出现画面撕裂。固定帧诊断仍用 FIFO。该呈现选择不改变 GPU service、资源退休或固定逻辑步。玩法状态的双 tick 展示插值见[运行时架构](runtime.md)。

## 入图预热与多视图资源所有权

每个 world generation 的首个 native Scene 帧前，主 probe 对同一冻结输入执行一次正常离屏准备和
绘制，提前完成静态地图/prop、当前冻结角色及附件、显示几何、天空/光照管线与主目标的主要首次开销。
随后创建两路持久辅助目标。预热不推进玩法和动作历史；runtime 重置固定步长累计器，加载耗时不补成
一串游戏逻辑步。成功后才记录 `prewarmed_generation` 并显示该地图的第一张 native 画面。
地图代际改变重新预热，关闭 probe 清除标记；失败按正常 Scene 错误链退出。

`SCENE-PREWARM` 记录代际、墙钟、该次主准备的上传字节和 draw 数。上传字节不等于显存占用；
辅助目标和驱动分配不包含在该字节计数中。这里是按地图及当前冻结来源预热，未出现的敌人类型、
武器预览、后续开启的展示和新动态容量仍可能首次加载。应用持有 device-local 资源至失效/退休，
物理显存驻留仍由驱动和操作系统管理，不把全部资产目录强行常驻。

首图 session 通过 `frontier_actor_prewarm` 与 `frontier_actor_warm_activate` 两个只读 layer 提示，
分别授权 ASSAULT 入图准备和 COUNTERATTACK 资源移交。主 probe 在既有入图离屏帧退休后，借用
实际初始枪手的冻结 pose，按原有 load/pack/prepare 路径创建最多六份独立备用资源：普通 AK 三份、
普通 SMG 两份、精英 AK 一份。scratch draw 只用于准备，不加入主 batch，不生成未来演员或推进玩法。
备用资源解除 frame pin 后由 probe 独占；真实枪手进入空 pose ordinal 槽时，只有 world generation、
character/body/bone、装备及 socket、衣裤、隐藏材料、颜色、武器、bind normal 与显示/过滤策略全部
相同才移动唯一所有权。真实 pose 的 palette、世界变换、附件变换和 draw 仍正常重新准备。
角色身份不参与外观复用；GUNNER/GUNNER_ELITE 门禁阻止玩家或其他职业消费资源。
地图代际替换及 probe 关闭销毁所有未移交资源；已移交资源归原 actor 槽的正常清理链。
缺少 donor、外观不匹配或可选预热失败沿用正常冷加载，预热失败本身不新增启动失败条件。
这将后续首用成本和额外 CPU/GPU 常驻容量移到入图阶段；不改变既有同步提交，不扩展跨帧 pipeline。
量化与签收口径见[性能诊断](../guides/rendering-performance.md#首图普通行动的阶段采样)。

交互式 GPU 启动为首帧 probe 安装临时事件观察者，由 Scene owner 报告预热及首个 native 帧的
实际耗时；运行时将结果加入独立 GPU 启动事件表，预热后刷新待呈现状态，成功呈现后解除观察者。
启动总计覆盖后端选择至首帧成功，计量边界见[启动界面合同](../reference/boot-interface.md)。

同一帧先完成主 owner 的上传、蒙皮和场景准备，再刷新到期的 WORLD 子镜头，最后提交主视图。
子镜头借用主资源缓存、已经蒙皮的角色 draw、匹配完整冻结值的敌人/程序角色几何，以及常驻显示
packet；只改镜头参数，保留各镜头独立的可见性、阴影和目标。仅子镜头需要的来源或不满足复用条件
的动态内容仍由子 owner 准备。主 registry 和 actor 的 pin 保持到全部同步子提交及主帧退休。

graphics resource 保留唯一创建 owner，多个同 device 消费者登记双向 reader 引用；各 graphics owner
使用相同定义的 descriptor layout，借用原 camera-neutral descriptor set。资源更新、纹理修改和释放
要求 owner 及所有 reader 退休；consumer 关闭解除引用，source 释放清除 consumer 的绑定。
此共享针对同一来源的多个镜头，不自动合并不同 actor 实例的 bind/纹理缓冲。

## 通讯镜头、单位镜头与设备预览

`rf_gpu_scene_layers_input.aux_view` 提交通讯/设备视图，`unit_view` 提交 RTS 单位视图，两路可同时显示。
每路分别保存稳定来源 ID、generation、镜头变换、视频矩形、内部尺寸和刷新频率；UI 布局只改变最终合成矩形。
`render/rf_gpu_scene_aux.inc` 按槽调度，Scene probe 的 `aux[2]` 各自持有持久子 owner、状态、刷新时刻与重试次数。
默认刷新为 12 Hz，最多 15 Hz；两槽按绝对周期错开半个周期，来源改变立即请求新帧。慢帧后回到各自相位，不累计补画。子视图清除两路嵌套请求，不递归生成辅助视图。
通讯直接按 240×360 的 2:3 竖画面渲染，合成矩形保持同一比例，禁止把横画面非等比拉伸进竖框；
设备预览默认从 320×180 开始，再按预览矩形调整内部高度。实际性能签收依实机采样，不把这些值视为性能保证。
槽 0 在设备打开时优先显示设备预览，否则显示剧情；槽 1 独立显示 RTS 单位，不能抢占剧情画面。
单位镜头复用剧情取景参数、
240×360 竖画面及低频调度，来源身份切换后等新帧再标记 live，具体见 [RTS 核心指挥](rts-command.md)。

WORLD 模式使用本帧同一份 world、actor、敌人、设备和 effects 冻结值，复用上述主 owner 几何，
以实体镜头绘制完整小画面；不二次推进动画历史，不缓存背景图像。天空、动态光照、制造动画、角色和特效继续更新。
主玩家手电保留原玩家镜头的位置和方向。`rf_gpu_scene_enemy_aux_camera` 与 `rf_gpu_scene_enemy_unit_camera`
在来源冻结前保留任一路镜头附近的敌人；普通 AI 来源本身不按主镜头裁掉，FPS 玩家额外身体只供辅助镜头消费。实体外壳按
同一镜头轴向生成，在主场景可见，对其自身镜头排除；窗口收起时外壳仍可保留。

WEAPON 模式复用制造展示的真实武器模型、物理适配和材质，按完整资产 bounds 居中取景，旋转只改变
展示变换，不创建成品或修改制造任务。初始采用侧面略偏三分之四的视角，按变换后的八个 bounds 角点
适配横纵视域和近裁剪；独立均匀补光与 `rf_gpu_graphics_scene_background` 的暗蓝灰线性 HDR 清屏色
只属于模型展示。WORLD 通讯镜头仍绘制真实现场背景。武器展示由子 owner 缓存，WORLD 按上述共享所有权缓存；隐藏/恢复不重复创建
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

可选 slow profile 在主帧原有辅助调用前后观察两槽 `status.frames`，只将本次成功刷新的 child
CPU 时间归给主帧；child GPU 还要求已退休 query 的 frame ID 与冻结 Scene 一致且正有限。
缓存画面不继承旧 timing。观察不改变刷新调度、共享 reader、同步退休或正常显示，关闭时不新增
读钟、query 读取或 slot 遍历；输出口径及可选 COUNTER 稳态筛选见[性能诊断](../guides/rendering-performance.md)。

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
