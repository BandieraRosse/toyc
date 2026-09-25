# GPU 渲染架构

> 状态：当前
> 所有者：Rasterfall GPU renderer、Core Host 与 Windows presenter
> 最近核对：2026-09-23

本文只定义 GPU 渲染的稳定所有权、数据流和失败边界。验收命令见
[GPU 验收与诊断](../guides/gpu-validation.md)，设备档位和性能门槛见
[GPU 性能标准](../reference/gpu-performance-standards.md)，尚未完成的工作只见
[当前活动计划](../plans/README.md)。阶段调查、单次设备现场和已撤销实验位于
[GPU 2026-09-22 归档](../archive/gpu-2026-09-22/README.md)。

## 所有权

| 职责 | 所有者 |
| --- | --- |
| world、角色、特效 producer 与层顺序 | `rasterfall/src/rasterfall_render.c`、`rasterfall/include/rasterfall_render_frontend.h` |
| 帧冻结、资源 pin、整帧 preflight 与执行编排 | `rasterfall/src/rf_core_host.c` |
| GPU service、mixed executor 与资源 cache | `gpu/src/rf_gpu_vulkan_backend.c`、`gpu/src/rf_gpu_mixed_executor.c`、`gpu/src/rf_gpu_resource_cache.c` |
| normal runtime 与 presenter 生命周期 | `rasterfall/src/rf_game_runtime.c`、`windows/src/window_sdl.c` |
| CPU reference 与 Raster ABI | `rasterfall/src/render/rasterfall_draw_reference.inc`、`rasterfall/include/rf_gpu_raster_abi.h` |

`rasterfall.c` 只负责进程、输入、固定步长主循环和顶层编排。renderer 读取玩法或展示投影，
不修改 `toy_game` 的权威结果。地图文本、Runtime Map、玩法碰撞和可见几何保持分层。

## 独立 Scene 开发入口

`--gpu-scene-play` 是显式实验性单人入口，自动选择 required native GPU 和独立 Scene。
不要求固定镜头或固定 tick，默认落地前哨站，继续使用正常输入、session 和 world 切换。
拒绝网络模式与 legacy map；性能与完整内容签收仍由活动计划跟踪。Scene 提取、准备或提交失败
进入 runtime 统一关闭路径，清理音频、Scene 资源、Game 和 Core 后非零退出。

`--gpu-scene-independent-preview` 在 runtime 层选择独立来源，不调用旧整帧
`rf_game_render_profiled`。`rf_core_begin_scene_frame` 只获取窗口 surface/extent，
不运行 CPU renderer begin/clear、不启动 mixed recording、不 pin 旧 registry。
camera 使用共享的只读展示求值；地图、正式模块化队员、旗帜、投射物和交互物直接冻结，
敌人和其余角色从 `rf_gpu_scene_enemy_collect_independent`、`rf_gpu_scene_actor_collect_independent`
冻结身体姿态、死亡、附属表现、程序/倒地角色、补充模块化角色及网络展示输入，
经 Scene registry/cache、pose/skinning 和独立 native owner 提交并退休。
逐帧检查旧 RasterCmd 和 mixed draw 数为零。旧 WORLD preview 保留 producer 驱动的诊断方式。

独立入口通过 `render/rf_gpu_scene_layers.inc` 在目标写入前提取天空、地图透明、特效、
第一人称和 HUD 布局；敌人冻结值携带死亡 alpha。有序批次按 SKY、WORLD、TRANSPARENT、
EFFECTS、VIEWMODEL、OVERLAY 稳定分层，层内不透明在前，透明保持来源顺序。
透明 source-over、depth-test/no-depth-write；SKY/OVERLAY 不测试或写入深度。
首次 VIEWMODEL draw 在同一 render pass 内清空深度附件，保留已经完成的世界颜色，
以独立的逻辑深度域直接合成武器和透明枪口；未覆盖的像素保留世界颜色，无 framebuffer copy。
POST 沿用正常帧 identity 策略。整批验证包括层序，失败不提交部分目标。
等深度屏幕矩形（粒子、文字和 canvas 面板）在上传前裁剪到视口，避免屏幕外投影坐标超过 GPU
几何范围而拒绝整帧；裁剪保持可见区域的颜色、深度和透明度。
世界特效和透明地图三角形以局部坐标上传，draw 保留世界平移；超过局部跨度的无纹理面先细分。
分层几何按顶点容量拆成资源块，批次不能跨块合并，透明来源顺序保持不变。

