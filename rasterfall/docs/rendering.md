# 渲染、HUD、特效与性能

> 文档更新：2026-09-19
> 源码核对基线补充：2026-09-19 [HG-2B 真实交错桥接](hardware-graphics-hg2b.md#真实-raster-abi--graphics-交错桥接)：`rf_gpu_graphics_raster_draw()` 在同 device/extent 的未结束 Raster target 中插入整数 indexed draws，GPU 内双向传递 color/depth；`rf-gpu-raster-test --mixed-gate` 验证交错顺序。Core/native 混合接入仍待实现。
> 源码核对基线补充：2026-09-19 [HG-2B Raster ABI 分段基础](hardware-graphics-hg2b.md#raster-abi-分段基础hg-2b-进行中)：`rf_gpu_vulkan_raster_segment()` 使用独立范围/CLEAR/LOAD 参数，验证完整 stream；中间段不读回，VIEWMODEL/Post 留在末段。真实交错已由 `rf_gpu_graphics_raster_draw()` 接通；Core/native 接入仍待实现。
> 源码核对基线补充：2026-09-19 [HG-2B 整数深度与 target bridge](hardware-graphics-hg2b.md) 已实现 GPU 整数裁剪/投影/深度、GPU color/depth 往返转换及 attachment LOAD；Intel 前置门禁通过。Raster ABI CLEAR/LOAD 分段基础已在 Intel 验证；compute/graphics 桥接已通过 Intel 固定 fixture；Core 混合顺序与 strict native 门禁仍待实现，正常帧不变。
> 源码核对基线补充：2026-09-19 [HG-2A graphics proof](hardware-graphics-hg2a.md)：`gpu/include/rf_gpu_graphics.h` / `gpu/src/rf_gpu_vulkan_graphics.inc` 拥有独立 indexed draw 与离屏 target，`graphics_v0.vert/.frag` 执行整数变换、form light 和 opaque flat/nearest。正常 renderer/Draw lowering 不变；混合遮挡与 CPU 采样一致性属于 HG-2B 门禁。
> 源码核对基线补充：2026-09-19 [HG-1B 资源生命周期](hardware-graphics-hg1b.md)：`rasterfall_render_resources.h` / `render/rasterfall_render_resources.c` 拥有 static prop CPU 资源；`rf_core_host.c` 负责帧 pin 的完成，`rf_game_lifecycle.c` 负责 world 失效。`draw-resources` 审计报告实际存活、退休、pin 与加载释放计数。
> 源码核对基线补充：2026-09-19 [HG-1A Draw/reference](hardware-graphics-hg1a.md)：`rasterfall_draw.h` 定义 CPU-backed Draw，`render/rasterfall_draw_reference.inc` 解析 submesh 并同步 lowering；`--frame-audit` 输出实例、Draw、源三角形及拒绝原因。
> 源码核对基线补充：2026-09-19 [HG-1A 前置修复](hardware-graphics-hg1-preflight.md)：`lib/graphics/renderer.c` 的 planar vertex-lit 插值与恒定光照分支均消费 `material_alpha` 和 `transparent_no_depth_write`；alpha=0 不改 color/depth/coverage，透明不写深度。GPU kernel 与 ABI 未变。
> 源码核对基线补充：2026-09-19 HG-0 冻结 [Hardware Graphics 架构与基线](hardware-graphics-architecture.md)；显式 `--frame-audit` 改为逐帧输出，测量脚本记录各入口独立口径与原始证据。
> 源码核对基线补充：`--normal-frame-audit` 输出已有 scene 阶段命令边界的 floor/map/static/gallery/character/private/projectile/later 汇总；later 是该离屏入口在 scene 后提交的 flags、enemies、AI 与文字等命令，不等同于正常 `--frame-audit` 的 world-submission 分类。
> 源码核对基线补充：正常地图遍历在提交前按 render record 的三维包围盒剔除完全屏外的 wall、texture、box、ramp、platform；保留近面交叉记录及原 map command range 顺序。MODEL、LABEL、SIGN 不套用这些几何 bounds。
> 源码核对基线补充：ground 2048 RFU 大板在分区/颜色查找前、boundary wall 组件盒在面生成前按保守视锥剔除；普通 static RMESH 保留现有模型级顶点准备前剔除。近面交叉仍交给三角形裁剪。
> 源码核对基线补充：正常 AI 的提交前侧平面检查按展示路径选横向半径：程序化角色 1600 RFU，模块化角色仍为 2600 RFU；垂直半径仍为 2600 RFU。程序化范围覆盖最大 920 RFU 武器模型、260 RFU 握持偏移、55 RFU 动画前后移以及身体 175/110 缩放与倒地旋转余量；模块化身体、装备和 socket 武器不套用这一较窄界限。近面仍由逐三角形裁剪。
> 源码核对基线补充：正常 AI 完成身体、装备和武器提交后，按每条命令的三顶点屏幕包围盒原地压紧该 actor 的命令段；完全位于任一视口侧平面外的命令不会进入 CPU/GPU flush，保留命令的相对顺序不变。`ai-triage offscreen_cmd` 仍统计移除前的数量。此阶段不节省姿态或逐三角形提交耗时；继续做提交前剔除需覆盖角色、装备、武器的空间边界。
> 源码核对基线补充：正常 AI actor 在既有前后距离检查后、动态光照与模型/装备/武器提交前，使用以角色根部上方为中心的保守侧平面检查；各展示路径的水平半径见上条，垂直半径 2600 RFU。穿越近面的角色仍由原逐三角形近裁剪处理。`ai-triage screen_culled` 计数这一早期剔除。该检查共用于 CPU/GPU frontend，不改变玩法状态或开发者展示角色。
> 源码核对基线补充：`--frame-audit` 的 `ai-triage` 记录 AI depth 门槛前后数量、命令为零及屏幕包围盒完全在外的 actor/命令数、模块化身体和武器的源三角形数，并细分身体蒙皮/缓存/三角形及武器准备/三角形耗时。屏幕包围盒是保守无像素贡献判据，不等同于最终 depth 可见性；Campaign 开发者展示角色计入 body/weapon 命令和源三角形，不计入 gameplay actor 数。
> 源码核对基线补充：`--frame-audit` 的 `ai-detail` 按模块化 AI 的动作求值、身体、被动装备、当前武器记录提交耗时，按身体、装备、武器和程序化回退记录命令数，并列出各路径角色数；这些是 frontend 提交阶段数据，不是可见三角形或独立 GPU 耗时。分项之和可能小于 `ai_teammates_ms`，其余为视锥检查、光照及调用边界等开销。
> 源码核对基线补充：`--frame-audit` 输出 normal scene 的 floor/map/static/gallery/character/private/projectile 命令分布与分段耗时，并拆分 world 的敌人、AI 队友、托管玩家、网络队友、文字和交互物提交；静态物件和陈列台耗时已从原合并统计中单独采样。该边界是 scene batch 提交数，不代表最终可见三角形数。
> 源码核对基线补充：GPU 性能审计将 `vkQueuePresentKHR` 与随后每帧 `vkQueueWaitIdle` 分开计时；`--frame-audit` 增加 classification、texture measure、binning 和上传阶段耗时。`fence_wait_ms` 仍是 CPU 等待墙钟时间，不是 GPU timestamp。
> 实测状态补充：用户确认地图核心游玩及窗口拉伸正常；正式地图 `--gpu-wave-repro --frames 320 --renderer gpu-compute --gpu-required --gpu-native-present --frame-audit` 产生 320/320 帧 `gpu-native`，零 fallback/readback/CPU copy。最近固定视角快照和当前边界见 [GPU 当前状态](gpu-current-state.md)，旧冻结矩阵见 [历史记录](archive/gpu-v1-final-acceptance-2026-09-19.md)。
> 源码核对基线补充：`--gpu-wave-repro --legacy-map` 在 session reset 后将真实波次倒计时设为 1ms，逐次打印 phase、alive 和 queued；与 `--frames`、`--gpu-required --gpu-native-present --frame-audit` 组合可直接检查敌人逐步出现的正常 world GPU 帧，不注入平台按键事件。
> 源码核对基线补充：2026-09-19 `rf_core_host.c` retained WORLD partition 同步实际分配容量；跨帧缩小/增长回归覆盖缓存复用。
> 源码核对基线补充：2026-09-19 Texture V1 measure 从最近命令检查重复纹理，packer 通过本帧唯一纹理视图表复用 handle；老地图右转进入约四万条 retained command 的高负载视野时，不再二次回扫此前全部命令并触发 200ms watchdog。
> 源码核对基线：RenderFrame V1 与 GPU-8B2d 已达到 local pass；GPU Required Runtime Contract 已禁止 strict 模式的 CPU replay/software present/readback/copy。Windows Intel strict native smoke 与 Fog/Post smoke 已各通过 120 帧，适配器为 Intel Iris Xe；正式地图 320 帧零回退复现及窗口拉伸已确认。Legacy anime normal renderer 已编译期隔离，anime actor 保留 gameplay identity 但统一落入 modular/procedural humanoid presentation，原 toon/material `0x40` 不再进入 normal frame。
> 当前调试原则：`--frame-audit` 同时输出到控制台和 Windows `rasterfall.log`，记录 frame ID、最终路径、层计数、fallback 分类、timing 与传输字节；Windows 实机仍是 native present 与 resize 的最终验收环境。
> 源码核对基线补充：Eula 正常 world/展示在 near/mid 使用 Gameplay Hybrid `eula_lod3.rmesh`，仅 FAR（4096 RFU 起）切换 compact LOD2；Maid 保持原策略。
> 源码核对基线补充：`--eula-animation-acceptance` 在 UI/Core/window 前早退，复用 legacy VMD evaluator、model instance、CPU skinning、Lighting V1 与标准 AK submission；`--character-performance[-suite]` 统一输出模型 CPU、raster wall 与 total wall 的 mean/median。
> 源码核对基线补充：2026-09-15 工作区；V2 Planar Raster Optimization 为 V2 无纹理平面建立专用不透明 solid/interpolated-light/fog/depth 路径，不再用 NULL texture/material fallback；旧路径仅作 `--render-performance` 的 `generic-planar` 逐像素 A/B。
> 源码核对基线补充：Maid 正常 world/展示仍优先 LOD2；Eula 按距离选择 Hybrid/compact LOD2。Campaign Maid 四人内容武器为 AK。
> 源码核对基线补充：Static World Lighting V2 — FROZEN；64×48、ambient/sun 192/64、contact 8/320 RFU、1024 RFU平面细分保持；契约与验收见 [Phase D](static-world-lighting-phase-d.md)。
> 源码核对基线补充：Static World Lighting V2 Phase C3：V2 为唯一正常 runtime world-light source；V1 独立 diagnostic owner、显式 fixed override 与统一 scene factor，见 [Phase C3](static-world-lighting-phase-c3.md)。
> 源码核对基线补充：Static World Lighting V2 Phase C2：正常 actor/enemy root 单点采样，世界武器继承 owner，本地第三人称/viewmodel 共用 sample，保留 form/material policy；固定诊断例外见 [Phase C2](static-world-lighting-phase-c2.md)。
> 源码核对基线补充：Static World Lighting V2 Phase C1：正常 map static RMESH 在 `render_static_props()` 按实例世界原点采样一次 V2，通过已有 scene override 与原 form lighting 组合；诊断/gallery 不变。见 [Phase C1](static-world-lighting-phase-c1.md)。
> 源码核对基线补充：Static World Lighting V2 Phase B：64×48 Runtime collision ray/AABB field；主地面、primitive architecture、boundary walls、ramp/platform 接入，既有顶点亮度光栅路径平滑建筑平面；static RMESH/actor 保留 V1/bypass 策略。
> 源码核对基线补充：Static World Lighting V2 Phase A：world-light bake/sample 移入独立模块，保持 V1 算法与原 triangle helper 的策略/舍入；static RMESH 与主地面仍 bypass。
> 源码核对基线补充：Campaign Continuous Wall / Floor 与 Component Collision：`boundary_wall` 为长度参数化 RFU 墙体；`attr.collision=component|boundary|none` 在 Runtime Map 展开独立碰撞，保留 object owner ID；布局导出调用 C inspector 获取实际碰撞。
> 源码核对基线补充：Campaign `env_arch_*` 复用正常 static prop renderer；V1 object.y 经 projection 保存，pivot 为 `-900 + map_prop->y`，旧 prop 的 y 为 0。
> 源码核对基线补充：Return-to-WHU 地面策略仅为 `rasterfall_world_uses_authored_ground(world_id)` 一个布尔查询，owner 为 world content 模块。旧 world 保持 checkerboard、spawn 优先和首个 floor paint 优先；WHU ground 使用地图颜色，后提交 floor paint 覆盖先提交颜色。道路/广场/操场使用 floor 而非零高度 box，所有颜色在同一 y=-900 presentation 平面分区，不增加深度层或修改玩法高度。WHU `--map ... --environment-capture <dir>` 复用正常 renderer 输出 A18、B 广场、分馆前场、D→E/F 四个站立眼高 BMP；A18 camera 读取地图定义。
> 源码核对基线补充：campus-corner与near/mid/far、campus-asset-*复用dev-tests实际static prop/quad；资产开放视觉壳处理旧双面薄板深度竞争，未改renderer。
> 源码核对基线补充：闭合 Architectural V1 prop 的 scoped backface culling 修复旧 static RFM2 双面薄墙穿透；frontend state 保存并恢复提交策略，其他模型不变。
> 源码核对基线补充：Visual CLI arch-family / arch-alley / arch-hall 与 inside/far/reverse 使用实际 prop、modular actor 和 enemy renderer；隔离表面研究不改正式场景。
> 源码核对基线补充：`--environment-capture` 固定 seed、headless Core、显式 Campaign world request；共享现有 world BMP capture，十二个设施/路线视角。
> 源码核对基线补充：Enemy Procedural Rig V1；三特感 profile/pose 分离、posed tongue socket、真实命中 particles、Charger/Tank 击飞真实位置历史 ribbon 与固定 world/silhouette capture。
> 源码核对基线：工作区（Enemy Visual V2 六份公开 RFM2 / renderer-only family；Character Material Lighting Policy V1；Enemy Presentation V1 1000ms ballistic body fade / rotating irregular fragments / directional trailing emitter / 10% legacy death；Humanoid Action Composition V1.1 additive recoil；modular RFANIM 独立 locomotion 时钟与双手持枪轨道；RFCHAR +Z forward basis；PRIMARY_GRIP weapon presentation；开发者 world strip 与战斗区共用 modular path；出生点 V2 action debug station；双正式四人 squad；Lighting V1；renderer frame ownership cleanup）

> 源码核对补充：正式 Hurd actor 通过四个专用 character profile 进入职业外观；恢复的四名 Maid 旗卫以 Maid character profile 接入 actor，同时继续由 anime identity 选择骨骼模型；普通 player、Eula、佣兵解析为 NONE。

正常 world render 的开发展示由 Game-owned World Content policy 控制：
`model_gallery` 与 `Character Test Strip` 只在 Campaign 01 启用，Outpost
不会调用这些 Campaign fixture（包括固定 Eula、developer strip 与 Humanoid debug）。明确的 visual/model diagnostic CLI 仍
使用各自的离屏 fixture，不受正常 world policy 影响。

Profession Modularization V1 的提交顺序是 finalized shared-body instance → passive rigid gear → active
weapon presentation；active weapon 从 finalized instance 的 `WEAPON_R` 对齐 authored `PRIMARY_GRIP`，不
读取 `pose_calibration_local`，绘制前执行左臂 FOREGRIP attachment IK。`rasterfall_render_character_instance()` 的 shirt/pants override
仅在单次 submission 生效，不修改 immutable resource material table。地图初始普通队友 Jesus 是首个
vertical slice：actor 的 stable character ID 解析到 Rifleman recipe，presentation runtime 按 actor index
持有独立 instance；`--visual-capture modular-teammate` 固定观察 idle/move/fire/reload/hit。

正式 RF 小队的每个 actor 先由 character ID 解析到 profile，再由 profile 的 modular recipe 选择
共享 V2 body 和共享 rigid gear resources；`render_modular_ai_teammate()` 按 actor index 保持独立
model instance。这个路径不从 actor 读取资源路径、gear list 或 palette override，失败时仍回退到
既有 procedural actor。

modular actor 在提交 body 前把玩法 animation semantic 适配为固定 action layers：IDLE/MOVE 更新并
保留 lower IDLE/WALK，FIRE 只替换 upper 为 RIFLE_FIRE，因此移动中开火不会清掉腿部动作；普通持枪
状态使用 RIFLE_IDLE。AIM clip 已可由 composition/CLI 使用，等待 gameplay 明确 aim semantic 后再由
adapter 接入，不能从骨骼姿态反推瞄准。组合完成并更新 bones 后，被动 gear 只读 HEAD/CHEST/BACK/HIP
等 finalized attachment，active weapon 则只读 finalized `WEAPON_R`，以 authored `PRIMARY_GRIP`
派生 weapon origin 和其他 weapon sockets；左臂 IK 只修改当前 actor 的 mutable instance，不回写共享
resource。

RFANIM walk 的 800ms authored 周期不直接复用 gameplay MOVE 的 400ms 回卷值。modular renderer
按 actor instance 累计跨回卷的展示时间，短暂 upper-body action 期间保留 lower-body 相位；因此动作
完整走完 800ms 后才循环。RFCHAR body、gear、socket 与 weapon 统一遵循契约的模型 `+Z` 前向，actor
yaw 不再附加 180 度修正。

出生点附近的 V2 action debug station 是 renderer-only fixture：它使用独立的
`rasterfall_model_instance`，不进入 `toy_game_actor`。按钮循环选择 `IDLE`、`WALK`、`RIFLE AIM`
和 `AIM + RECOIL`；最后一项按 lower locomotion → upper aim → additive recoil 的顺序组合，
并从同一 finalized pose 更新 AK socket、PRIMARY_GRIP、FOREGRIP 和 hand target。

RF Humanoid V2 的资产前向事实保存在 modular skeletal profile 中；body world rotation、
rigid attachments、character sockets 和 skeletal weapon 使用同一 profile basis，不在枪械
绘制函数内追加独立的 180 度修正。运行时可用 `--action-runtime-debug` 输出 actor、renderer
path 以及 lower/upper/additive action，确认 RF Humanoid 没有被 legacy anime path 抢占。

## Enemy Visual V2

普通敌人的可选 BLOCK_INFECTED / HUMANOID_INFECTED 家族由 renderer-only recipe 选择六份公开
RFM2；默认 AUTO 按 COMMON 70/20/10、FAST 40/40/20、HEAVY 30/40/30 混合。
Smoker / Charger / Tank 使用 `rasterfall_enemy_rig.h` / `render/rasterfall_enemy_rig.inc` 的
procedural rigid profile → truth adapter → pose → generic renderer；几何和计时不再混在专用绘制函数。
Charger 的命中边沿与 Tank 的 625ms 峰值只消费 gameplay；模型尺寸由 profile 拥有。
新增敌人的默认路线、固定关键帧、轮廓与 near/mid/far 验收见下方专题入口。
资源、串行 scratch pose 与逐槽位步幅均归 renderer，
不进入 gameplay 或 snapshot。新身体通过既有三角形入口保留受击、死亡旋转和渐隐；
入口、预算、远裁剪和验收见 [enemy-visuals.md](enemy-visuals.md)。

正式地图的 `MODEL_DISPLAY` 陈列台继续使用旧 style 1--5 的程序化敌人展示，并在右侧增加
style 6--14 的三组展示：COMMON、FAST、HEAVY 各自依次绘制 LEGACY、BLOCK_INFECTED、
HUMANOID_INFECTED。V2 两个家族由地图 draw record 触发同一感染模型 recipe，位置、姿态和
地面锚点属于 renderer；不会创建 enemy、碰撞体、AI 或网络状态。

## 渲染边界

### RenderFrame V1 与 Sky B2

Core retained command 缓存跨帧复用。`gpu_pre_post_partition_world()` 用当前命令数
分配替换缓冲区后，必须同时将 `retained_command_capacity` 更新为实际分配数量。
保留旧扩容容量会使后续增长帧绕过扩容并在 `memcpy` 时写出堆边界；该问题与暂停菜单
无关，静止场景命令数不增长时可能不触发。`rf_core_retained_span_logic_test_v1()`
覆盖分区后的缩小、增长、再次缩小及 stable partition 内容，属于 `--logic-test` 门禁。

Texture V1 的资源表在每帧 pack 时同时维护 descriptor、texel 和仅供 host 查找的 texture-view
数组。每个 textured command 只在该唯一纹理数组中查找 handle；首次出现时追加 descriptor 并复制
texel。该 host 指针数组不写入 Raster ABI，也不改变 GPU 资源格式。禁止为每条纹理命令从头回扫
此前全部 command：老地图面向高密度角色区时 retained stream 可超过四万条，这种回扫会把 pack
推过交互式 200ms watchdog，并以 frame presentation failure 结束运行。

`rf_render_frame_v1` 是 Core 持有的一帧有序提交描述，只保存 camera/extent 快照、固定层计数和
backend 审计信息，不拥有玩法状态、renderer command pool 或 Vulkan object。层顺序固定为
sky → world → transparent → effects → viewmodel → overlay。当前 vertical slice 已完成 sky：CPU backend
继续使用 `rasterfall_sky_draw()`，GPU backend 将同一方向、pitch 与固定颜色参数写入
`RF_GPU_RASTER_CMD_SKY_V1`，由 Raster dispatch 在任何 depth-tested world command 前生成 device-local
背景；不上传 CPU 天空，也不把 sky 伪装成 screen overlay。CPU reference、full-scan 与 tile-binned
shader 消费同一命令，sky 不写 depth。

B3 通过 `rf_core_render_frame_record_world_v1()` 对正常 world batch 按
`transparent || material_alpha != 255 || (textured && texture->has_transparency)` 分类。B2d-0..2 已使透明成为 Raster state：
source-over 命令 depth-test 但始终不写 depth，material/texture alpha 在 ABI payload 中显式编码，
CPU reference、full-scan 和 tile-binned consumer 均按 packed command order 执行，不自动深度排序。
GPU retained collector 将多次 WORLD flush 先收集为一个前缀，再一次稳定分成连续 opaque→transparent span；
`BEGIN_TRANSPARENT_V1` 是 ordering/audit barrier，层决定跨域顺序，state 决定像素语义。
B2d-3 的 retained span、marker、RGBA 分类与基础 Core 放行已完成；B2d-4a..4e 完成 producer 分流，B2d-5 已用完整 pre-post span fixture 收口 normal-frame local gate。
未支持的 material/texture/edge/overlay 仍整帧 CPU replay。
effects 的 direct-pixel 与 unsupported debt 仍会使整帧回退；opaque viewmodel command 已迁入 native GPU frame，并由 span marker
切换独立 depth/coverage 域。`rf_core_begin_screen_overlay()` 之后的 renderer-command debt
仍不能误称为 native GPU 覆盖。

GPU-8B2 的 interactables vertical slice 已将拾取物、按钮等 depth-tested world geometry
移入首个 world flush 之前；Core 现在对 normal world 与 interactables 执行一次完整分类、
pack 和 fallback 决策。透明基础语义已覆盖 flat 与 RGBA/material alpha；不支持的材质 feature
仍按完整 batch 回退，不允许半帧 GPU/半帧 CPU。

B4 submission contract 不再允许调用位置隐式决定层序。`rf_core_render_frame_enter_layer_v1()` 持有逐层
cursor，跳层或退回旧层的提交都会失败并增加 `invalid_layer_transitions`；frame audit 必须为
`cursor=overlay invalid_transitions=0`。normal frame 先完成 world/transparent barrier，再分别提交并 flush
effects、viewmodel，二者都是 Post V1 之前的 scene layer。只有这些 barrier 全部完成后才能调用
`rf_core_begin_screen_overlay()`；该入口同时切换返回的 surface 与 `renderer->surface`，使 HUD、Console、
Desktop、名字/提示和 effect overlay 共享同一 Core-owned color+coverage truth。禁止把 effects/viewmodel
的直接 framebuffer 部分归入 screen overlay 来规避 GPU consumer 缺口；unsupported material/texture
features、edge 与 overlay 仍标为 unsupported，viewmodel 则要求使用明确的 pre-post span consumer。

GPU-8B2 retained pre-post consumer 使 Core 在 world、effects 和 viewmodel 的各次 flush 中按层
保留原始 `toy_raster_cmd`，到 viewmodel barrier 后再做唯一整帧决策。当 effects/viewmodel
仍有 command 或 direct-pixel debt 时，Core 会禁止 native GPU present，并按原层批次顺序
重放全部保留命令到 CPU surface；不允许出现 world 已由 GPU 消费、post-world 层却只写入
不会 present 的 CPU surface 这种半帧状态。只有全部 pre-post 输入可无损表达时才进入
Raster V1/Post/native present。frame audit 的 `retained_pre_post` 和 `pre_post_cpu_fallback`
分别记录本帧保留命令数和整帧回退决策。这是消费与 fallback 契约，不代表
effects/viewmodel 已获得 GPU backend。B2c 的 VIEWMODEL span 在 pack 时插入通用
`BEGIN_VIEWMODEL_V1` marker，GPU/CPU consumer 以 retained layer range 切换独立 inverse-Z depth，
不读取或改写 world depth，并写独立 coverage；Post fog 在 coverage 像素跳过。后续 B2a 将审计拆为每层 command 和 direct pixels：
`pixel_count` 继续表示层的总绘制结果，`direct_pixel_count` 只表示绕过 command consumer 的
surface 写入。`pre_post_fallback_reason` 现在以显式 bit 区分
`UNSUPPORTED_MATERIAL`、`UNSUPPORTED_TEXTURE`、`UNSUPPORTED_EDGE`、
`UNSUPPORTED_OVERLAY`、`UNSUPPORTED_GENERIC_COMMAND`、effects/viewmodel direct pixels
以及 `CONSUMER_FAILURE`；支持的 transparent command 不设置 reason。已能无损表达的 effects/viewmodel/transparent command
不再因层名被禁止，且 effects facade 通过独立 stats 回传实际 direct producer
结果，不再把同次调用中的 triangle command 结果数整体记为 direct debt。任一真实
direct producer 或 unsupported command 仍使整帧回放。

GPU-8B2b 已把 billboard、普通 hit/fire/explosion particle 和屏幕线 ray 改为全亮、无雾且携带
投影 `inv_z` 的 opaque triangle commands。ray 保留近平面与 Liang-Barsky 屏幕裁剪，并把线宽沿
屏幕法线展开成 quad；轴向退化段使用同深度小矩形。逻辑 fixture 分别覆盖 particle、billboard、
本地 tracer 轴向段和越过右边界的通用 ray，断言预期 command 数且 `effects_direct_pixels=0`。
特殊死亡 fragment/dust 继续沿既有 triangle/alpha command 路径；B2d-4e 已将二者整个生命周期固定为有序透明 EFFECTS producer，即使 fragment 首帧有效 alpha=255 也显式 source-over/no-depth-write，且不产生 direct-pixel debt。

GPU-8B2d B2d-0..2 已完成 ordered CPU reference、flat source-over GPU consumer 与 RGBA/material
alpha consumer；固定 fixture 覆盖透明提交顺序、alpha 边界、透明不写 depth、fog 后 blend、RGBA
alpha0 与跨 tile。B2d-3 已完成 retained span/Core 基础放行、显式透明 marker、跨多 WORLD flush 的稳定布局与
RGBA 分类；B2d-4a/4b/4c 已按真实 RFM2 material alpha、RGBA texel alpha 与二者组合
迁移普通 flat/textured world producer，并保留 RGB + material alpha=255 的 opaque 快路径；组合有效 alpha 固定为 `texel_alpha * material_alpha / 255` 向下取整。RGBA command 在 CPU replay 与 GPU pack 前即具有 no-depth-write，真实 fixture 覆盖 alpha 0/64/128/255、RGB 对照、组合零边界、texture table、retained span 和 hand-off。B2d-4d 进一步由正常 `render_platform()` 与 air-gate `draw_box_alpha()` helper 固定 alpha 96/48、platform-before-gate 原始提交顺序、稳定几何以及 Raster V1 pack；B2d-4e 固定 enemy death fragment 的四面体 4-command、dust 的 camera-facing quad 2-command、fragment-before-dust 原序及满/衰减 alpha，并保证二者始终 source-over/no-depth-write、`effects_direct_pixels=0`；muzzle outer/lobe 与 enemy dissolve death fade 也已迁移。VIEWMODEL transparent 使用同一
source-over state、独立 depth domain，并在 alpha>0 时写 coverage 供 Post fog skip；alpha0 不写任何输出。
B2d-5 在 Core retained-span 门禁中同时放入 opaque/transparent WORLD、EFFECTS 和
VIEWMODEL，断言稳定 WORLD 分区、EFFECTS 原序、VIEWMODEL 独立 barrier、零 direct-pixel/
fallback reason、native-prepared 路径与完整 Raster V1 stream；既有 unsupported fixture 继续断言
任一不支持命令在提交前拒绝整帧，不允许部分 GPU 成功。这是 GPU-8B2 的 local-pass
收口；后续 Windows Intel native present、resize 和 timing 结果见 [GPU 当前状态](gpu-current-state.md)。
GPU-8B2 已按 opaque effects、direct producer、VIEWMODEL consumer/local muzzle、Transparent V1 的
顺序收口。Transparent V1 固定 source-over、material/texture alpha、depth test/no-depth-write
和原始顺序，不以 OIT 或重排作为首版前提。这里的 GPU-8B2 编号仅描述已实现的阶段，
不作为当前开发队列。

### GPU-9A Post-Raster Compute Pass V1

normal native frame 的当前所有权和顺序为：

```text
Core 收集/pack Raster V1 stream
  → backend 同一 primary command buffer dispatch Raster V1
  → device-local raster color + depth
  → compute-write → compute-read barrier
  → 可选 Post V1（raster color/depth → 独立 post_color）
  → compute-write → compute-read/write barrier
  → CPU color+coverage overlay upload / GPU source-over composite
  → compute-write → transfer-read barrier
  → presentation color buffer → swapchain image
  → 单次 queue submit/fence → present
```

Post disabled 时 presentation color 直接别名选择 raster color，不 dispatch、不复制；identity/fog
enabled 时选择同尺寸、device-local、replacement-first 随 raster resize 重建的 `post_color`。Raster V1
ABI 不变，且避免 Raster 与 Post 的原位读写 hazard。Raster→Post dependency 是
`COMPUTE_SHADER / SHADER_WRITE` → `COMPUTE_SHADER / SHADER_READ`。depth 是真实的 signed 32-bit
`inv_z = 1048576 / camera_z`，不是线性米制距离；Fog V0 在 far/near inverse-depth 阈值间做单调反向
插值，输出始终 canonical `0xffRRGGBB`。shader/pipeline 不可用时 post setter 失败并保持 bypass，不把
GPU service 或 Raster V1 标为失败。显式 diagnostic readback 可比较结果，normal native path仍为
color readback 0、CPU framebuffer copy 0。

GPU-7A 的捕获边界是 `toy_renderer_flush()` 消费和透明排序命令前的只读 observer。诊断不建立
第二套 traversal/camera/culling/lighting；它使用固定 Campaign near/mid camera 与 0/30 enemies，按
真实 `toy_raster_cmd` 分类，只复制可无损表达为 Raster V1 的 flat opaque command。world/viewmodel
多次 flush 的 selected command 保持原相对顺序并只加入一次 clear：

```sh
build/rasterfall --gpu-world-raster-test near 30 build/world.bin
build/rf-gpu-raster-diff-test --replay-raster-stream build/world.bin
```

前者录制、分类并用 GPU-4 packer 写 stream；后者用 GPU-6 CPU oracle 与 GPU-6.5 tile-binned GPU
比较同一 stream。真实 world replay 默认跳过成本不成比例的 full-scan。这是 partial world diagnostic，
不是完整 normal GPU frame 或 FPS。

GPU-7C/7D 复用同一边界，ownership 位于 Core：normal frontend 只遍历一次并生成唯一
command pool。Core 对 world flush 完整分类；flat、vertex-lit planar 与 Texture V1 opaque 可进入 GPU，
transparent、overlay、edge 或 other 任一出现则 CPU 消费原始完整 batch。Texture V1 使用每帧
pointer-identity dedupe、1-based handle、descriptor table 与 packed texels，不把 CPU pointer 写入 ABI。
eligible batch 经既有 pack、CPU tile binning 与共享 raster backend；readback 同时覆盖 color surface 与
renderer depth。normal 路径传入 GPU API 的 stride 单位为 32-bit 元素，`toy_surface.stride` 在 Core
边界从字节显式换算；surface 尺寸改变时先 resize raster resource。

`--gpu-normal-scene <near|mid> <0|30>` 复用 GPU-7A 固定 camera/enemy fixture，但不走 headless
capture：它仍执行正常 Core begin/render/world flush/oracle/overlay/present 主循环。当前 transparent
不属于 Texture V1，出现时整批 fallback 是显式覆盖边界，不计作 GPU 成功帧。oracle mismatch 会在
保存 replay artifact 后以完整 CPU color/depth 恢复 surface，避免在 GPU depth 上二次软件绘制。

Temporary Campus Kit的`--visual-capture campus-corner`支持`-near`、`-mid`、`-far`；
`campus-asset-<name>`观察单件。全部位于process-only dev-tests fixture，复用已有
static prop/quad，不改变正常world。组图及开放视觉壳限制见
[temporary-campus-kit-v0.md](temporary-campus-kit-v0.md)。

Profession Visual System V1 的 `--profession-lineup <model-dir> <output-dir>` 由 options →
main 早退 → `rasterfall_render_profession_lineup()` 执行；实现位于
`dev-tests/rasterfall_visual_capture.inc`，复用 `visual_acceptance_model_frame()` 和标准 AK。
六人从左到右为 Rifleman、Breacher、Recon、Medic、Engineer、Heavy；同一深度缓冲、同一
Lighting V1、无名字标签，沿相机水平轴摆放，保证 side 也不重叠。输出 2400×900 BMP：
front/three-quarter 的 near/mid/far，以及 side-mid；距离为 2200/4400/8800 RFU。
正面与 3/4 站位间距 440 RFU，side mid 为避免枪管遮住相邻背包，间距为 660 RFU；
同一方向的角色与间距不随距离变化，远景不会自动放大来伪装可读性。

RFCHAR 的 Character Lab、world strip 与 lineup 统一采用 835 milli-scale（2.080m
源 body 对应约 1.736m 展示身高）。装备不再影响身体缩放；RFCHAR 使用 canonical 原点，
不会被非对称背包的包围盒偏移。旧非 RFCHAR 模型仍使用原有高度适配。此校准仅属于诊断展示，
不修改正式 gameplay actor 渲染或 RFCHAR/RFM2 数据。

`src/rasterfall_render.c` 是世界渲染和角色渲染主体：投影/近裁剪、三角形提交、地面与地图图元、
拾取物、敌人、玩家/队友、骨骼角色、弹道粒子及模型诊断。公开入口在
`include/rasterfall_render.h`，共享状态由 `rasterfall_render_context` 绑定。

低模 AI 链路为 `rasterfall_render_ai_teammate()` → `render_ai_teammate()` 的 actor
遍历/可见性/模型路径选择 → `rasterfall_render_procedural_humanoid()` → 现有 pose、身体部件、
武器 helpers → primitive 提交。公开入口和 `rasterfall_procedural_humanoid_state` 位于
`include/rasterfall_render.h`，实现保留在 `src/rasterfall_render.c`，不增加编译单元。
场景层提供 actor 的 x/z、sy/cy、ground_y + airborne_y、当前 slot 武器、downed、动画 ID/时间；
AI 的 muzzle_flash 参数仍为 0。入口只读这些瞬时参数，不查 actor 数组、不裁剪整个人物、
不绘制姓名/血条，也不查询地面；调用者可传入指定 camera。

`rasterfall_character_profile()` 提供 body/leg/skin/hair 基础外观：body 用于躯干及上臂，
leg 用于腿，skin 用于头部和脸部，hair 用于脸部矩形；武器 helper 的前臂固定肤色保持原样。
负 character ID 的旧 class/body tint 由调用适配层覆盖 profile 副本的 body_color，
其余外观继续使用默认 profile。实际骨骼路径选择仍取决于 `anime_character_id` 与模型是否加载，
不在本入口解释 profile 的 model_path/actions。

入口采样现有 `rasterfall_actor_animation_sample()`，保留 reload 武器时长、前移/抬升、
腿摆动、身体俯仰、death/revive 翻倒和 downed 简化身体的既有行为与绘制顺序。
内部保存/恢复 primitive helpers 的 lift/roll 临时状态；仍依赖已绑定 render context 和串行
helpers，不承诺并发重入。`render_player_avatar()` 仅保留其他现有调用者的参数适配。
职业身份枚举 `rasterfall_profession_id` 位于 character identity 头文件，与基础 character ID 独立；
稳定职业身份保存在四个 Hurd profile 和一个 Maid profile 的 `profession_id` 中，不作为重复字段进入 actor 或网络结构。
actor 展示适配器从 `character_id` 解析 profile，并通过
`rasterfall_procedural_humanoid_state.profession_id` 携带一次程序化绘制的身份；普通 player、地图佣兵、
hired AI 和远端普通玩家均使用负值 `RASTERFALL_CHARACTER_NONE` 并解析为 NONE。`anime_character_id`
继续独立选择原有 Eula/Maid 骨骼资产路径；正式 Maid 旗卫同时携带 Maid `character_id`，不再由模型选择器隐含 profession。`rasterfall_profession_visual_profile()` 在 character
模块解析静态 presentation-only 配置：accent/gear 颜色、head、badge、waist_bag、backpack、vest。
NONE/无效 ID 返回 NULL，完全跳过装备绘制，基础身体和既有绘制顺序保持原样。

正式 Hurd 四人是 `anime_character_id == 0` 的普通程序化 actor；renderer 与其他低模 AI 一样只读
actor 的 gameplay state，但从其稳定 `character_id` 解析四个 profession profile。即使工作区存在私有
骨骼角色资产，也不会把 Hurd actor 错切到 Eula/Maid 模型分支。Visual CLI 的 `hurd-squad` 仍仅是
离屏 fixture，不是正式 Hurd actor 的状态源。

renderer 内 `render_profession_visual()` 组合相同 actor-local box primitive：Gunsmith 橙色工具侧包、
露出扳手和护目镜；Logistics 卡其大背包、侧袋、胸袋和帽檐；Medic 灰白医疗箱、绿色十字和头带；
Guard 宽厚深绿背心、肩部护片、盾徽和简化头盔带。基础身体配色仍由 character profile 提供。
附件使用现有 lift/roll，躯干附件使用 body_pitch，头部使用原有 head lift；downed 简化代理不画
直立装备，death/revive 使用既有整体翻倒变换。未增加动画状态或附件资产系统。
portrait 可复用该入口和静态 profile，
仍需自行提供 camera 和展示状态。

RF Humanoid Headgear / Face Coverage V1 属于 asset-side presentation 扩展：变体模型沿用
同一 RFCHAR skeleton 和 stable `HEAD` attachment，头盔、goggles、respirator 等几何以
`RF_HEAD` 刚性权重随角色 pose 求值。renderer 不解释 headgear 名称，也不增加 actor 或网络
字段；Character Acceptance / world capture 只通过替换 `--character-world-model` 或输入模型
路径观察不同变体。当前阶段的完整 RFCHAR variant 是验证 carrier，未来若加入通用 rigid HEAD
assembly，仍应保持 renderer 只消费稳定 attachment transform。

静态环境组件通过 `rasterfall_render_static_prop()` 提交 world-space RMESH。地图 parser 将
注册表 asset name/id 转为轻量 `toy_map.props` 实例，renderer 遍历该数组；入口消费 RFU
`x/y/z`、绕世界 Y 轴的 yaw 和实例缩放，按
“RMESH local → `512/232` profile scale → instance scale → yaw → world translation”求值。
注册表模型缓存按 asset id 懒加载一次，多个实例共享同一 `rasterfall_model_asset`；地图实例在
空地 `z=-17000` 一带按 1500 RFU 间距展示十件工业组件，用于检查底部 pivot、尺寸、yaw、材质和深度；
相邻实例的 profile 碰撞 AABB 保持正间隙，不以视觉网格孔洞替代玩法碰撞。
地图实例使用 `asset x z yaw scale` 五个字段，`y` 固定为地面锚点 `-900`，`scale=1000`
表示资产原始设计尺寸。visual mesh 是 presentation-only；碰撞由地图 parser 从 profile 独立生成 gameplay box。

Generic Rigid Attachment V1 使用独立的 `rasterfall_render_rigid_resource()` full-transform submission，
不走 floor alignment。矩阵为 row-major，求值链为
`actor/world × finalized socket × mount correction × authored rigid local`；translation、完整 3×3
rotation 和 uniform scale 同时作用于顶点，rotation 同时作用于法线。RMESH header 的
`position_scale` 在 submission 边界把 asset meters 换成 512 RFU/m，附件 bounds 不参与角色或附件
scale。`rasterfall_render_rigid_attachment()` 只查询 instance finalized socket、组合矩阵并提交共享
resource，不修改 host pose，也不把 actor/world 状态写入 instance。

`--rigid-attachment-acceptance <model-dir> <output-dir>` 在同一深度缓冲绘制共享一份 Humanoid
resource 的 bind/turned 两个 instance，并让二者共享同一份 helmet/backpack resource；右侧额外旋转
chest 与 head，固定输出 socket 数值和 `rigid-attachment-acceptance.bmp`，可用重复 capture 做逐字节回归。

`--squad-acceptance` 额外以 Jesus WALK+FIRE、Engineer WALK、Heavy WALK+FIRE 压测组合后的 body、
backpack/hip/chest gear、socket attachment 与八 instance 隔离；三视角仍是固定输入确定性 BMP。

程序化敌人模型采用统一的 `enemy_body_part` 描述：每个条目对应一个基本身体组件，类型包括局部朝向盒、世界盒、圆柱、椭球和面部矩形，尺寸与局部偏移仍使用现有 RFU 数值。通用解释器按描述顺序提交几何，因此可以在不改变玩法状态的前提下继续接入参数化配置。敌人位置以 `toy_game_enemy.x/z` 为水平锚点，垂直基准由地面 `Y=-900`、`ground_y` 和 `airborne_y` 组成；Charger 的水平放大和普通敌人的既有缩放语义保留在解释器中。Tank 的挥臂依赖蓄力时间，是动态组件，继续由专用函数求值后插入静态组件之间，以保持原有遮挡和绘制顺序。

敌人死亡样式由 `rasterfall_effects` 按槽位持有，仅在首次观察到 `active == 2` 时选择：约 10%
保留原有整体压扁，其他情况在 1000ms `dying_ms` 窗口内沿最后一击方向加入确定性的随机侧偏，
以抛物线抬升并绕身体中心旋转；最后 380ms 通过透明三角形提交让全部 `enemy_body_part` 同步渐隐，
不再按部件顺序逐个消失。同时固定容量 emitter 跟随飞起的身体持续发射两层 raster fragments：
较大的主体碎片使用随时间旋转的不规则四面体，并按带重力的抛物线形成高速定向冲流；较小尘屑
使用透明 camera-facing 几何和更宽的侧向扩散；
两层生命周期分别为 1350ms 和 1550ms，因此可在 enemy slot 清空后继续消散。网络侧缺失最后一击
事件时才回退敌人朝向。样式、碎片和消隐阈值均不进入 snapshot。受击方向偏移也读取同一 effect
event payload，位移幅度按 damage 限幅缩放。

`src/render/rasterfall_render_frontend.c` 是渲染器前端适配，管理默认纹理、覆盖配置和 worker
绑定；底层光栅器在仓库公共的 `lib/graphics/renderer.c` / `include/toy_renderer.h`。

## Static World Lighting V2 与 RMESH 形体光照

World/environment lighting 的 owner 为 `include/rasterfall_world_light.h` 与
`src/rasterfall_world_light.c`。`rasterfall_render_context.world_lighting` 持有每个 world 的缓存；
`rasterfall_render_bake_lightmap()` 在 world load/switch 后只生成 V2 field，失效独立诊断 V1 cache。
`rasterfall_world_light_bake_v2()` 只读 Runtime Map collision/surface；64×48 sample 使用地面相对
高度沿 canonical form-light 方向测试三维 AABB。`rasterfall_world_light_at(lighting,x,y,z)` 对
三个分量分别做 Q8 bilinear，并 clamp bounds；它是一层 ground-following XZ field，不是 3D volume。
`rasterfall_world_light_v2_q8()` 组合 environment × (192 + visibility×64/256)/256 × contact/256；
开放区 environment 为 256，旧 gradient/proximity/east boost 不进入 V2。建筑平面细分并复用
现有 textured rasterizer 的 flat fallback/vertex light 插值，近裁剪同时插值光照。
正常 partitioned ground/floor paint 消费 V2 并保留原无雾策略；map wall/texture/box/ramp/platform
及独立 boundary wall visual boxes 消费 V2，原 form、面调色和 fog 数学不变。
Static World Lighting V2 is the sole normal-runtime world-light source.
正常 static RMESH、players/AI/RFCHAR、普通/特殊感染体、世界武器和投掷物、viewmodel
均消费 V2；默认 `world_brightness_at()` 也只查询 V2，覆盖 sign/交互物等辅助 world geometry。
只有显式 diagnostic scope 可以查询 V1。V1 的 32×24 cache 不在正常 render context 中，
独立诊断 owner 按需 bake；固定诊断例外与架构回归见 [Phase C3](static-world-lighting-phase-c3.md)。
最终 field 世界尺度、bake/contact/interpolation、性能与冻结边界见 [Phase D](static-world-lighting-phase-d.md)。
组合顺序为 `world light × form lighting × material policy → final color → fog`；material policy
在 scene×form 后应用材质下限，原地面无雾与专用 VFX 策略保留。

正式 RMESH 路径在 `render_gallery_model_range()` 统一应用低成本 ambient + directional
form-lighting，覆盖 RFCHAR/skeletal body、static prop 和通过同一模型入口绘制的第三人称 weapon。
变形或实例 yaw 后的三个顶点法线先求平均，每个提交三角形只计算一次整数点积；纯色材质在提交前
调制 base color，纹理材质把同一固定亮度交给已有 textured raster command。没有新增逐像素法线
计算，也不改变材质色相、饱和度或 gameplay 状态。

当前 Q8.8/Q15 参数集中在 `rasterfall_render.c`：世界主光方向为归一化
`(-0.408, 0.816, -0.408)`（表面指向高处西北主光），ambient 为 `136/256`，directional 为
`120/256`，因此 `form = max(136, 136 + max(dot(N,L),0) * 120) / 256`，最大为 1.0。
RFCHAR 或 skeletal 模型使用 `144/256` 的基础 presentation visibility floor；其他 RMESH 使用
`136/256`。Character Material Lighting Policy V1 在同一个 `character_render_policy()` 中复用
RFM2 material role，并在 form 与 scene/lightmap 相乘后对角色材质作最终亮度保护：FACE 与 SKIN
为 `224/256`，EYES 为 `240/256`，HAIR 为 `176/256`；上限当前统一为 `256/256`，rim 字段预留为
零且不执行额外 pass。CLOTHING、EQUIPMENT、无 role 材质及全部非角色 RMESH 保持 Lighting V1
原公式。纯色与纹理 submission 都应用相同策略，雾仍在原有阶段处理。当前没有 stylized
quantization、point light、shadow、probe 或动态局部光。

`rasterfall_render_set_model_lighting()` 仅供 presentation/诊断消融。`--model-performance` 的
`full` 与 `lighting_off` 保留相同材质功能，只切换上述 form-lighting，能够直接比较成本。
Character Acceptance 额外输出 `lighting-ab/{bind,rifle-idle,rifle-aim}/{front,side,back,three-quarter}.bmp`，
每张图左侧为 OFF、右侧为 V1；`--visual-capture lighting-props` 以相同方式固定输出 crate、
workbench、vent unit 和 industrial pillar。两条入口都不读取时钟，适合用 `cmp` 做确定性检查。
Character Acceptance 还输出 `lighting-policy/{normal-light,back-light,dark-environment}.bmp`：前两张
从固定 directional key 的正反方向观察，dark 使用固定 `96/256` scene brightness；该 override
只存在于进程内 capture fixture，不进入 runtime 参数、地图、gameplay 或网络状态。

其他视觉模块：

- `lib/graphics/fb_font.c`：加载 `assets/fonts/gb2312-16.rfh`，把 UTF-8 字符串映射到恢复的旧版
  VGA 半宽 ASCII 或全宽 GB2312 16×16 点阵；启动菜单中的“光栅坠落”是游戏内中文显示的常驻验证入口。该资产也供
  地图排布导出器读取，运行时不依赖 FreeType、系统 CJK 字体或宿主 libc 编码转换。

- `rasterfall_hud.c`：玩家、网络、波次、商店 HUD，交互提示和 BMP/帧导出。
- `rasterfall_viewmodel.c`：第一人称手臂、武器模型、后坐/摆动和枪口位置。
- `rasterfall_effects.c`：消费 `rasterfall_effect_event`，并从投掷物/燃烧区域展示状态同步枪口闪光、弹道、命中粒子、炸弹闪烁、Molotov 火焰和局部镜头晃动等短生命周期表现状态。
  事件消费现在还会登记到固定容量的 `rasterfall_effect_instance` runtime 池；instance 将底层
  组件类型（particle/ray/billboard/overlay/emitter/material/camera_shake）与语义 kind 分离。tracer、命中火花、分层 muzzle flash 和 Molotov 火焰已迁移到统一
  `RAY`/`PARTICLE`/`BILLBOARD` 组件；`ENTITY_HIT` 生成命中粒子但不再额外生成整条 hit ray，炸弹 fuse flash 使用 billboard；玩家伤害闪屏使用 `OVERLAY`，敌人受击颜色使用 `MATERIAL` feedback，交互高亮已登记为短生命周期 `INTERACTION_HIGHLIGHT` billboard 并驱动现有高亮绘制，屏幕空间效果通过 `render_effect_overlay()` 和
  `rasterfall_render_overlays()` 提供统一入口，因此本阶段不改变已有效果画面。LOCAL_VIEW 的
  muzzle core 由 `rasterfall_viewmodel_render()` 以 viewmodel projection/depth 作为 opaque child
  提交；remote/AI core 仍由 world EFFECTS 消费。local outer/lobe 复用 VIEWMODEL projection、
  独立 depth 与 coverage，remote/AI outer/lobe 保留在 EFFECTS，并以真实 material alpha 的
  `transparent` command 进入 Transparent V1。Charger/Tank 命中
  actor 后，effects 为每个目标保留一条最多 16 点、覆盖最近约 `RASTERFALL_KNOCKBACK_TRAJECTORY_HISTORY_MS` 的真实 world-space airborne 位置历史；renderer 将相邻点组成白色、camera-facing、深度测试 ribbon，落地后按 `RASTERFALL_KNOCKBACK_TRAIL_FADE_MS` 快速消失。
  轨迹是 presentation-only，不替代 actor 的实时位置，也不重建完整抛物线。`CAMERA_SHAKE`
  组件不修改权威摄像机，只在渲染阶段复制出的 `render_camera` 上叠加视空间平移、偏航和俯仰扰动；当前仅本地 `WEAPON_FIRE` 事件生成该组件，AI/远端开火事件通过 `LOCAL_VIEW` 标志隔离。多个组件先按轴叠加，再按每轴最大值限幅，并用短时插值追踪目标值。`EXPLOSION`
  现在由固定生命周期的 emitter 生成 16 个通用 `EXPLOSION_PARTICLE` 子实例；事件类型统一定义在
  `include/rasterfall_effect_event.h`，该模块不反写 gameplay。
- `rasterfall_sky.c`：天空背景。
- `rasterfall_perf.c`：阶段计时、场景统计和性能输出。
- `src/dev-tests/*.inc`：角色基准和蒙皮跟踪，直接包含进 render 编译单元。

## Windows Intel GPU 验收状态

strict native smoke 与 Fog/Post smoke 已各通过 120 帧，正式地图 320 帧零回退运行和窗口拉伸已确认。最近固定视角的命令与耗时快照见 [GPU 当前状态](gpu-current-state.md)；旧冻结矩阵不再作为当前工作队列。

## 一帧的数据流

GPU-8B1/B4 后的正常帧 layering 为：

```text
Core begin: software toy_surface + renderer clear
  ↓
world command stream（scene / flags / enemies / actors / world labels）
  ↓ world/transparent ordering barrier
GPU Raster V1 eligible batch
  ├─ software-present：GPU color+depth readback → toy_surface/depth
  └─ native path：world stream/resource 冻结，暂不 present
  ↓
interactables
  ↓ effects 独立 submission + flush（pre-post）
viewmodel
  ↓ viewmodel 独立 submission + flush（pre-post）
Post V1 semantic boundary
  ↓
Core begin screen overlay，同时切换 renderer.surface
  ↓ CPU direct writes：prompt / name/status / crosshair / HUD / pause / game-over / scoreboard /
                  input debug / console / GUI desktop
  ├─ CPU renderer：原 toy_surface
  └─ native GPU：独立 XRGB8888 color + 8-bit coverage overlay
       ↓ full upload + compute source-over 到 device-local world color
  ↓
Core end：overlay final flush → software present，或 overlay composite → swapchain native present
```

GPU-8B1 只恢复 post 之后的 screen-space 层。interactables、world effects、viewmodel 均明确位于 post
之前；opaque viewmodel command 由 GPU-8B2c consumer 消费，LOCAL_VIEW muzzle core 与 outer/lobe
复用该层的 projection/depth/coverage，remote/AI muzzle 仍为 world EFFECTS；outer/lobe 已通过
Transparent V1 的真实 material alpha 和 no-depth-write policy 表达，不能上传为 overlay。B4 冻结的是
submission/target/order 契约；GPU-8B2 的后续功能边界见上文历史说明。

主循环更新 session/net/effects 后，展示层从 `actors[TOY_GAME_PLAYER_ACTOR_INDEX]` 和其他 actor
读取玩家状态，再设置 `rasterfall_render_context`，调用场景及实体公开入口；客户端远端玩家的
HP、武器、downed、动画和统计也从对应 actor 读取。网络连接状态来自 `clients[]`，远端位置/朝向
插值来自 derived presentation cache，不作为远端 gameplay 展示源；不从 `toy_game` 顶层玩家字段
取 HUD、第一人称武器或受击效果数据。
底层 renderer 收集/光栅化几何；Core Host 负责 begin、分层 flush、最终 flush 和 present；随后绘制 HUD、菜单和调试叠层。客户端角色展示可能使用
网络插值状态，不应误读为权威 `toy_game` 状态。

Renderer frame ownership：Core 拥有 renderer/window/surface 的创建、初始化、生命周期和销毁，
并通过 `rf_core_begin_frame()`、`rf_core_flush()`、`rf_core_end_frame()` 管理一帧。Game runtime
只更新 camera、准备 presentation state 并提交 draw commands；Rasterfall renderer 只负责
rasterization、commands 和 render cache/state。renderer 内的 `rasterfall_render_bind()` 全局绑定
是现有串行 presentation context：它把 session/effects/net/纹理/lightmap 提供给旧的绘制 helper；
并行模型录制则优先使用 `toy_renderer.recording_context` 对应的 frontend state。该绑定不拥有
window、surface、present 或 Core 资源生命周期。

战斗事件链路为：规则结果/网络展示适配器 → `rasterfall_effect_event` →
`rasterfall_effects_consume()` → runtime instance pool。tracer、命中火花、
muzzle flash 和 Molotov 火焰的渲染已经直接消费 runtime `RAY`/`PARTICLE`/`BILLBOARD` instance；runtime instance 统一拥有组件类型、
语义 kind、位置、方向、速度、生命周期/年龄、尺寸和 alpha 等基础状态。更新阶段统一按固定 16ms
步进推进粒子运动和寿命；
射击同步器只搬运规则层射线与枪口坐标；
伤害、命中规则和网络快照不读取或写入这些视觉状态。镜头晃动实例在固定步中先按轴叠加并限幅，
再以 `RASTERFALL_CAMERA_SHAKE_SMOOTHING` 对聚合目标做短时插值；快速连射因此会累积到上限，
停火后平滑回零。

当前镜头后座平衡参考（玩家射速倍率 200%，实际间隔为基础 cooldown 的一半）如下：

| 武器 | 实际间隔 | 后座衰减 | 垂直单发振幅 | 理论连续峰值 | 垂直上限 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 手枪 | 100 ms | 110 ms | 8 | 约 9 | 18 |
| SMG | 50 ms | 150 ms | 8 | 约 16 | 20 |
| AK | 90 ms | 240 ms | 18 | 约 32（触顶） | 32 |
| 霰弹枪 | 400 ms | 170 ms | 16 | 约 16（单发） | 28 |
| AWP | 600 ms | 110 ms | 5 | 约 5（单发） | 10 |

理论连续峰值按“同向叠加、线性衰减”估算，实际画面还会经过短时插值和确定性噪声，因此会略低或有小幅波动。

爆炸由炸弹命中/结束位置产生 `RASTERFALL_EFFECT_EVENT_EXPLOSION`，在
`rasterfall_effects_consume()` 中登记一个固定容量 emitter，并由 emitter 按间隔把子组件写入统一
instance pool；当前爆炸配置生成冲击波 ray、短时 billboard 和固定数量的粒子子 instance，由
  `rasterfall_render_effects()` 的通用 world primitive 分支绘制。该接入不修改炸弹伤害和网络协议；联机事件仍应由展示适配器构造已有 event。当前 Molotov 火焰已经通过
`rasterfall_effects_sync_fire_zones()` 将每个燃烧区域同步为固定容量 emitter；emitter 按固定间隔批量生成
  `FIRE` 语义的 `PARTICLE` instance，并由通用粒子绘制入口消费。emitter 的子组件类型、生成间隔、
  数量上限、散布和 placement pattern 都是固定容量 runtime 描述；FIRE 与 EXPLOSION 都从统一
  preset table 复制 emitter 标量参数及 child descriptor 列表，事件只负责填充位置等动态字段，主循环现在通过
  `rasterfall_render_effects()` 统一提交 ray/billboard/particle，overlay 仍在屏幕空间阶段单独提交，
  以保证 HUD 和第一人称视图模型的层级顺序。
  受击 `DAMAGE_FLASH` overlay 以低透明度混合四角短 L 形红边，并在准星外围绘制快速消失的
  红色箭头。箭头以最近存活敌人为展示层伤害来源估计，并相对当前相机朝向归一到前、后、左、右
  及四个对角方向；该估计不进入 `toy_game` 或网络快照。
  镜头晃动不进入世界 primitive 绘制；`rasterfall_effects_apply_camera_shake()` 在世界渲染前对当前
  `render_camera` 做确定性衰减采样。受击摇晃使用独立的可配置 preset：默认随机左右偏航约 15°，
  先快速到峰值、短暂保持，再在 500ms 内连续衰减；重置时清除未完成的受击方向。最短接受间隔内
  的重复伤害不会生成新的摇晃，参数位于 `include/rasterfall_effects.h`。
  统一 instance 和 emitter 池均为固定容量环形池，满载时按写指针覆盖最旧槽位；该策略已由逻辑测试覆盖。后续可在不改变火焰语义的前提下替换粒子渲染细节。

tracer RAY instance 保留事件提供的枪口起点和命中/射程终点，但绘制时按 lifetime/age 在两点
之间截取短段并推进到终点，不再显示整条弹道。颜色在整条可见短段内保持统一，线宽、尾段比例和 68--86ms 的寿命
由 `toy_game_weapon_info` 的 presentation-only descriptor 提供；普通武器统一白色，AK/AWP
采用黄橙色。第一人称本地玩家 tracer 按相机空间深度抑制枪口近处亮度，使用平滑插值从约中
距离开始逐渐显现；AI/远端玩家也使用短线段快速移动，但采用更短的可见段和独立距离参数。
AI/远端 tracer 使用世界空间小方柱，避免沿射线方向观察时固定屏幕线宽盖过透视长度而显示为横线。
instance pool、事件和深度测试 flags 不变。

## Visual CLI V1：固定场景观察

Architectural V1 使用 `--visual-capture arch-family|arch-alley|arch-hall --visual-output <bmp>`，
两个原型还有 `-inside`、`-far`、`-reverse` 视角。`tools/architecture_round.py --capture` 生成组图；
`architecture_capture()` 只拥有隔离 fixture，调用正常 prop、modular Rifleman 和 enemy renderer。
场景日志区分环境与总提交；墙地研究使用便宜 flat quad。
`arch-asset-<name>` 为低矮件提供俯斜 metric 单件视图；通过实际 prop registry 启用建筑闭合网格剔除。
冻结范围及正式集成边界见 [Architectural Environment V1](architectural-environment-v1.md)。

环境地图验收使用 `build/rasterfall --environment-capture tmp/environment-review --textures`，
再用 `python3 tools/environment_sheet.py tmp/environment-review` 查看十二视角组图和 PNG。
原始 BMP 保留。入口显式加载正式 Campaign 并复用正常 scene、actor、flag 和深度渲染，
使用固定 seed 且不进行 simulation tick；覆盖基地、北区、东西设施、东西路线、Hurd、南侧、坡道、出生室、南侧动力场和北侧动力端站。
角色测试带 capture 的行为不变。实景设施组合与碰撞边界见 industrial-props.md / map-format.md。

从仓库根目录运行：

```sh
make rasterfall
build/rasterfall --visual-capture procedural-humanoid --visual-output /tmp/rf-humanoid.bmp
build/rasterfall --visual-capture lighting-props --visual-output /tmp/rf-lighting-props.bmp
build/rasterfall --character-acceptance rasterfall/private-assets/models/rf_humanoid_acceptance.rmesh /tmp/rf-humanoid-v11
build/rasterfall --squad-acceptance rasterfall/private-assets/models /tmp/rf-squad-acceptance
```

输出为 24-bit BMP（单人 800×800，小队 1600×800），路径由调用者指定，父目录须已存在；成功后打印最终路径并退出。
已有文件会覆盖。可连续 capture 后使用 `cmp` 检查字节一致性，再用图片查看工具观察。
`--character-acceptance` 是独立的正式角色验收场景，依赖指定私有角色 RMESH 和现有标准 AK，
输出固定三姿态四视角及 near/mid/far A/B；RFCHAR 正面为 canonical +Z。该入口保留旧的
CHEST `visual_rf_calibration()` 枪架和双手 `rifle_solve_hands()`，用于 legacy carrier/校准诊断，
不代表正式 modular teammate 的持枪来源。正式 modular path 从 finalized `WEAPON_R` 对齐
authored `PRIMARY_GRIP`，并在绘制前用同一枪的 `FOREGRIP` 解算左上臂/前臂；`visual_rf_check_grips()` 仍只检查 legacy acceptance。
普通 Visual CLI 仍不依赖窗口、音频、地图或 gameplay step。
该入口加载一个 `rasterfall_model_resource`，全部 pose、握持 IK、CPU skinning 和 socket 查询来自
`rasterfall_model_instance`；另输出 `two-instance-isolation.bmp`，在同一 depth buffer 中以共享 resource
绘制左侧 bind 与右侧 aim 两个独立 instance，作为 deterministic ownership 观察门。

真实地图验收使用：

```sh
build/rasterfall --character-world-capture /tmp/rf-world-v11
# 对照另一套 RFCHAR body，仍走同一个地图、灯光、深度和 Character Test Strip：
build/rasterfall --character-world-capture /tmp/rf-world-v2 \
  --character-world-model rasterfall/private-assets/models/rf_humanoid_v2.rmesh
```

该入口先加载正式地图并 reset session，再通过正常 `rasterfall_render_scene()` 输出
`near.bmp`、`mid.bmp`、`far.bmp`，并输出 `{near,mid,far}-{old,idle,aim,motion}.bmp`。
固定镜头取相对目标 (0.6d,0,0.8d) 的斜正面位置（d=2000/4000/8000 RFU），
避免原正前方工业 prop 与负 Z 边界墙挡住距离验收；场景深度和几何均照常绘制。
Character Test Strip 位于 `rasterfall.map` 的
`z=-20000` 展示带：旧 procedural AK，以及由 `render_modular_preview_frame()` 绘制的 RF
rifle idle、RF rifle aim、RF locomotion-like pose；后者与战斗区共享 RFANIM 分层组合、V2
`WEAPON_R + PRIMARY_GRIP` 武器呈现和左手动作轨道。它们是 renderer presentation-only
entities，不进入 actor、碰撞、AI 或网络状态。未提供
`--character-world-model` 时使用 `rf_humanoid_v2.rmesh`；提供时只替换 strip 的
skeletal body，不改变 camera、AK、地图或 world render path。

角色模型观察以组图为默认工作方式，以便一次比较姿态、角度和距离。V2 角色使用
`python3 tools/character_lab_sheet.py` 汇总 bind/rifle-idle/rifle-aim 的四视角；真实场景
使用 `python3 tools/character_world_sheet.py` 汇总 near/mid/far 与 old/idle/aim/motion。
组图脚本只是对当前 capture CLI 输出的离线拼接层，不改变渲染路径；需要像素级诊断时再打开
其保留的单张 BMP 原始文件。

数据流：options → main 诊断早退 → 命名场景检查/固定 setup →
`rasterfall_render_procedural_humanoid()` → 普通 primitive 与武器 helper →
`toy_renderer_flush()` → `rasterfall_hud_dump_bmp()`。setup 与进程级 capture 实现在
`src/dev-tests/rasterfall_visual_capture.inc`，由 render 编译单元包含，人体代码没有副本。

场景 `procedural-humanoid` 使用 Akari 基础 profile、手枪、idle 0ms、未倒地，
actor 展示锚点 x/z/lift 均为 0，朝向 sy=512/cy=-887；camera 位于 (x=0,y=-350,z=-1900)，
yaw sy=0/cy=1024、pitch sy=0/cy=1024。固定纯色背景与光照，串行光栅化并关闭交互 watchdog。
fixture 仅预载公开手枪到既有模型缓存，避免 gallery 扫描；该入口只供新进程诊断后立即退出，
不是运行中切换场景的 API。

```sh
build/rasterfall --visual-capture hurd-squad --visual-output /tmp/rf-hurd-squad.bmp
```

`hurd-squad` 从左到右固定 Gunsmith、Logistics、Medic、Guard，均使用 Akari 基础身体与 idle 0ms，
前三人显示 Pistol，Guard 显示 SMG（仅 fixture 的 weapon 展示字段）。camera z=-2600，其余相机参数
及角色朝向沿用单人场景；角色 x 为 -1320、-440、440、1320，z/lift=0。斜向正面构图同时展示胸口、
头部、武器和侧后附件。固定背景/光照，串行绘制，预载公开 Pistol/SMG，不初始化 gameplay。
职业装备与普通人物共用 `rasterfall_render_procedural_humanoid()`，没有人物绘制副本。
接口不提供 portrait、任意相机控制、回放或图片基线管理。

`--squad-acceptance <model-dir> <output-dir>` 是正式 RF roster 的固定离屏验收：同时构造两套
四人 roster，共八个 modular actor，输出 `front.bmp`、`three-quarter.bmp`、`side.bmp`。
三视角均为 2400×900、固定相机和固定横向间距；body 只加载一次，gear 按资源 ID 共享，instance
按 actor index 独立。该入口同时检查 palette isolation、RFCHAR attachment transform regression
和 instance resource ownership，重复运行可用 `cmp` 做字节级 deterministic capture 回归。

## 常见任务落点

- 世界物体缺失或遮挡错误：`render_scene()`、对应 `render_*`，再查近裁剪和 depth 路径。
- 角色模型/LOD/并行渲染：character loading、`render_characters_parallel()`、骨骼角色入口。
- 第一人称枪械位置或枪口：`rasterfall_viewmodel.c`；第三人称持枪在 render/model/calibration。
- UI、记分板、伤害闪屏：`rasterfall_hud.c` 或 `rasterfall.c` 中独立 overlay。
- 光照、纹理、材质：lightmap 和 textured triangle 路径；资产解码在 `lib/assets.c`。
- 性能回归：先用 `rasterfall_perf` 的分阶段数据区分玩法、建模、提交和 raster，再改实现。

模型和动画的求值边界见 [assets-animation.md](assets-animation.md)。改可见结果时保留确定性截图/像素
测试的价值；改并行路径时还要比较单 worker 和多 worker 的画面与统计。

## Continuous Wall / Floor

`boundary_wall` 是 prop registry 的代码生成组件，长度来自 V1 object `attr.length`，
经 runtime object → toy_map_prop → prop instance 传递；`render_boundary_wall()` 使用
Map component 的闭合分带几何并在提交前剔除不可见面。碰撞仍由 Map Runtime 拥有，
renderer 不修改玩法。墙脚、主体、压顶不叠共面大板；扶壁与 collision contract 共用位置。
地面 `draw_partitioned_floor()` 改为 2048 RFU 大板和低对比接缝，边缘裁到 world bounds，
继续与区域 paint 在同一平面分区；WHU authored-ground 保留颜色，不添加接缝。
正常 world frontend 先以地面大板的保守视锥检查过滤完全不可见的大板，再做区域分割和颜色查找；
`boundary_wall` 的组件盒在生成可见面前使用相同检查。穿过近裁剪面的盒仍交给逐三角形裁剪。
普通 static RMESH 已在 `render_gallery_model_range()` 的顶点准备前执行模型 AABB 视锥检查。
地图 wall、texture、box、ramp、platform 在 render record 入口做整块三维包围盒检查，
避免屏外组件进入逐面提交和 V2 平面细分；诊断 MODEL、文字及标牌保留各自路径。
未新增纹理或 floor mesh。观察入口仍为 `--environment-capture` / environment_sheet.py。

## Static RMESH 的 V2 环境光

正常地图的工业设备、facility/power-yard 及 `env_arch_*` 都经 `render_static_props()` →
`rasterfall_render_static_prop()` → `render_gallery_model_range()`。地图遍历按实例世界原点
（`x, -900 + map_prop.y, z`）查询一次 `rasterfall_world_light_at()`，将 V2 Q8 因子临时放入
已有 `active_scene_light_override_q8`，提交后恢复。RMESH 不开启 planar V2 scope，
不在三角形/顶点热循环重复查询。boundary wall 保留 Phase B 的独立 procedural 路径。

环境因子与原 normal/form 因子相乘，纹理提交直接组合 Q8；无 role 的 flat 材质保留
原 form 调色再乘 scene 的整数舍入。material policy 与 fog 仍属于已有提交层；static prop
保留原 gallery 无雾策略。gallery 与独立诊断保留专用策略；actors、RFCHAR 和 viewmodel 已在 C2 接入。
设施由多个实例构件组成，第一版不引入大型 RMESH 细采样；证据和边界见
[Phase C1](static-world-lighting-phase-c1.md)。`--logic-test` 包含实际模型命令的 scene × form 回归。


## 敌人波次性能诊断

Eula 固定动画签收使用 `--eula-animation-acceptance <model-dir> <output-dir>`，输出 Full、LOD1、
compact LOD2、Hybrid 的 bind/idle、三个 walk 采样、四个 head/neck deformation pose 和 rifle
idle/aim。head/neck 是 presentation-only 形变检查，不是 gameplay action。Python 组图入口为
`tools/eula_animation_acceptance_sheet.py`，原始 BMP 全部保留。

统一角色微基准使用 `--character-performance <model> [warmup] [frames] [repeats] [workers]` 或
`--character-performance-suite ...`。suite 对 optional private assets 缺失打印 SKIP；每个实例共享
resource、持有独立 instance pose。输出规模、pose hierarchy、skinning、vertex cache、model CPU
submission、raster wall 和 total wall 的 mean/median。`submit` 是模型阶段 CPU 累计口径，不能与
并行 wall time直接相减。历史 `--model-performance` 继续负责材质/光栅消融，`--actor-performance`
继续负责固定五角色并发路径，`--render-performance` 继续回答 Campaign world 一帧成本。

`build/rasterfall --render-performance 12 --textures` 使用固定 Campaign 世界与内容，
在 1280×720 离屏表面比较近／中距离的 0、10、30、60 个普通敌人。保留地图 actor、
static props 和 Campaign fixture；不运行真实波次。每项预热两帧，随后输出指定帧数的均值。
入口由 options → Game runtime headless 初始化 → `rasterfall_render_world_benchmark()` 编排，
实现包含于 `src/dev-tests/rasterfall_world_benchmark.inc`；Linux/self 显式依赖与 Windows
自动 header 依赖覆盖该文件，不新增编译单元或资源。

`normal` 保留正常 V2 专用 planar path；`generic-planar` 只把同一批平面回退到旧
NULL-texture `textured_lit` 路径作 A/B；`constant-world` 把 helper world/root 查询替换为 256，
保留几何和逐像素路径（static RMESH 的直接 sample 仍保留）；`flat-planar` 保留细分，
仅把无纹理平面的顶点光照路径改为每三角形恒定光照；`no-planar-v2` 关闭平面细分与顶点路径，
其他 consumer 仍使用 V2；`legacy-enemies` 只替换普通敌人身体；`no-actors` 跳过 AI actor
提交，地图陈列角色仍保留。这些只用于诊断，不是正常游戏选项，也不启用 V1。

V2 planar normal 由 `draw_world_triangle_views()` 提交
`toy_renderer_triangle_planar_vertex_lit()`。该 command 只携带投影顶点、不透明 base
color、逐顶点 Q8.8 light 和三角形常量 fog；worker 保留与旧路径完全相同的
边函数覆盖、屏幕空间 light 重心插值、逆深度 `>=` 比较/写入、
`shade_color()` 光照后 fog 及整数舍入。它不携带 UV、texture sampler、material、
alpha 或 blend 分支。近裁剪与 light 插值仍在 world frontend 完成，1024 RFU
平面细分和 world-light sample 未改。三顶点 light 完全相同时，同一 planar command
可在 command 粒度先计算一次 `shade_color()` 再复用 flat 内循环；非相同 light 仍必定走
专用逐像素插值路径，`planar_constant_tris` 单独计数。

benchmark 在两种路径各自 flush 后比较完整 framebuffer 和 depth buffer，输出
hash、differing pixels/elements 与最大 RGB/depth 误差。`WORLD-PERF` 额外输出
`planar_tris/planar_constant_tris/planar_px/planar_us`、
`tex_tris/texture_fallback_cmds/tex_us` 与原
bbox/inside/raster wall 统计。路径 `*_us` 是各 worker 重叠活跃时间之和，
`raster_us` 才是主线程观察的 wall time。

frame 包括世界／实体提交与两次 flush，不含逻辑、begin、截图 IO、present、HUD、
交互层和战斗 effects；raster 是 flush 墙钟，含命令分类／排序／worker 等待。
敌人 skin/vertex/body/bones 明细是模型提交计时差值，不能视作所有敌人姿态适配的完整拆分。
独立 `WORLD-LOGIC` 对同一快照执行 16ms world tick，复制在计时外，排除 session/network/effects；
不代表持续 AI／导航缓存状态或完整 gameplay 帧。不同消融可能改变遮挡，不应把各项节省简单相加。

真实窗口统计保留 `scene`（动态光照 frame scope 开始、世界与 flags 提交）、`enemies`
（敌人、队友及 world label 提交）、`raster`（首个世界 flush）、`overlay`（交互、effects、
viewmodel、HUD 与后续 flush）。`begin` 只拥有 Core frame acquire/clear，`present` 只拥有最终
Core end/present；两者均在 Game render 外计时。阶段墙钟不重叠；`raster` 的命令、像素与路径
明细只覆盖首个世界 flush，后续 flush 的墙钟和像素归 `overlay`。
现场调查记录见 [2026-09-14 开销调查](archive/render-cost-investigation-2026-09-14.md)。
