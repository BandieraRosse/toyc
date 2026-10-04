# 地图与世界内容架构

> 状态：当前
> 所有者：Rasterfall Map Runtime 与 session 世界生命周期
> 事实入口：`rasterfall/lib/rasterfall_map_parser.c`、`rasterfall/lib/rasterfall_map_runtime.c`、`rasterfall/src/rasterfall_map.c`、`rasterfall/src/rasterfall_session.c`
> 最近核对：2026-09-21

空间地图 `.map` 与 Game-owned `assets/worlds/*.content` 分别保存空间事实和 actor、terminal、flag、formation 等世界内容。静态 world identity 将两者绑定；内容地图不进入 Runtime Map。字段契约见 [地图格式](../reference/map-format.md)，修改和查询流程见 [地图编辑与查询](../guides/map-authoring.md)。

## 所有权和依赖方向

```text
.map text → Map Parser / Map IR → Runtime Map → Gameplay Projection Adapter
                                              ├→ collision / navigation
                                              └→ renderer draw records
World Content text → Game-owned parser → session / gameplay actor state
```

parser 只解释格式并产生运行时无关的 Map IR；Runtime Map 拥有稳定 ID 和运行时视图；session 拥有 level/map 的 load、binding、reset 与 unload。Game projection adapter 转换为现有玩法、碰撞和渲染兼容结构。可见几何、碰撞、玩法声明与 World Content 是不同输入，不能互相代替；renderer 不解析地图文本，也不拥有玩法真值。

性能基准使用 `performance_empty` 和 `performance_components` 两个静态 identity，
映射到各自 `.map` 与共用空 `performance.content`。组件版只增加 crate 对象和组件碰撞，
不在渲染中删除碰撞或通过 visibility 改变玩法投影。它们由性能控制器请求，
不加入指挥桌部署目录。测试结束仍走完整 session load/reset；合同见[实验区合同](../reference/experiment-labs.md)。

## Map IR Runtime Bridge

标准实验区域由 `rasterfall_map_labs.inc` 在 Parser 层展开：`lab` 生成实验 region，
`attr.lab` 子记录在全文件解析完成后转换到世界坐标。Runtime Map 保存区域用途、围合和原点，
其余消费者继续接收普通世界坐标记录；renderer 不解析复合文本。展示角色、步行台位、原创模型
和光照试样在来源求值边界将局部坐标加上 Runtime Map 原点，随后冻结到原有只读 Scene 值帧。
生成工具只生产地图文本，不拥有 runtime 规则；格式见[地图格式](../reference/map-format.md)。

机器组件的 `assembly` 同样由 Parser 降级：可放入 lab，object 与 sign 通过 `attr.assembly`
引用其平移原点，Y 高度在边界转换为各自既有坐标合同。它不拥有交互、材质、字体或碰撞；
控制 ID 留在根 object，各模型通过 registry 独立加载。Runtime Map 与 gameplay 接收展开后的普通记录。

`rf_map_runtime_load()` 在堆上分配容量型临时 Map IR，转换完成或失败后释放；
Runtime Map 继续独立持有运行时数据。避免约 1 MiB 的局部 IR 占用 Windows 启动调用栈。

`rasterfall/lib/rasterfall_map_runtime.c` 将 V1 parser 的结果复制为不暴露 parser 内部结构的运行时视图，
公开 region、interaction、actor spawn、pickup、object 和 collision 的稳定 ID 查询；各类记录按稳定 ID 规范化，
调用方不依赖文本行顺序。
interaction 的 `action` 仍是字符串，runtime registry 再把它解析为 action ID；parser 不包含 gameplay callback。

`src/rasterfall_map.c` 提供 Gameplay Projection Adapter，把 V1 region、interaction、actor_spawn、pickup、object、
collision、surface 和 render 转换到 gameplay/renderer 现有数组。因此 collision primitive、collision engine、spawn 算法、
AI、prop 和 renderer 行为保持不变；Runtime Map 是 authoritative world representation，projection 只是迁移期接口，
默认流程只加载 V1 Runtime Map。
`legacy_index` 只用于兼容数组的稳定排列，不是 V1 record 的顺序语义。
region 投影按显式索引排列后追加无索引记录，每条只消费一次；安全区可分开声明观察支带，
不把性能场出生范围纳入安全区。
GPU Scene 另以同一 object 投影顺序和 Runtime Map authored ID 冻结静态 prop 值；
boundary wall 网格从该只读值帧构建，不改变 Runtime Map、碰撞或玩法所有权。