当前入口仍是内容不完整的开发预览；细节与可玩门槛由活动计划拥有。
动态来源数量逐帧输出，不能以 `dynamic_sources_pending=0` 代替完整帧验收。它仍共享 Scene 审计编排和 GPU 初始化设施，
几何与光照 helper 尚位于既有 renderer 模块；独立正常帧协调器及多 slot 流水尚未实现。
这种代码复用不能演变为调用旧 draw producer 来取得新帧输入。开发顺序只由活动计划拥有。

显式独立预览 capture 用同一冻结批次先提交离屏 Scene color/depth 读回，再 native present；
只将指定帧标为 `readback=1`，未请求捕获的帧保持零读回。该图用于内容检查，不证明 swapchain
自身的呈现颜色，也不参与产品性能结论；捕获失败仍整帧失败，不回放 mixed。
分层几何与两种纹理 backing 由 Scene owner 持有到同步退休；后续帧按容量更新，纹理内容变化时失效对应资源。

## 混合帧数据流

```text
game/session presentation snapshot
    -> renderer producers
    -> retained frame (Draw + RasterCmd)
    -> Core freeze / pin / whole-frame preflight
    -> mixed executor on shared color/depth targets
    -> native presenter
```

producer 按 WORLD、EFFECTS、VIEWMODEL、POST、OVERLAY 的固定层序生成 retained frame。普通 opaque
static RMESH、持久 ground/map/boundary geometry 和角色 body 可进入 hardware Draw；动态、透明、特效、
viewmodel、overlay 及尚未满足数值合同的内容保留为 RasterCmd。

Core 必须在任何 target 写入前冻结执行计划并完成整帧 preflight。required 模式下，unsupported、编码失败、
资源 generation 不匹配或 presenter 失败都会使整帧失败；禁止先写入部分 GPU target 再回放 CPU 整帧。
Draw 与 RasterCmd 共用 color/depth target，命令顺序、CLEAR/LOAD、透明、viewmodel 和 overlay 语义由统一
recording 保持。

producer 身份只用于诊断，不自动形成 target 可见性边界。相邻 WORLD Draw spans 之间没有 Raster span 时，
executor 可按原顺序合并为一个 graphics batch；Raster span 是硬边界。任何进一步合并或迁移必须同时
保持画面合同并减少实际 Draw/Raster run、bridge 或 whole-loop 成本，不能只以 RasterCmd 数量下降签收。

normal AI world 对模块化角色先按 actor 顺序求值 pose/IK 并冻结全部可见 body Draw，再按相同顺序提交
opaque gear/weapon RasterCmd。附件继续读取对应 actor 的 finalized pose、placement 和 scene-light override；
该编排不得跨越 transparent、effects、viewmodel 或 overlay 层。

## Raster 与诊断合同

indexed Draw 的正面必须与正式 CPU renderer 一致：屏幕 Y 向下，CPU 接受 `(c-a) × (b-a) < 0`，
正高度 Vulkan viewport 使用 `VK_FRONT_FACE_CLOCKWISE`。普通与 integer-compatible pipeline 共用该规则；
单面材质剔除背面，双面材质保留两面，opaque 深度测试/写入仍为 GREATER_OR_EQUAL。
独立测试若使用相反的叉积顺序，必须同时反转符号判定，不能把错误 GPU 绕序写成 reference。

Raster binning 按原 command index 将命令写入 tile 列表。binned shader 可用有序索引跳过当前 segment 外
命令，但必须在 `segment.end` 停止；独立 full-scan shader 保留为 differential 对照。帧审计按 producer
记录 RasterCmd/span，并按实际 bridge 记录方向、color/depth traffic、层、相邻 producer 和 target generation。

GPU timestamp 属于完成的旧 frame slot，必须按其 frame ID 回填，不能直接归到当前 CPU frame。
`valid` 要求 requested 与 recorded 相等且 dropped 为零；退出时未回收的尾部样本不进入分位数。
graphics submit/wait 是队列关系证据，不等于某个 producer 的 GPU 时间。

