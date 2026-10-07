# GPU 渲染架构

RTS 楼层 V1 在 lighting upload 尾部增加建筑 X/Z bounds 与世界层顶 Y/amount；普通 Scene
fragment 按这份只读主镜头状态执行有序抖动 discard，天空和屏幕 HUD 跳过，地下层也保留完整 UI。shadow shader 不剖切，静态实例及
深度仍使用既有世界资源。AUX 的 world_only lighting 不复制主视图剖切参数，因此单位与通讯镜头
保留完整空间。细则见 [RTS 楼层](rts-command.md#建筑与楼层-v1)，生成 shader 同步维护 SPIR-V。

静态 ramp mesh 与 CPU 绘制共同消费 `thickness`、`steps`：悬空楼梯只生成有限厚度的踏步或斜板，
底面随坡面移动，不能把上层楼梯侧面延伸到地面。未设置厚度的旧地图保留原有实心坡道。

> 状态：当前
> 所有者：Rasterfall Core Host、Scene owner、Vulkan graphics
> 最近核对：2026-10-06

Rasterfall 的渲染入口为 CPU 软件渲染和独立 GPU Scene。GPU Compute Raster、mixed executor 及 Draw/Raster bridge 已退役。旧实现和诊断合同见[退役归档](../archive/gpu-compute-retirement/README.md)，不能作为当前设计依据。

GPU 实时阴影、动态灯与 PBR/HDR 管线见[GPU 光照架构](gpu-lighting.md)，CPU 保留静态烘焙且不承担高级功能。两后端内容与游戏内控制终端的配置边界由[渲染 baseline 清点](../reference/rendering-baseline.md)定义；后端的绘制原语和资源优化可以不同。

静态建筑遮挡由 Vulkan 后端查询设备能力，在支持时启用 KHR Ray Query，并在 graphics owner
保留 BLAS/TLAS；其他设备使用 GPU 软件 BVH。实例/设备版本协商、资源与 shader 变体的稳定边界
见[建筑硬件查询](gpu-lighting.md#可选硬件-ray-query)，不改变 Scene 光栅呈现或 Game 状态所有权。

顶部可见性列缓存与 DDGI 探针由各 graphics owner 独占；WORLD AUX 可从已初始化的主场复制，
借用主场静态建筑查询结构，后续照明更新仍独立。地图代际变化时重建，resize 保留。
灯具与自然光分别持有探针场，自然光布局依据建筑表面。天空环境 compute 先生成不含太阳盘的
方向缓存；两场使用同一建筑结构，在灯表后、阴影前更新，随后由片元各读取一次并合成漫反射。
它不写入 Game 或旧 CPU 静态光场；布局、更新预算与已知限制见[DDGI 原型](gpu-lighting.md#ddgi-漫反射原型)。

默认 fast 间接照明在两场探针之后增加接收空间缓存 compute，binding 16 由各 graphics owner
持有；geometry/field generation 更新时初始化几何关系，正常帧只轮转刷新缓存值，resize 保留。
compute 读写与 fragment 读取之间显式同步；独立 `receiver` timestamp 不混入 `gi`。
reference 保留为显式对照模式，数据合同和覆盖限制见[接收空间缓存](gpu-lighting.md#可选接收空间缓存)。

## 帧所有权

`rasterfall/src/rf_core_host.c` 管理窗口、输入、固定步长循环的宿主边界，以及 CPU 或 Scene 模式的初始化、呈现和关闭。`rasterfall/src/rf_game_runtime.c` 组织 session、只读 Scene 来源冻结和帧提交。玩法真值由 Game/session 持有；渲染和 HUD 不写入玩法状态。

CPU 模式把 `toy_raster_cmd` 交给软件 renderer，在 CPU surface 上完成分层画面。该命令是 CPU 渲染合同，不属于已退役的 GPU Raster stream。

GPU 模式使用 `--renderer gpu-scene`（与 `--gpu-scene-play` 等价）。独立 Scene owner 冻结 world、角色、地图、天空、透明层、特效、viewmodel 和 HUD 来源，准备 graphics 资源并直接向 Vulkan Scene graphics 提交。`gpu/src/rf_gpu_vulkan_graphics.inc` 持有 Scene mesh/texture、skinning、render pass 与 swapchain present；`gpu/src/rf_gpu_vulkan_backend.c` 持有 Vulkan device、队列和通用 GPU service。Scene 帧不经过 CPU RasterCmd lowering、GPU Compute Raster、mixed executor 或桥接拷贝。

## Scene 资源与同步

不透明 WORLD 在完整着色前增加深度预通道，使用同一顶点变换、索引范围和背面剔除状态。
`graphics_depth.frag` 与颜色着色器共用 `scene_cutaway.glsl` 的楼层溶解覆盖，因此不会留下
不可见屋顶的深度。预通道位于 SKY 后、WORLD 颜色前；透明、特效、viewmodel 和 HUD 保留原顺序，
viewmodel 仍独立清深度。颜色通道保持 reversed-Z 的大于等于比较，使共面表面的后绘制颜色继续获胜。
公共批次若在 WORLD 中插入不测深度的屏幕 draw，整批保持原提交顺序并跳过预通道。
预通道只写已有深度附件，不增加图像、读回或玩法状态；draw 和实例上传计数包含额外提交，
WORLD 时间也包含预通道成本。`RF_GPU_DEPTH_PREPASS=0` 在初始化时关闭以便同包对照，默认 `1`。
回归逐像素比较遮挡、共面、剖切、混合相机、透明和 viewmodel 的颜色及深度。

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
阴影图各自的光空间独立剔除；镜头外的模型仍可投射镜头内阴影。GPU 蒙皮资源使用当前 palette
的保守边界，不能使用 bind bounds。没有距离隐藏、小物件删除或模型降面。静态纹理过滤设置在每帧准备开始时读取一次。
资源包围盒保存真实局部 min/max，中心和半径按顶点相同的旋转、缩放、竖直 pivot 变换并保留
2 RFU 裕量，避免将偏心模型强行扩展至局部原点另一侧。动态上传成功后原子替换边界；主视图和
各阴影图共享该保守边界，但分别判定可见性。屏幕空间来源继续跳过此资源级剔除。

蒙皮资源创建或 bind 更新时，按位置影响骨骼保留局部 AABB；palette 更新只变换这些骨骼边界，
两骨混合的非负权重保证顶点落在包围全部变换边界的总 AABB 内。身体、衣物及 chunk 各有自己的边界；
刚性附件沿原资源边界路径。FP32 求值和整数舍入保留数值裕量，无效、非有限或不可表示的
边界退回完整绘制。`RF_GPU_DYNAMIC_CULL=0` 关闭蒙皮边界剔除以便同包对照。
每个主/AUX consumer 在光照矩阵更新后、蒙皮 dispatch 前生成自己的 camera/shadow 位图；
资源变换仅计算一次，各阴影平面仅准备一次。深度、颜色和阴影录制共用该位图，不共用镜头的
可见性结论，不改变透明顺序。CPU 姿态提取和 palette 主机复制仍在此判定之前。

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

动态三角形路径以敌人和程序角色的完整冻结值、world generation、顶点格式及 CPU 光照模式决定是否复用几何和 draw
段；只有输入完全相同且无需 CPU 顶点光照时跳过提取和上传，相机参数仍逐帧更新。动画采样和表现
历史照常推进，主视图与阴影仍各自剔除。GPU 蒙皮仅在 bind 未变、palette 和顶点数完全一致且上次
提交成功时跳过重复上传与 dispatch；取消的更新不能成为有效缓存。可映射的 bind/palette 缓冲保持
映射至资源销毁，非 coherent 内存仍显式 flush。没有跨实例共享姿态或放宽角色包围盒规则。
角色资源 generation 决定静态材质支持、纹理使用集合和衣服/身体骨架兼容校验的失效；
这些结果成功校验后复用，姿态、变换、索引范围和最终提交批次仍按当前帧验证。
同一角色衣物复制身体已打包的 palette，避免重复逐骨转换；静态显示仍按完整内容键缓存，
文字、样式或位置改变必须重新生成，动态光束、信标与 HUD 继续使用逐帧来源。

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

当前 Scene 仍为单槽：上一帧退休后才能改写资源。蒙皮批次 end 只封存已复制的输入；每个
主/AUX 绘制消费者只在同一 command buffer 中执行本视图或阴影需要的蒙皮和后续绘制，保留 transfer→compute、
compute→vertex/transfer 屏障。只有 queue submit 成功才认可姿态缓存；封存而未消费的资源
不能再更新或释放；本镜头不消费的更新留给后续 AUX/主镜头。成功 submit 只认可实际录制的
资源，全部视图退休后取消无人消费的剩余更新；cancel 同时使姿态缓存和当前边界无效。
staging 中尚未提交的 bind 更新保留至下次消费，即使生产者下一次只发送 palette；
成功提交后才清除该待上传状态，避免取消后重新出现的模型使用旧绑定。
诊断顶点读回仍可消费整批。冷创建仍同步；`RF_GPU_SKIN_FUSED=0` 恢复独立
蒙皮提交及等待，以便同包对照。native present 后立即 retire 等待帧 fence；没有多帧 pipeline。
合并蒙皮纳入总 GPU 时间并单列 `detail_ms[8]`，不混到 SKY/WORLD 阶段。
准备计时包含主视图建筑/探针初始化和普通光照准备，单独记录 `lighting_prepare_us`；
不能将其遗漏到未归因墙钟。上传、蒙皮批次、录制、acquire、queue submit、present、retire
按同一冻结帧观察，嵌套区间和 GPU 时间不相加；整帧减 GPU 不是可直接消除的 CPU 工作。

启动环境切到 GPU Scene 时保留 Win32/SDL 窗口句柄，先释放 SDL 硬件呈现器并建立软件呈现器，再为同一窗口创建 Vulkan surface；启动页仍由软件画布呈现，进入游戏后由 GPU Scene 接管。窗口 resize、swapchain 重建及错误注入必须按 graphics owner 的完成/退休顺序处理。失败时传播帧错误，不把残缺 Scene 帧解释为成功。实现边界与复现入口见[Scene 工作流](../guides/gpu-scene-fixture.md)和[Windows Native](../guides/windows-native.md)。

正常交互帧由 Game Runtime 以 120 FPS 节流。其 Vulkan swapchain 优先选 immediate，其次 mailbox，均不可用时退回必备的 FIFO；前两者允许在 60 Hz 显示器上继续采样并提交更多帧，immediate 可能出现画面撕裂。固定帧诊断仍用 FIFO。该呈现选择不改变 GPU service、资源退休或固定逻辑步。玩法状态的双 tick 展示插值见[运行时架构](runtime.md)。

## 统一角色加速

公共边界由 `rf_gpu_character.h` 定义：固定顶点、骨骼权重、材质段和独立值类型 palette。
Block 与六种普通感染体分别由自己的 source adapter 提供 bind/sample；
`render/rf_gpu_character_gpu.inc` 统一管理资源模板、实例缓存、上传与镜头 draw。
Humanoid 保留贴图、衣物和分块 adapter，经 `rf_gpu_character_upload.h` 使用同一资源更新／替换事务。
它的实例注册表通过 owner-local `rasterfall_resource_pool` 共用不可变模型和纹理 backing；
同路径只加载一次，注册表各自保留句柄代际和帧 pin。一个实例失效不影响其他实例及退休中的帧。
资源池最多保留 registry 容量的模型，满时仅淘汰无人引用项；owner 在全部实例完成退休后释放池。
新增固定几何角色应实现生产者适配，不复制 GPU 生命周期或另建多视图蒙皮调度。

四层所有权如下：模型目录／resource 持有不可变网格与骨架；source 持有动作语义和采样历史；
公共实例槽保存自己的冻结输入及最终 palette；GPU owner 保存可更新的输出、当前姿态边界与 reader。
Block 外观键包含颜色、职业和武器；感染体模板键是 recipe，世界代际也参与失效。
同 owner 中相同键共享引用计数的 CPU bind、索引与材质段，模板晚于最后一个实例释放。
资源是加载后不可变的；当前没有文件热重载，磁盘资产更新需重启。owner 关闭／世界代际变化
失效 GPU 模板，不代表重新读取已经加载的模型文件。

普通感染体默认不再逐帧展开三角形或 CPU 蒙皮：步态仍由原 sampler 冻结，GPU 消费最终骨骼矩阵。
压缩、死亡翻滚、反馈颜色及淡出保留；需要 form-light 的材质使用公共 shading mode 4，
顶点阶段从同三角形的蒙皮法线均值恢复原角色颜色乘数，片元阶段继续使用几何法线。
计算由 CPU 移到 GPU，不以直接取消原乘数的方式改变身体明暗。
CPU 分步整数取整与合并 FP32 蒙皮不承诺逐像素一致。显式 CPU 顶点照明、旧模型、带额外几何的
诊断来源以及特感刚性 rig 暂保留旧生产者。`RF_GPU_INFECTED_RETAINED=0` 可作同包对照。

主／AUX 只有在世界、帧号和完整冻结输入都匹配时借用同一 GPU 输出，视图和阴影各自裁剪。
上传替换先成功创建候选资源再退休旧资源，失败不发布候选；取消批次仍由后端失效，不能以
CPU palette 缓存命中代替 GPU 完成。`SCENE-CHARACTER` 报告模板构建／共享、姿态求值／复用、
视图借用、bind 上传和 palette 请求，以及采样、打包、上传耗时；palette 请求不等于实际 GPU 上传。
IK 包含在采样耗时中，GPU 蒙皮继续使用通用 `detail_ms[8]`，不将其重复计入 CPU。

当前共享模板不等于跨实例 GPU bind buffer 去重：GPU 输出、bind 和 palette 仍逐实例分配，
Humanoid 的贴图打包和分块仍保留自身 adapter。实例槽有界，感染体按验证后的 source slot
索引；程序角色按冻结序号索引并比较完整输入，序号不是持久身份。并行求值、动画降频、特感迁移、
跨 adapter 的资源目录整合及跨实例显存去重仍需后续收敛和实测，不由这一入口隐含提供。

### RFCHAR 加速与纹理复用边界

RFCHAR（含 RF-C01）通过 `rf_gpu_scene_native.c` 的角色 adapter 接入公共
`rf_gpu_character_upload()` 更新／替换事务；保留材质、衣物和几何分块适配，不直接使用
Block／感染体的模板生产者。CPU 求值动画、IK 和最终 palette，GPU 执行顶点蒙皮。
稳定资源复用 bind，后端仅在上次提交成功且 palette 等输入一致时复用蒙皮结果。

当前复用范围如下：

| 范围 | 已实现行为 | 边界 |
| --- | --- | --- |
| CPU 模型及纹理 backing | 同 owner 的实例注册表经 `rasterfall_resource_pool` 按相同路径共享不可变加载数据 | 不等于跨实例 GPU buffer 去重 |
| 单实例稳定帧 | `scene_prepare_textures()` 按资源句柄、generation 和绑定状态复用已上传纹理 | 资源替换或失效后重新准备 |
| 同一 mesh 的几何 chunks | 首 chunk 上传 texture set，其余 chunk 引用计数共享 | 身体、衣物等独立 mesh 不因此自动合并纹理 |
| 主／AUX 视图 | `shared_parent` 消费者借用主视图角色资源并更新相机参数 | 依赖既有共享帧条件，独立 owner 不自动共享 |

纹理 mip 由 CPU 生成后上传 device-local storage buffer；fragment shader 手动完成
clamp、双线性／三线性过滤及 sRGB 解码，尚未使用 Vulkan image/sampler 硬件纹理采样路径。
具体材质容量与 OPAQUE 合同见本页角色材质说明。不同角色实例当前仍各自生成、上传并持有
GPU texture set，GPU bind／蒙皮输出也仍逐实例分配。

后续优化及验收门槛归[延期事项](../plans/README.md#rfchar-加速后续)，不由当前路径隐含提供。
以上边界于 2026-10-07 对照源码核查；此次核查未运行原生 GPU 或画面对照，不作为新增性能签收。

## 程序角色常驻几何

标准 Block carrier 默认由 `render/rf_gpu_scene_block_source.inc` 将固定身体、武器、职业装备和
枪口闪光拆成 bind 几何与逐帧 palette。身体继续由原 `block_character_sample` 求动作及 IK；
武器消费 finalized 挂点和原 adapter，装备消费 CPU 绘制共用的 box 枚举与骨骼归属。
GPU 不推进动画、改写 Game 或反求玩法动作。平面靶和显式 CPU 顶点照明诊断保留三角形提取路径。

`render/rf_gpu_character_gpu.inc` 由 Scene probe 持有有界槽及一次构建 workspace；外观颜色、
职业、武器或 world generation 变化重建 bind，普通移动和动画只更新 palette。
顶点、三角形和不透明颜色段保留来源顺序；枪口闪光常驻但只在冻结状态要求时提交。
完整世界旋转进入 palette，倒地/死亡/复活不受 yaw-only draw 限制；局部原点随角色移动，
避免大世界坐标进入 FP32 骨骼求值。CPU 分步取整与 GPU 合并求值可产生少量 RFU 的舍入差，
由几何对照及实机画面验证约束，不声明逐像素一致。

资源沿用现有 GPU skin batch、当前姿态边界和主/AUX 独立可见性。主 owner 在敌人/程序来源
准备后封存批次；AUX 只有在同帧、同世界和完整冻结输入一致时借用父资源，自己更新镜头参数。
不匹配来源由子 owner 独立准备。更新、释放及取消遵守全部 reader 退休合同；取消的 GPU 姿态
仍由通用 skin cache 失效，CPU 保存的 palette 不代表 GPU 已执行。每来源最多沿用 4096 三角形
上限，probe 关闭释放资源和 workspace。`RF_GPU_BLOCK_RETAINED=0` 选择原动态三角形路径作同包对照。

常驻 bind 和 GPU 蒙皮输出会增加显存容量：64 个槽全部达到上限时，bind、输出顶点和索引的
理论 payload 为 `64 * 4096 * 3 * (88 + 56 + 4)`，约 111 MiB，另计 palette、可选 staging、
描述符和对齐；这不是实际驻留测量。各槽按实际来源建立，CPU 共用一个有界构建 workspace，
按外观共享 bind 模板，每实例保留 palette。普通感染体槽另按实际模型顶点数计费，不能套用
64 个 Block 槽的总数。原动态来源槽可能同时保留历史容量，独立子 owner 也单独计费。

## 入图预热与多视图资源所有权

Vulkan backend 持有一个 device 级 `VkPipelineCache`，同设备的主/AUX graphics 与 compute
管线创建共用它。首次 graphics 创建按 vendor/device、driver version、pipeline cache UUID、
文件长度与校验和检查持久缓存；失配、损坏或加载失败回退空缓存。正常后端关闭时有界读取
驱动数据，写入进程独立临时文件并原子替换；写失败只影响下次启动复用。默认位置是运行目录
`build/rf-gpu-pipelines-<vendor>-<device>.bin`，不进入源码或 package 资产。新设备、驱动变化
和首次运行仍有编译成本。`RF_GPU_PIPELINE_CACHE=0` 关闭缓存；
`RF_GPU_PIPELINE_CACHE_PATH` 指定文件，`-` 仅使用内存缓存。缓存由 backend 生命周期管理，
不替代 GPU 屏障、提交或退休。接口合同见 [Vulkan pipeline cache](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreatePipelineCache.html)。

每个 world generation 的首个 native Scene 帧前，主 probe 对同一冻结输入执行一次正常离屏准备和
绘制，提前完成静态地图/prop、当前冻结角色及附件、显示几何、天空/光照管线与主目标的主要首次开销。
随后创建两路持久辅助目标。预热不推进玩法和动作历史；runtime 重置固定步长累计器，加载耗时不补成
一串游戏逻辑步。成功后才记录 `prewarmed_generation` 并显示该地图的第一张 native 画面。
地图代际改变重新预热，关闭 probe 清除标记；失败按正常 Scene 错误链退出。

`SCENE-PREWARM` 记录代际、墙钟、该次主准备的上传字节和 draw 数，另分开主准备、光照准备、
主 GPU 绘制、GI/receiver GPU 区间、两路 AUX 创建与备用角色池准备。嵌套区间不能相加，
这些计时不等于完整启动时间。上传字节不等于显存占用；
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

同一帧先完成主 owner 的上传封存和场景准备，再刷新到期的 WORLD 子镜头，最后提交主视图。
子镜头借用主资源缓存、封存或已经蒙皮的角色 draw、匹配完整冻结值的敌人/程序角色几何，以及常驻显示
packet；只改镜头参数，保留各镜头独立的可见性、阴影和目标。仅子镜头需要的来源或不满足复用条件
的动态内容仍由子 owner 准备。主 registry 和 actor 的 pin 保持到全部同步子提交及主帧退休。
借用只在生产者 batch 封存后允许。第一个到期 AUX 可在自己的 command buffer 中完成共享蒙皮；
成功提交后从生产者 pending 队列移除，后续 AUX/main 沿同一 GPU queue 消费结果，不再重复 dispatch。
辅助绘制仍同步退休；失败/关闭先排空 GPU，再解除 reader 和资源引用。

WORLD AUX 首次准备优先调用 `rf_gpu_graphics_clone_lighting`，从同设备、同配置且已退休的
主 graphics 在 GPU 上复制已初始化的顶部缓存、两场探针、静态布局、接收缓存和固定灯遮挡掩码到独立缓冲，
保留更新游标及初始化状态；compute 写入到 transfer 读取、transfer 写入到 shader 消费均有
屏障。只有不可变建筑 AS/BVH 借用主 owner，并登记 reader。源建筑替换要求所有 reader 退休，
替换/关闭解除引用、恢复子 owner 空查询结构并使旧 GI 失效；子关闭也解除引用。照明缓冲、
镜头、天空环境和阴影目标不共享，灯具更新继续各自执行。配置不匹配或主场未准备好回退
独立初始化；`RF_GPU_AUX_LIGHTING_CLONE=0` 提供对照。WEAPON 预览解除建筑借用并关闭
间接光，重新进入 WORLD 时重新复制。该路径减少首次初始化，未引入多帧并行。

graphics resource 保留唯一创建 owner，多个同 device 消费者登记双向 reader 引用；各 graphics owner
使用相同定义的 descriptor layout，借用原 camera-neutral descriptor set。资源更新、纹理修改和释放
要求 owner 及所有 reader 退休；consumer 关闭解除引用，source 释放清除 consumer 的绑定。
此共享针对同一来源的多个镜头，不自动合并不同 actor 实例的 bind/纹理缓冲。

## 通讯镜头、单位镜头与设备预览

指挥桌另有 `RF_GPU_AUX_FROZEN`：借用已退休、独立地图 owner 的离屏颜色目标，只做 GPU
合成，不按 WORLD/WEAPON 的刷新频率更新。目标地图首帧由同一 Scene 冻结、几何与光照链
生成；没有截图读回或 CPU framebuffer 往返。来源 owner 在解绑主视图并退休后才释放，
UI 与输入共用预览矩形并在 overlay 留出透明内容区。点击预览框后才启动后台首帧任务，
选图不自动触发 GPU 资源准备。主 GPU 提交退休后，工作线程独占场景渲染和设备队列，
主线程在启动工作线程前，通过 `rf_gpu_graphics_scene_snapshot` 一次读回已退休主颜色目标，
作为加载期间的静态大厅背景。该接口不重新提交 Scene 绘制，临时 staging 在读回后释放；
正常主帧和目标地图预览仍不读回。主线程通过独立 GDI 子窗口绘制背景与加载 UI，
不使用场景 renderer 或 GPU 呈现；join 后释放 CPU 背景，才能
移交/释放预览资源并恢复正常主 Scene。预加载会话和移交规则归
[运行时生命周期](runtime.md#指挥桌预加载与出生点预览)。

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