正式地图源位于 `rasterfall/assets/maps/rasterfall.map`；旧兼容源为同目录的
`rasterfall_legacy.map`。旧源的磁盘结构定义在 `include/toy_map.h`，文本解析在 `lib/map.c`，仅供显式 fallback/reference
使用。`--legacy-map` 和 `rasterfall_session_load_legacy()` 是当前保留的 legacy compatibility entry。V1 的 parser、IR、
Runtime Map 和 projection adapter 是默认输入链路；修改语法时必须同时检查 parser、runtime、玩法绑定、
碰撞/导航、渲染和逻辑测试。

显式启动地图只覆盖本次进程；地图 `attr.identity` 决定本次 session 的既有 world/content policy，无 identity 时沿用 Campaign policy。实验命令见[地图编辑指南](../guides/map-authoring.md)。

Outpost V1 使用同一 V1 链路，源文件为 `assets/maps/outpost.map`；单层开放屋顶基地、四向大厅与七件家具的
空间合同见 [Outpost V1](../reference/outpost-hall-v1.md)。当前 content 只放置 Null，家具为地图
object，不声明交互终端；设备视觉不改变 Game 状态。既有 `station_terminal`、
`operations_terminal`、`super_terminal`、`return_outpost` 和 `return_to_whu_v0` 是 Game-owned interaction vocabulary，分别
映射为 Station GUI 请求、Campaign 01 world request、锁定反馈、返回请求和 Return-to-WHU Planar Massing V0 world request；它们不是 Core API，也不改变
Runtime Map ownership。

边缘站点 01 使用 identity `frontier_station_01`，配对 `assets/maps/frontier_station_01.map`
与 `assets/worlds/frontier_station_01.content`。World Content 定义五名正式队员及编组；敌方守军、
周期感染者和最终增援由 session 的局部任务模块创建。部署与返回沿用普通世界请求，入口只对离线运行开放。

Runtime Map 的 `mission_guard`、`mission_entry` 与 `mission_route` region 提供稳定空间绑定，
三设施通过 `frontier_workshop_terminal`、`frontier_storage_terminal`、`frontier_energy_terminal`
object ID 绑定，真实普通机仍为 `mesh_weaver`。生成工具和地图元数据描述位置、路径与设备，
不决定阶段、配额、接管或胜负。绑定时验证必要 ID、记录种类和守军的正式地面/身体碰撞，
缺少引用或落点被阻挡时明确报告并拒绝半有效任务。

设施控制和运行开关属于 session/Game；对象视觉和 renderer 只投影状态。仓库接管开放配置的 CPU/存储能力，
能源接管开放既有供给，车间接管开放普通制造；接管与开关不重置有限材料、储能或待取成品。
设备模型及 component collision 分别投影，货架空隙与门洞必须在正式导航和身体碰撞中成立。
详细任务所有权见[玩法架构](gameplay.md#边缘站点-01-的局部任务权威)。

Research BX18 使用同一 object → registry → static RMESH 路径；六个设备的粗盒由独立 component
模板生成，壁挂件无碰撞，物理终端不声明 interaction。资产、摆放和预算见
[Research V1](../reference/research-renovation-v1.md)。CPU 与 GPU Scene 都消费已有静态实例来源。

Host Rack 的地图候选位以 `attr.length` 编码柜号和自下而上的槽号。Runtime Map 保留八个 authored 候选；
平台硬件数量在 projection/presentation 边界决定机柜是否启用。未启用柜的生成碰撞 flag 关闭，
CPU renderer 与 GPU Scene snapshot 共用 prop resolver 隐藏其模型；启用柜的空槽显示封板。
地图 identity 保持稳定，实时负载只进入只读展示，不参与联机权威状态。
资产、容量换算及摆放见 [Host Rack V2](../reference/host-rack-v2.md)。