graphics batch 在每次 render pass 内复用连续 draw 的顶点、索引、descriptor 和 pipeline 绑定。
资源或整数索引模式变化时重新绑定，pipeline 按双面、深度与透明策略切换；每项仍独立上传 push constants
并执行原有 draw，preflight 和层序检查不变。缓存不跨 command buffer 或 render pass。

正常游戏画面使用中性 fog。RasterCmd fog 字段、CPU/GPU consumer 和底层 Post Fog 测试合同仍可保留，
但 normal runtime 不把它们接入画面。

## 资源生命周期

- 模型 registry 拥有不可变 CPU bundle 与 stable handle/generation；GPU cache 拥有 device resource。
- Core 在 begin-frame pin 本帧引用；frame slot 完成前不得释放。world 切换后的旧 generation 只有 pin 清零
  后才能回收。
- 双帧 slot 分别持有 extent target、command/fence/query 和动态 Draw backing；不可变 mesh/texture cache
  由 executor/device 统一持有。
- 已完成的 frame slot 复用角色 skinning 的动态 Draw resource、顶点/索引 buffer 和 descriptor；bind/palette
  每帧更新并执行 skinning。输入超过既有容量时，在 slot recycle 后销毁旧 generation 再创建更大资源。
- raw Raster 分段重传必须保留先前录制引用的 backing，直到相关录制完成或销毁。
- resize 只重建 extent 相关 target、slot binding 与 swapchain，不得重复上传稳定 world mesh/texture。
- swapchain 由 backend 唯一拥有；acquire、render fence 与 present completion 分开跟踪。正常热路径禁止
  queue-idle，只有明确的 recreate/teardown 边界可以排空队列。

## 独立 Scene fixture 提交

显式 `--gpu-scene-world-preview` 将真实 normal-scene/wave workload 的冻结 WORLD 提交到
native swapchain。旧 producer 暂时仍负责展示求值与冻结；Core 的
`rf_core_finish_scene_recording` 丢弃未执行的 mixed recording 并释放其 CPU pin，
随后 WORLD owner 使用独立 registry、graphics/cache 和资源 pin 完成 Scene submit/retire。
该路径不执行 mixed、不读回 WORLD color/depth，也不上传 CPU framebuffer。
native 成功提交与退休单独计数，不能因 CPU scene pixel 为零误判无输出。
提交/退休失败先排空 graphics owner 再释放 CPU pin；不接回 mixed。
当前 owner 同步等待每帧退休，动态敌人/程序角色按槽复用资源容量；这不是多帧流水性能方案。
此显式预览仅显示 WORLD，天空、透明、特效、VIEWMODEL、OVERLAY 尚未接入；默认完整呈现仍走 mixed。

`rf_gpu_scene_native.c` 拥有显式 `--gpu-scene-native-fixture` 的冻结输入、资源解析、整帧验证和单个
Scene slot；正常帧仍由 Core mixed executor 编排。fixture 只提交 `opaque_box` 地图几何、RF rifleman
body 和 HEAD 附件。地图沿用正式地图 mesh builder；catalog ID 解析为独立 registry 的 handle/generation。
材质、索引、palette 范围与变换全部检查后才统一 pin；设备准备失败也不会提交 target 写入。

CPU pose 仍由独立 instance 求值，并冻结 finalized pose 的 bind-normal 策略。body palette 上传到既有 compute skinning，rigid HEAD 的 finalized
矩阵通过单骨 palette 消费，资源 `position_scale` 到 RFU 的换算只在资源解析侧执行一次。不可变地图
资源跨帧保留，动态 device backing 在退休后 update 或增长；CPU pose 每帧独立求值，pack/upload backing 在
slot 退休后按容量复用，world 资源失效不回收 CPU backing。

