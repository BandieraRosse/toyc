# Hardware Graphics 开发计划

> 计划更新：2026-09-19
> 当前进度：HG-0、HG-1A、HG-1B、HG-2A 已实现；HG-2B 已接通真实 Raster ABI compute → graphics → compute GPU bridge，并通过 Intel 交错遮挡回归；下一步实现 Core 混合顺序与 native present 门禁。正常帧仍使用原 CPU/compute 路径。
> 实施入口：[架构与基线](rasterfall/docs/hardware-graphics-architecture.md)；[HG-0 checkpoint 证据与限制](rasterfall/docs/hardware-graphics-hg0.md)。

| Checkpoint | 状态 | 交付/下一步 |
| --- | --- | --- |
| HG-0 | 完成 | ownership/Draw V0/数值合同、可复现 Windows 测量脚本、CPU/compute captures、native/Fog/正式地图波次基线 |
| HG-1A | 完成 | CPU planar 前置修复；普通 opaque static RMESH 按实例/submesh 提交 Draw，同步 reference；命令/color/depth 精确回归与 Windows Intel 基线通过 |
| HG-1B | 完成 | CPU bundle registry、generation、Core 帧 pin 与延迟释放；[实现与验证](rasterfall/docs/hardware-graphics-hg1b.md) |
| HG-2A | 完成 | 持久 device-local VB/IB/texels、flat/nearest、RGBA8/D32 离屏 indexed draw；[数值合同与验收边界](rasterfall/docs/hardware-graphics-hg2a.md) |
| HG-2B | 进行中 | [整数深度、target bridge 与真实 compute/graphics 交错](rasterfall/docs/hardware-graphics-hg2b.md) 已通过独立验证；Core 混合顺序与 native present 待实现 |
| HG-3A / HG-3B | 待开发 | opaque static props allowlist → 扩围 |
| HG-4A / HG-4B | 待开发 | Ground → map/boundary 几何 |
| HG-5A / HG-5B | 待开发 | Character geometry → GPU skinning |

HG-0 校正：显式 frame audit 现逐帧输出；normal near/mid 标签只控制初始相机，稳定位置以实际审计为准；
波次必须显式加载 Campaign 并验证活敌。原始 HEAD 的独立 texture full-scan fixture 已复现失败，
HG-1A 已定位为 CPU planar vertex-lit 忽略 alpha/no-depth-write，并补齐恒定与插值光照路径；
前置记录见 [HG-1A 前置修复](rasterfall/docs/hardware-graphics-hg1-preflight.md)，Draw 接入及验收见
[HG-1A Draw/reference](rasterfall/docs/hardware-graphics-hg1a.md)。当前无 hardware normal-frame 接入。

以下保留原计划及其原始调研记录；其中“本轮未修改跟踪文件”等表述仅描述计划形成时的状态。

