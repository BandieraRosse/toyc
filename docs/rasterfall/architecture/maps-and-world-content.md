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

## Map IR Runtime Bridge

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
GPU Scene 另以同一 object 投影顺序和 Runtime Map authored ID 冻结静态 prop 值；
boundary wall 网格从该只读值帧构建，不改变 Runtime Map、碰撞或玩法所有权。

正式地图源位于 `rasterfall/assets/maps/rasterfall.map`；旧兼容源为同目录的
`rasterfall_legacy.map`。旧源的磁盘结构定义在 `include/toy_map.h`，文本解析在 `lib/map.c`，仅供显式 fallback/reference
使用。`--legacy-map` 和 `rasterfall_session_load_legacy()` 是当前保留的 legacy compatibility entry。V1 的 parser、IR、
Runtime Map 和 projection adapter 是默认输入链路；修改语法时必须同时检查 parser、runtime、玩法绑定、
碰撞/导航、渲染和逻辑测试。

显式启动地图只覆盖本次进程；地图 `attr.identity` 决定本次 session 的既有 world/content policy，无 identity 时沿用 Campaign policy。实验命令见[地图编辑指南](../guides/map-authoring.md)。

Outpost V1 使用同一 V1 链路，源文件为 `assets/maps/outpost.map`。`station_terminal`、
`operations_terminal`、`super_terminal`、`return_outpost` 和 `return_to_whu_v0` 是 Game-owned interaction vocabulary，分别
映射为 Station GUI 请求、Campaign 01 world request、锁定反馈、返回请求和 Return-to-WHU Planar Massing V0 world request；它们不是 Core API，也不改变
Runtime Map ownership。