graphics owner 持有独立 command pool/buffer、fence、acquire semaphore 和 WORLD color/depth。
`rf_gpu_graphics_scene_present` 将全部 mesh 放入同一 render pass，直接 blit color 到 backend 唯一
swapchain，完全不接收 Raster stream，也不调用 bridge。render-finished semaphore 按 swapchain image
索引持有，重新 acquire 同一 image 才证明此前 present wait 完成。单 graphics queue 串联上传、compute、draw
和 present；当前 fixture 在每次提交后显式等 fence，再释放 pin，不是多帧流水实现。
Scene 的四个 GPU timestamp 查询随 command buffer 重置，围住 WORLD draw 与 swapchain blit；fence 退休后按
提交时的 frame ID 读取。设备不支持 timestamp 时报告 unsupported；这些值不覆盖此前独立提交的上传和蒙皮。

`scene_retire` 成功才允许 slot 复用；失败保留 pin 到 graphics/backend teardown 排空 GPU 后。resize
只替换 extent target/swapchain，world 失效后的旧 handle 在 fence 完成前保持可解析。诊断 capture 直接
读取 Scene color/depth，不经过 Raster 转换；该显式 readback 与连续 native 提交分别统计。复现和未完成
门禁见 [Scene fixture 指南](../guides/gpu-scene-fixture.md)。

正常帧的 `--frame-audit` 另从 Runtime Map 冻结 V2 world 元数据及独立 V1 地图渲染值帧，
并用后者提取 wall、box、ramp、style 2 platform 四类不透明网格。地图范围、出生区及
authored ground 策略另按值冻结，分区地面与 mixed 共用同一网格生成算法，作为第五类资源。
静态 object 按 authored ID 和 `level->props` 投影顺序冻结；现行持久 boundary wall 几何从
此值帧生成第六类资源。可见 static RMESH 按同一冻结 object 值、V2 实例光照、资产 profile
与既有 Draw 材质规则提交；镜头外实例沿用 mixed 的模型 AABB 可见性检查。资产按模型路径进入同一
Scene registry，跨实例共享 GPU cache，已 pin 的旧代随帧退休。当前仅在正常帧审计离屏提交。
普通 `TOY_MAP_DRAW_MODEL` 盒体从冻结 render 值生成第七类网格；floor 值帧另冻结现行该入口
使用的 V1 诊断光照场，其面片保持旧路径的单四边形划分和每三角形中心采样。
SIGN 的牌柱、牌面及双面 bitmap 字形从同一 render 值生成第八类网格；字形保持旧路径
的横向连续像素 run 合并，牌体与文字均按三角形中心采样 V2 光照。
展示模型 style 1、2、6、9、12 的旧方块人/圆柱人形体共用第九类网格，按三角形中心
采样冻结 V1 光照。style 3–5 的特殊感染体从旧 rig 目录求静态姿态、世界变换及面光照，
生成第十类网格并逐三角形采样冻结 V1 光照；style 7–8、10–11、13–14 的六种导入感染体展示模型复用旧 idle rig 姿态、CPU skinning 和材质，生成第十一类网格并烘焙逐三角形冻结 V1 光照。
生成网格的离屏 Scene draw 使用读取顶点光照的 graphics 管线；静态 RMESH 保留
整数深度兼容管线；仅超出该管线保守屏幕投影范围、但整数顶点变换仍安全的实例使用 Scene 硬件裁剪管线。
两类实例在同一 WORLD color/depth 中提交，沿用相同材质、光照、资源 pin 和缓存；不转回 RasterCmd。
硬件裁剪分支仍检查旋转、缩放及 camera 变换的 int32 运算范围，不能把数值溢出当作可裁剪几何。
片元顶点光照上限为 384，与旧 Raster 的范围一致。
独立 Scene world registry 持有 generation handle，按 world/map、V2 light generation、
冻结的完整绘制值、地面与 object 输入复用网格，
并延迟释放已 pin 的旧代。独立 native fixture 在三件套
结束后调用可复用的 `rf_gpu_scene_world_gpu_prepare`，将 fixture 中非空的六类网格经 GPU cache
预备并提交离屏 Scene WORLD，检查 color/depth 覆盖、
cache hit 和旧代退休。正常帧 `--frame-audit` 在 mixed present 完成后，使用同一 Vulkan context
创建独立 graphics owner 和 GPU cache，消费当帧冻结的 map/prop handle 与 render camera，将非空网格
提交到独立 Scene target 并读回 color/depth。八名非 hired 正式模块化队员各自的冻结 body palette、
被动装备、AK 武器与 V2 actor 光照值也经逐 actor 独立 GPU 资源预备，和地图 draw 共用该 Scene WORLD color/depth。
session 旗帜的 active、位置、颜色、当前选中状态与短标签按帧复制为只读值；旗杆与旗布共用两份常驻 cuboid GPU 资源，
每面活动旗帜提交两项 WORLD opaque 实例。标签沿用旧路径的 bitmap 字形和连续像素 run 合并，
按旗帜来源槽缓存双面 GPU 字形网格；标签变更时仅替换该槽的网格。字形与旗布共用 Scene WORLD 深度。
活动投射物从玩法数组按 slot 顺序冻结 kind、位置、高度、旋转时间、闪烁状态及 V2 光照；
bomb/molotov 各复用一份 RMESH GPU 资源和模型纹理，闪烁时使用材质纯色，实例变换在 draw 中求值。
其 WORLD opaque draw 与旗帜、地图和角色共用 Scene color/depth；普通帧没有投射物时不加载这两份资源。
session 交互物按来源槽位冻结 kind、weapon、位置、效果高亮及 V2 光照，并在 PLAYING、非暂停、非商店状态下可见。Scene 将七类拾取模型按 primitive 使用常驻 GPU 资源绘制；按钮、药瓶、弹药盒使用共享形体，特殊按钮底座按来源槽位及高度缓存。两类交互物与其余 WORLD opaque 共用目标深度；程序形体仍需固定视觉基线审批。
特感、普通感染体与 LEGACY 身体由同帧 producer 冻结 pose/步态、变换、反馈与光照输入，独立预备从这些值生成
Smoker、Charger、Tank 的刚性网格，和地图、角色共用 Scene WORLD color/depth。
几何枚举与 mixed 共用，连续同色三角形合并 draw，保留原始提交顺序；冻结时若采用 V2 顶点光照，
则在复制的 light field 中采样，否则使用冻结的实例光照。Scene owner 在同步提交退休后复用敌人与程序角色
的动态三角形资源槽；槽只代表容量，不代表角色身份，每帧重新提取并写入全部活动顶点和 draw 材质。
顺序索引、白色纹理、buffer 与 descriptor 保留；容量不足时替换资源，暂时不活动的槽保留到 owner 关闭。
更新同步刷新活动索引范围和位置边界；共享、skinned 或尚未退休的资源拒绝更新。顺序三角形资源
创建后保持 host-visible 顶点内存映射，热帧从普通 CPU 工作区连续复制；非 coherent 分配按
nonCoherentAtomSize 对齐并仅刷新活动字节覆盖范围。其他内存首次更新时建立持久映射的 staging
buffer，并继续同步 transfer；资源销毁时解除映射，失败由 owner teardown 回收。
世界切换随 Scene owner 关闭释放资源。该策略仍是单 slot 同步退休，不表示多帧流水已经实现。
owner 跨审计帧复用，registry frame pin 在诊断
提交完成后退休，cache 随 generation collect。该诊断不替换 Core mixed executor 的正常提交；
读回耗时也不在此前记录的 `FRAME-AUDIT whole_loop_ms` 内。
固定 normal capture 请求与该审计同时启用时，诊断额外写出同帧 Scene WORLD PPM，
供与 mixed 最终 BMP 核对世界局部画面。