---

 建议增加高于 RasterCmd 的 Draw IR，并优先迁移普通 opaque static RMESH。 但在单 mesh proof 与正常帧迁移之间，必须增加一个独立的“graphics/compute 混合目标与深度互
  操作”checkpoint。当前最大的架构缺口是目标所有权和执行顺序，而不是缺少一次 vkCmdDrawIndexed()。

  本轮未修改跟踪文件。已按文档导航核对源码，并重新构建 Windows package，运行 --help、--logic-test、固定视角离屏审计及 Intel Iris Xe 上的 46 帧 strict native
  compute 检查。

  基线有一处需要纠正：

   指标                                              本次重新构建后的结果
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   Ground                                                            3594
  ─────────────────────────────  ─────────────────────────────────────────
   Map records                                                       1527
  ─────────────────────────────  ─────────────────────────────────────────
   Static props                                                      8957
  ─────────────────────────────  ─────────────────────────────────────────
   离屏审计总命令                                                   20321
  ─────────────────────────────  ─────────────────────────────────────────
   离屏 gallery / later                                         98 / 6145
  ─────────────────────────────  ─────────────────────────────────────────
   Native 检查                    46/46 帧，零 fallback/readback/CPU copy
  ─────────────────────────────  ─────────────────────────────────────────
   第 17–46 帧 frontend 中位数                                  23.942 ms

  因此，三项静态统计与你提供的一致；17.6k 总数及 51% 占比不能直接当作统一 normal-frame 基线。离屏与正常帧的 actor、交互物、viewmodel 统计边界不同。现有 gpu-
  current-state.md 的 28k 快照也不应继续作为新阶段性能分母。实机日志保存在 tmp/hg-native-audit.log。

  ## 1. 当前实际架构

  flowchart TD
      A["Gameplay / Session / Runtime Map"] --> B["Game runtime：准备展示状态、相机、调用 renderer"]
      B --> C["场景与实体 producers"]
      C --> C1["ground / map / boundary wall：程序化几何"]
      C --> C2["static prop / body / gear / weapon：RMESH"]
      C1 --> D["CPU frontend"]
      C2 --> D
      D --> D1["顶点准备：变换、CPU skinning、world-to-view"]
      D1 --> D2["逐三角形：材质/光照、near clipping、projection"]
      D2 --> E["toy_raster_cmd：屏幕空间三角形"]

      E --> F["CPU renderer：software color + inverse-depth"]
      E --> G["Core：retained spans、WORLD stable partition、整帧资格检查"]
      G --> H["Raster ABI V1 + Texture V1 pack"]
      H --> I["CPU tile binning"]
      I --> J["Vulkan compute raster"]
      J --> K["GPU storage buffers：color / depth / viewmodel coverage"]
      K --> L["可选 Post → 独立 post_color"]
      L --> M["CPU overlay 上传 + GPU composite"]
      M --> N["buffer → Win32 swapchain → present"]
      K --> O["非 native / diagnostic：readback"]

  具体边界如下。

  - Producer 层已经存在，但没有统一 mesh draw contract。 render_scene() 组织 ground、map、static props 等；普通 prop 经 render_static_props() →
    rasterfall_render_static_prop() → render_gallery_model_range()。最后一个函数把材质解析、顶点缓存和逐三角形提交混在一起。源码入口 (rasterfall/src/
    rasterfall_render.c:1499)

  - RasterCmd 与 Raster ABI 是两个层次。 toy_raster_cmd 是含纹理指针、屏幕顶点、bbox、area 和材质状态的 CPU 内存结构；Raster ABI V1 是固定宽度、无指针、96-byte
    command 的序列化契约。两者都已经位于投影和裁剪之后。RasterCmd (include/toy_renderer.h:41)、Raster ABI (rasterfall/include/rf_gpu_raster_abi.h:1)

  - RMESH 已具备 mesh/submesh 结构。 RFM2 包含 vertex/index table，以及 primitive 的 first-index、index-count、material-index；位置为整数，法线 Q15，UV 为 unsigned
    Q16。应读取 position_scale 和既有 presentation 变换，不能假设文件中的整数可直接作为最终世界坐标。格式与运行时结构 (rasterfall/include/rasterfall_model.h:15)

  - 资源生命周期尚未统一。 RFCHAR 已有 immutable model_resource 与独立 model_instance；普通 static props 仍是 renderer 内懒加载的 static_prop_models[]，未找到对应
    统一卸载入口。GPU persistent cache 不能直接依赖这种进程存活期假设。

  - Core 拥有帧决策。 WORLD 完整收集后做 opaque/transparent stable partition，再结合 effects/viewmodel 决定整帧 GPU 消费或 CPU replay；strict 模式禁止回放和
    software present。retained 与决策代码 (rasterfall/src/rf_core_host.c:542)

  - 当前 Vulkan 没有 graphics attachment 链路。 color、signed inverse-depth、post_color 均是 buffer；swapchain 属于 rf_gpu_vulkan_raster，仅作为 transfer
    destination。设备队列选择检查 compute，之后检查 present，没有要求 graphics。backend 资源 (gpu/src/rf_gpu_vulkan_backend.c:925)

  - compute dispatch 不能直接续画已有帧。 shader 每次从局部 color=black、depth=0 开始，遍历本次命令后写出结果。shader (gpu/shaders/raster_v1.comp:147)
  - Linux freestanding normal runtime 与 Windows GPU 接线也并不对称；共享 Vulkan backend 有 hosted Linux 诊断入口，不能把它等同于 Linux normal native GPU 已完成。

  ## 2. Hardware Graphics 目标架构

  flowchart TD
      A["Gameplay / World truth"] --> B["Presentation producers：选择资源、LOD、实例变换、展示策略"]
      B --> C["Draw IR：mesh / submesh / instance / material"]
      C --> D["Reference lowering"]
      D --> E["现有 RasterCmd"]
      E --> F["CPU raster"]
      E --> G["Raster ABI + binning → compute raster"]

      C --> H["Hardware graphics encoder"]
      R["持久 mesh / texture / material resources"] --> H
      H --> I["indexed draw：VS / clipping / raster / interpolation / depth"]

      G --> J["Core 有序帧计划 + GPU target contract"]
      I --> J
      J --> K["Post / overlay / native present"]

      L["尚未迁移的 RasterCmd producers"] --> E

  迁移期间，Core 的有序帧记录可以同时包含 DrawSpan 和 RasterSpan，但它们不应混成一种低层指令。

  判断 Draw IR 是否成功的硬指标：对已迁移 mesh，CPU 每帧的工作随实例数和 submesh 数增长；不再随该 mesh 的三角形数增长。

  资源注册时一次性遍历三角形、整理 GPU 数据可以接受；每帧生成“一 triangle 一 Draw”不能接受。

  ## 3. 推荐抽象边界

  Draw IR 最小契约

  建议区分帧级 View、持久资源和逐 draw 数据，避免一个不断膨胀的结构。

   层次                 V0 必要内容
  ━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   View                 相机变换、viewport、projection、near plane、WORLD/VIEWMODEL depth domain
  ───────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
   Mesh resource        稳定 ID＋generation、vertex/index 数据、submesh ranges、bounds、单位/顶点格式
  ───────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
   Material resource    base color、texture handle、nearest/address mode、alpha、sidedness、既有 shading policy
  ───────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
   Draw item            mesh handle、submesh/range、实例变换、material binding/override、scene-light Q8、fog policy、layer/domain、稳定顺序
  ───────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
   诊断信息             producer 类别、实例标识；不参与 gameplay truth
  ───────────────────  ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
   生命周期             引用有效期、帧冻结时点、资源释放/失效规则

  V0 topology 固定为 triangle list；实例默认一次一个，暂不要求 instancing、indirect draw 或 bindless。

  Draw 中不应出现 screen-space 三顶点、area、bbox、裁剪后的扇形、u_over_z。这些属于 lowering 的输出。

  最佳 producer 位置

  第一入口放在 rasterfall_render_static_prop() 完成 profile、资源选择和实例策略解析之后，进入 prepare_gallery_vertex_cache() 之前。

  render_gallery_model_range() 应逐步拆成：

  1. mesh/submesh/material policy 解析；
  2. Draw 提交；
  3. reference lowering，保留现有整数变换、光照、裁剪、投影代码。

  不能在 toy_renderer_triangle_*() 上包装 Draw IR：到那里 CPU 成本已经发生。

  同时必须把 active_scene_light_override_q8、gallery facing、强制背面剔除等隐式状态，冻结为提交时的显式值。延迟执行时不能重新读取这些全局变量。

  持久资源所有权

  建议采用两层资源：

  - renderer 资源注册表：持有 CPU 可读定义、稳定 handle/generation、submesh/material 描述；
  - Vulkan backend cache：持有 device-local vertex/index buffer、纹理存储、descriptor、pipeline 及上传 staging。

  Gameplay/session 不持有 GPU handle；model_instance 继续拥有姿态，不拥有重复的 immutable mesh。

  V0 纹理可先采用持久 buffer-backed texels，在 fragment shader 复用现有 nearest、repeat 和整数寻址语义。无需同时引入 sampled image、mipmap 或新的过滤规则。当前
  Texture V1 的每帧 handle 仍服务原 compute 路径，不必立即重写。

  生命周期必须明确：

  - mesh/texture 首次使用上传；静态资源稳态上传量为零；
  - instance/light/material override 写入帧数据；
  - resize 只重建尺寸相关 targets，不重传 mesh；
  - world unload、resource reload、device 重建具有 generation 失效规则；
  - GPU 完成引用后才能释放资源；
  - CPU backing 保留到 reference replay 不再需要它。

  CPU/reference 与 compute 的复用

  HG-1 让 Draw IR 全部 lowering 回原 RasterCmd，分别验证原 CPU 路径和原 compute 路径保持行为。

  hardware 模式只对已批准的 draw 范围绕过 lowering。未迁移 producers 继续生成 RasterCmd。CPU fallback 从冻结的 Draw＋Raster 帧记录重建，不能为“随时 fallback”而在每
  个成功 hardware frame 背后偷偷执行完整 reference lowering。

  ## 4. 混合帧、深度和光照：必须明确的设计

  采用同一个 Vulkan device 和帧编排器

  保留现有 compute backend，新增 graphics executor；首版优先使用同时支持 graphics、compute、present 的同一队列，保持单帧在途。不要同时引入多队列和多帧并行。

  后续应把 present 生命周期从 rf_gpu_vulkan_raster 拆到共享 presenter/target owner，保留现有 compute API 的适配入口。

  必须新增 Render Target / Synchronization Contract

  推荐 V0 以 GPU 内转换连接：

  compute 前段：ground/map 等原有命令
      ↓ GPU color/depth export
  graphics attachments：static RMESH indexed draws
      ↓ GPU color/depth import
  compute 后段：其余 world、transparent、effects、viewmodel
      ↓
  现有 Post → overlay → native present

  这要求：

  - compute 增加明确的 CLEAR / LOAD_EXISTING 执行模式；
  - graphics attachment 明确 load/store；
  - clear、sky 只能在指定起点执行；
  - graphics/compute 分段不能改变 WORLD partition 后的原有顺序；
  - VIEWMODEL 保持独立 depth 与 coverage；
  - Post 仍只执行一次；
  - 全程不经过 CPU framebuffer/readback。

  目标描述需覆盖 format、extent、color encoding、depth encoding、有效内容、读写者及访问转换。backend 必须补齐 attachment writes、early/late depth tests、compute
  reads/writes、transfer 之间的 barrier 和 image layout 转换。现有 compute→transfer barrier 不足以覆盖 graphics。Vulkan 同步规范

  深度是首要技术风险

  现有规则是：

  vertex inv_z = floor(1048576 / view_z)
  pixel depth  = 屏幕空间插值后整数截断
  clear        = 0
  compare      = >=，同深度后提交者覆盖

  不能把这些整数直接复制进 D32 image，也不能假设普通 floating-point reversed-Z 与它等价。

  建议混合期优先验证“保留量化 inverse-depth”的兼容方案：以明确比例映射到 D32，GPU bridge 做数值转换；如需 fragment shader 输出量化深度，也必须单独测 early-depth 优
  化受限的成本。hardware depth attachment 仍负责测试与写入。

  但顶点量化、near clipping 后的新顶点、屏幕采样位置、相等深度规则必须通过 HG-2 专门 fixture 决定。若映射无法满足遮挡门禁，就停止 normal-frame 接入，不能靠放宽整图
  误差蒙混通过。Vulkan depth target 是独立 image/attachment，其格式能力也需要查询。Vulkan depth 文档

  V0 保持现有 lighting/material

  当前 static RMESH 的实际语义是：

  - 实例世界原点采样一次 World Lighting V2；
  - 三个变换后顶点法线求平均；
  - 用整数 directional＋ambient 得到逐三角形常量 form light；
  - flat 与 textured 路径保留各自的颜色调制顺序和舍入；
  - static prop 设置 gallery lighting，primitive fog 当前为零；可选 Post Fog 是另一层。

  因此不能直接换成 Gouraud 或逐像素 Lambert。

  最低成本方案是 GPU mesh 注册时准备 primitive 的法线信息。可以使用带 primitive 属性的 corner vertex 数据，必要时复制顶点；每个 submesh 仍一次 indexed draw。若要求
  匹配当前整数舍入，应保留三个源法线分别变换后再平均，而不是仅预存平均法线。

  GPU 数据中的 primitive 属性不是 per-frame triangle command；二者的生命周期和 CPU 提交成本完全不同。flat 插值本身只取 provoking vertex 的值，不会自动计算三法线平
  均。Vulkan shader 插值规则

  透明材质继续保留 texel_alpha × material_alpha / 255、source-over、no-depth-write 和原顺序。HG-3 首批只迁 opaque submesh，避免同时承担 blend 舍入和透明排序迁移。

  ## 5. 主要技术风险

   风险                                  必须采取的约束
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   CPU 整数 raster 与硬件覆盖规则不同    HG-1 要求严格一致；hardware 使用明确的边缘误差规则，遮挡错误单独判失败
  ────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────
   混合 depth 不一致                     同深度、远处薄墙、近面交叉、交错遮挡设为接入前门禁
  ────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────
   implicit frontend state 丢失          提交时冻结 transform/light/material/culling，禁止 consumer 读取变化后的全局状态
  ────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────
   graphics/compute 重排                 保存 draw 与 legacy span 的稳定顺序；不先做跨 producer 排序优化
  ────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────
   持久缓存引用失效                      handle generation、frame pinning、GPU 完成后释放
  ────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────
   静态统计掩盖部分未迁移                按 asset/submesh 统计 eligible、migrated、legacy、拒绝原因
  ────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────
   上传或 bridge 抵消收益                分开统计 resource upload、instance upload、bridge bytes/time、CPU lowering 时间
  ────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────
   strict 被“GPU 混合”名义弱化           预定 compute 范围正常；要求 hardware 的 draw 意外退回 lowering 必须被单独识别
  ────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────
   平台能力假设                          graphics/compute/present queue、attachment format、shader 能力独立查询
  ────────────────────────────────────  ─────────────────────────────────────────────────────────────────────────────────
   回归测试要求不现实                    保留 CPU↔compute 精确 oracle；不要求 hardware 无条件逐像素等同整数 raster

  Static props 仍是最佳首批 producer：immutable、实例变换简单、无 skinning、world light 已按实例采样、占据最多静态命令。

  但需要明确排除：

  - BOUNDARY_WALL：实际走程序化盒体与 planar lighting；
  - 不支持的 material/submesh；
  - 透明部分；
  - 需要保留的 architectural 强制 backface-culling 策略。

  ## 6. 分 checkpoint 实施计划

  所有 checkpoint 独立提交。关闭新路径后应能回到原 CPU/compute 路径，且不要求回滚地图、资产或 gameplay。

  HG-0：事实与架构冻结

  交付 producer/ownership 表、Draw V0 草案、数值语义清单、场景清单和测量脚本。

  - Correctness：冻结 CPU、compute reference captures；记录所有相关隐式状态、排序、depth、fog、alpha 规则。
  - Windows：记录 commit、exe/资产标识、GPU/driver、相机、尺寸、预热区间；CPU logic、compute strict/Fog、near/mid、波次基线。
  - 退出条件：总命令与各分项口径可复现；不把目前不同入口的统计混为一谈。
  - 回滚：仅文档和诊断。

  HG-1A：Draw Contract＋静态 producer，全部回落 RasterCmd

  先接一个普通 prop，再覆盖普通 static RMESH。reference lowering 保持原操作顺序和精度。

  - Correctness：旧 producer 与新 Draw lowering 的规范化命令字段、顺序、完整 color/depth 一致；覆盖 yaw、scale、缺失纹理、背面剔除、近裁剪和场景光。
  - Windows：CPU captures、compute differential、strict native/Fog 均无新增差异或 fallback。
  - 退出条件：每实例按 submesh 提交；producer 不逐 triangle 构造 Draw。
  - 回滚：切回旧 producer。

  HG-1B：资源身份与生命周期

  建立 renderer resource registry 和稳定 handle；此阶段可先只有 CPU backing。

  - Correctness：共享资源不重复注册；卸载/重载不会命中旧 generation；帧内引用有效；两个实例不共享可变状态。
  - Windows：重复 world switch、资源失败、resize、退出均无悬挂引用或持续增长。
  - 回滚：Draw 继续引用原资源适配器。

  HG-2A：单 mesh indexed-draw proof

  新增 graphics capability、持久 VB/IB、opaque shader、离屏 color/depth attachment；先 flat，后 nearest texture。

  - Correctness：变换、winding、near clipping、UV、form light、Q8 舍入、单/双面验证；诊断 readback 允许。
  - Windows：Intel 物理 GPU 跑数值与图像 fixture；重复绘制不重传静态 mesh；resize 后正常重建 targets。
  - 退出条件：CPU 不执行该 mesh 的 per-triangle frontend；硬件 depth 真正参与。
  - 回滚：独立诊断入口，normal frame 不变。

  HG-2B：混合 target、depth、顺序与 native present

  这是 HG-3 的强制前置。

  - Correctness：构造 compute→graphics→compute 的交错遮挡、同深度、透明后段、VIEWMODEL coverage、Fog/Post fixture；验证 clear/load 和双向转换。
  - Windows：strict native 零 readback/copy；resize、最小化/恢复、swapchain 重建、退出；记录 bridge 成本。
  - 退出条件：遮挡和层顺序门禁通过；unsupported preflight 不会留下半帧；故障按 strict policy 失败。
  - 回滚：仍不切换 normal producers。

  HG-3A：少量 opaque static props 正常帧试点

  选 flat 主体＋局部 RGB 纹理的资产，按 asset/submesh allowlist 启用。

  - Correctness：真实 Campaign 场景、多方向、近距离穿越、与 map/actor 相交；A/B 检查剔除和阴影区亮度。
  - Windows：相同场景跑 CPU、compute、混合模式；120 帧 strict smoke＋320 帧波次；测 frontend 与 frame-time 中位数/P95。
  - 退出条件：已迁移部分 cpu_lowered_triangles=0，稳态 VB/IB/texture 上传为零，无重复绘制或漏画。
  - 回滚：关闭 allowlist。

  HG-3B：扩大普通 opaque static RMESH

  逐类扩展，boundary wall 和透明部分继续明确留在旧路径。

  - Correctness：建筑内部、远处薄墙、反面、不同 yaw/scale 全覆盖。
  - Windows：同机同场景至少三轮 A/B；frontend 收益应超过运行波动，整帧不能被 bridge 成本明显拖慢。
  - 退出条件：按资产列出的 eligible 范围全部走 hardware。8957 是对照命令量，不是强制归零目标。
  - 回滚：逐类开关。

  HG-4A：Ground；HG-4B：Map 与 boundary geometry

  先把 floor partition、paint、细分结果变成 world generation 对应的持久 mesh；再迁 wall/box/ramp/platform 等 render geometry。

  - Correctness：保留 Campaign 与 WHU 不同 paint precedence、spawn priority、接缝、无雾策略、V2 vertex light；保留 planar light 的屏幕空间插值语义。air gate 等动态
    可见性继续由展示状态选择。

  - Windows：Campaign/WHU captures，掠射角、接缝、门开关、resize 与 strict/Fog；静态几何不随相机每帧重建。
  - 退出条件：不更改 Runtime Map/collision/gameplay truth；仅更新 renderer cache 和 invalidation。
  - 回滚：ground 与各 map producer 分别切换。

  HG-5A：Character geometry；HG-5B：GPU skinning

  先保留 CPU pose/IK/skin，上传每实例变形后的顶点，让 hardware 接管后续几何链路；再独立迁移 skinning。

  - Correctness：双实例隔离、finalized pose/socket、职业 palette、刚性装备、LOD、步态/瞄准/后坐、死亡表现；GPU skinning 对照 CPU 顶点/法线结果。
  - Windows：角色 acceptance＋真实 world 多角色场景；记录动态 vertex/palette 上传量和 CPU skinning 成本；strict、波次和生命周期检查。
  - 退出条件：不把 animation/IK authority 搬进 GPU；socket 与 weapon mount 继续消费既有 finalized pose。
  - 回滚：先回 CPU skinning，再回 reference geometry，两个开关独立。

  建议统一增加以下审计字段：

  draw_items / instances / submeshes
  hardware_draws / hardware_source_triangles
  legacy_raster_commands / cpu_lowered_triangles
  resource_upload_bytes / instance_upload_bytes
  bridge_bytes / bridge_ms
  unexpected_lowering / unsupported_reason

  ## 7. HG-0/HG-1 优先模块，以及现在决定和延迟的事项

  最值得先修改或新增的接口

  以下名称为建议，当前仓库尚无这些模块：

   模块                                           职责
  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
   rasterfall/include/rf_draw.h                   View、Draw、handle、layer/domain、material/light policy
  ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────
   rasterfall/src/render/rf_draw.c                validate、record、冻结顺序与帧引用
  ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────
   rasterfall/src/render/rf_draw_reference.c      Draw → 原 RasterCmd lowering
  ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────
   rasterfall/src/render/rf_render_resources.c    mesh/texture/material 注册、generation、释放
  ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────
   rasterfall_render_frontend.h/.c                从隐式 scope 提取显式 submission state
  ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────
   rasterfall_render.c                            static prop producer 接点；拆分 model range 的职责
  ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────
   rf_core_host.h/.c                              有序 Draw/Raster spans、preflight、replay、统计
  ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────
   rf_gpu.h/.c                                    后续 graphics/target/resource capability 与无 Vulkan 类型的服务边界
  ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────
   gpu/include/rf_vulkan_min.h                    HG-2 才扩展 graphics/image/render-pass ABI
  ─────────────────────────────────────────────  ─────────────────────────────────────────────────────────────────────
   gpu/src/ 新 graphics/target 模块               复用现有 device，避免继续扩大单个 backend 文件

  HG-0 文档应新增 hardware-graphics-architecture.md，并同步总索引、rendering、gpu-current-state 的入口及基线字段。HG-1 新编译单元同时检查根 Makefile、Windows
  Makefile 和适用 self 规则。

  必须现在决定

  - Draw 的粒度是 mesh/submesh/instance，禁止 screen-triangle 化。
  - producer 与 reference lowering 的分界位置。
  - 资源身份、generation、帧引用及销毁责任。
  - 坐标、单位、pivot、变换顺序与现有整数语义。
  - layer/domain、透明分类、稳定顺序和 equal-depth 规则。
  - 混合期 depth/color bridge 的验证合同。
  - CPU/compute 保持精确回归，hardware 使用独立、明确的误差验收。
  - strict 中“预定 compute 范围”与“意外退回 lowering”的区别。
  - HG-3 首批仅普通 opaque static RMESH。

  应延迟设计

  - GPU-driven culling、indirect/multi-draw、bindless、mesh shader；
  - 通用 render graph、多队列、多帧在途；
  - 自动资源淘汰、复杂 streaming；
  - 全套 GPU animation/IK；
  - 通用材质节点系统、PBR、阴影、新视觉效果；
  - hardware 原生高精度 depth 全面替换现有量化深度。

  角色方面现在只需保留“immutable mesh＋独立 deformation binding”的边界。V0 可以只支持 rigid，后续再加入 CPU-deformed vertex stream 和 skin palette；无需现在冻结骨
  骼 buffer 布局或把全部动画系统塞进 Draw IR。

  推荐实施顺序是 HG-0 → HG-1A/1B → HG-2A/2B → HG-3。只有 HG-2B 的混合遮挡与顺序门禁通过，才开始正常帧 static props 迁移。