程序角色、host/guest 玩家与补充模块化角色也进入上述 WORLD submit，来源与身份限制由
[角色表现](character-presentation.md)拥有。双面属性来自冻结的 producer/material，不能在 Scene 默认改为单面。
敌人阴影、舌头和束缚圈保留原几何及深度语义；死亡身体使用冻结旋转、光照与 alpha，渐隐身体进入有序透明层。

`rf_gpu_graphics_scene_capture_at` 为离屏 WORLD render pass 写入两次 GPU timestamp，并在同步 fence
完成后按调用方 frame ID 读取；上传、skinning 和 readback 不计入 draw 时间。旧无计时 capture 保留包装入口。
`SCENE-WORLD-COST` 报告准备墙钟、实际 mesh/texture 上传字节、draw 数及 bridge 次数；该独立 owner
出现 bridge 即失败。`SCENE-EXTRACT` 单列本地 pose 与敌人/程序角色的 CPU 几何提取时间。
这些是离屏诊断成本，不能替代正常帧 whole-loop 或 FPS。

独立 native 路径另输出 `SCENE-FRAME-COST`：world、正式角色、敌人/程序角色、分层准备及提交/退休墙钟，
并记录动态资源复用/创建数。`whole_loop_us` 从本轮主循环开始计至 Scene 退休后输出该记录前，包含逻辑和
来源提取，不包含后续日志与循环尾部工作；capture 帧还包含额外离屏读回，不能用于正常帧性能比较。
这些分段不是完整 prepare 的穷尽拆分，旗帜、拾取物和批次数组准备等仍在总 prepare 中。
`SCENE-SUBMIT-COST` 进一步区分 native record/acquire/submit/present 墙钟与 fence 退休墙钟；
显式 capture 的离屏读回只计入原总段，不混入这两个 native 子段。退休仍是同步的。
敌人三角形提取按单个身体缓存已蒙皮顶点，光照按当前冻结光场和精确世界坐标缓存；碰撞键重新计算，
光照缓存不跨身体或帧，保留材质、面光照、死亡变换和逐三角形顺序。六种普通感染体的 Scene 提取各自复用
一份独立 scratch pose；已蒙皮顶点按资源、bind 模式及采样步态缓存，跨同姿态敌人和帧复用。
缓存只依赖不可变资源和姿态，不保存敌人位置、死亡变换、反馈、世界光照或动作历史；
遇到尚未缓存的顶点时重置独立 pose 并求值。它不与 mixed 展示实例共享可变姿态。
`SCENE-ENEMY-COST` 将敌人准备中的资源更新/创建与 draw 构造/预检分别计时；
两者和 `SCENE-EXTRACT geometry_us` 都包含在 `SCENE-FRAME-COST enemies_us` 中。

## Scene 敌人几何与颜色合批

普通感染体按资源顶点索引缓存一次身体提取中的世界位置和旋转法线。每次身体调用使用新的 stamp，
溢出时清空 stamp 表；位置、朝向、squash、死亡及光照不能跨身体误复用。数组随不可变 recipe 缓存持有，
不共享 mixed 可变 pose。Scene owner 另保留敌人几何工作区，每个身体重置活动计数和光照缓存，
活动顶点、颜色、双面标志与顺序索引全部覆盖；不再逐身体清零完整顶点容量，owner 关闭释放工作区。

敌人及程序角色使用显式 Scene color resource。提取回调直接生成 20 字节
`{position[3], light_q8, rgb24}` 顶点；旧 mixed/skinning 保持 56 字节布局。
Scene color resource 使用独立顶点 stride 与两组输入属性，分别校验光照 `[0,384]` 和颜色
`[0,0xffffff]`；同一三角形三个角必须同色。
仅 `material[3]=2` 的非纹理、非整数深度、非屏幕坐标、非 form-light draw 可以消费此资源。
普通资源仍保持原 UV 范围和 shader 路径。shader 使用 flat 三角形颜色与原有插值光照、整数截断顺序。
同资源内连续三角形只按双面策略拆批，保留原三角形顺序；透明 alpha、层序与深度策略不变。
动态更新仍检查坐标、光照、颜色、容量、布局、owner 和退休状态，失败不提交部分目标。

`SCENE-ENEMY-COST triangles` 统计敌人及程序角色实际提取的三角形，用于合批前后内容核对。
`SCENE-CPU-COST` 细分循环前段、动态来源、冻结、pose、地图资源缓存与 prepare 余项：
`logic_us` 是既有 update 区间，包含在 `loop_prepare_us` 中；`pose_us` 与 `SCENE-EXTRACT local_pose_us`
相同；`misc_prepare_us` 已在总 prepare 中。不得把这些子段重复相加。
`SCENE-SUBMIT-COST` 另记录命令预检/录制、acquire、queue submit 和 present 的 CPU 墙钟。
它们属于 `submit_present_us`，不含全部 blit/barrier 录制与重建善后；fence 仍单列 `retire_us`。
GPU draw timestamp 仍覆盖整个 Scene render pass，不是各层 GPU 时间，也不包含蒙皮。

## 角色 GPU skinning

同步 graphics skin update 在上一提交及 Scene 使用退休后写入输入：目标支持 host-visible 时直接 map/flush，
否则按资源容量保留 staging buffer，随资源销毁。bind 与 palette 仍每帧完整更新。独立 Scene owner 在
`skin_batch_begin/end` 之间收集私有、已退休资源的更新，输入立即复制到资源自有 buffer，end 统一录制
transfer/compute 并提交、等待一次；冷资源创建保持同步。排队资源禁止重复更新或销毁，批次结束前禁止
绘制和顶点读回。cancel 只丢弃未提交 dispatch，重新消费前须再次更新；失败提交仍由 owner teardown
排空后回收。mixed 和显式旧上传诊断保留逐资源同步路径。
Scene actor 以资源 handle/generation、顶点数和 body bind-normal policy 判断 bind 是否不变；命中时只更新
palette，沿用已上传的 bind buffer，仍执行本帧蒙皮。资源新建、容量增长或上述身份变化必须上传完整 bind。
取消批量蒙皮或准备失败时使该缓存失效，下一帧完整上传。
`RF_GPU_SCENE_LEGACY_BIND_UPLOAD=1` 可恢复逐帧完整上传作同包对照。没有跨帧流水或减少蒙皮工作。
非 coherent 内存刷新完整映射分配，transfer 分支保留 transfer→compute barrier，
compute→vertex/transfer barrier 保持不变。host 写入经后续 queue submit 对设备可见。

独立 Scene 的分层 workspace 拥有几何、顶点、顺序索引、纹理快照及稳定排序工作区；WORLD 批次数组
也由 probe 按容量保留。每帧重新提取活动内容，GPU 分层资源在退休后更新顶点及活动索引范围；容量
不足才增长，暂时不活动的 chunk 保留到 owner 关闭。纹理尺寸、通道与源字节快照比较发现变化时只
失效 textured chunk，不能仅以源指针判断内容未变。resize 保留这些资源，world/owner 关闭时释放。
分层顺序、透明顺序、VIEWMODEL 深度清除和 OVERLAY 无深度语义保持不变。

`SCENE-RESOURCE-COST` 报告 skin/全部提交数、fence 等待次数、分层创建/复用数和统一蒙皮批次的
`actor_batch_us`。后者已包含在 `SCENE-FRAME-COST actors_us` 中，不能重复相加。批量模式下
`SCENE-ACTOR-COST upload_skin_wait_us` 对热资源只包含输入复制及排队，不含稍后的统一提交等待；
冷资源和逐资源诊断模式仍包含同步提交。

CPU 继续拥有 pose、IK、socket、gear 和 weapon placement。mixed frame 冻结 finalized palette、bind
position/normal、BDEF influence 和索引；compute skinning 输出写入 frame-slot device-local vertex buffer，
body Draw 直接消费。

normal GPU skinning 不生成 CPU reference。`--gpu-character-vertex-diff` 只为目标帧建立对照；
`--gpu-character-skinning-off` 是正式回滚边界，只恢复 CPU-skinned vertex upload，不改变上游所有权。

## Presenter 与失败边界

Windows native presenter 使用同一 Vulkan device/queue 和唯一 swapchain generation。逐帧审计必须保持
fallback、readback、CPU framebuffer copy、hot queue-idle、非法层转换和 poisoned presenter 为零。
`--gpu-required` 下任何 unsupported、preflight、submit、present、readback 或 CPU copy 都必须非零退出。

省略 `--gpu-native-present` 只用于显式的软件呈现 A/B，不是 required runtime 的降级路径。CPU renderer
仍是独立完整实现，用于 reference 和不启用 GPU renderer 的正常运行。

## 支持边界

- Windows 原生 PowerShell、物理 GPU 和 native present 是主开发与签收环境。
- Linux hosted Vulkan、WSL 与 llvmpipe 只用于编译、ABI 或 correctness 辅助诊断，不能替代驱动、窗口、
  resize、presenter 生命周期或性能结论。
- 设备丢失恢复、跨厂商完整矩阵、validation/sync、fault injection 和长时 soak 属于按风险触发的专项，
  不由日常 Quick 自动替代。
- 运行参数以 package 中 `rasterfall.exe --help` 为准；验收范围与证据要求由 GPU 验收指南拥有。
