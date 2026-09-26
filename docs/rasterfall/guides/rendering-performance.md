# 渲染性能诊断

> 状态：当前
> 所有者：Rasterfall CPU renderer 与 world producer 性能诊断
> 最近核对：2026-09-23
> 事实入口：`build/rasterfall --help`、`rasterfall_perf.c`、`src/dev-tests/rasterfall_world_benchmark.inc`

本文用于区分 world producer、角色模型提交、CPU raster 和完整窗口阶段成本。GPU native whole-loop、
bridge 和 physical-device A/B 由 [GPU 验收与诊断](gpu-validation.md)与
[GPU 性能标准](../reference/gpu-performance-standards.md)拥有。

## 先选正确入口

| 问题 | 入口 | 口径 |
| --- | --- | --- |
| 单个角色资源、pose、skinning、vertex cache、submission | `--character-performance` / `--character-performance-suite` | 角色微基准 |
| material/form-light/raster 消融 | `--model-performance` | 模型离屏基准 |
| 固定五角色并发与 worker | `--actor-performance` | actor 并发基准 |
| Campaign world 与敌人数扩展 | `--render-performance` | world producer + raster 离屏基准 |
| 正常窗口阶段 | runtime `rasterfall_perf` 输出 | begin/scene/enemies/raster/overlay/present |
| GPU native 整帧与 bridge | GPU sampling/metrics 工具 | 物理 GPU whole-loop |

不要把不同入口的累计计时、wall time 或分位数相加。

## 角色微基准

```text
--character-performance <model> [warmup] [frames] [repeats] [workers]
--character-performance-suite ...
```

每个实例共享 resource、持有独立 pose。输出规模、hierarchy、skinning、vertex cache、model CPU submit、
raster wall 和 total wall 的 mean/median。`submit` 是模型阶段 CPU 累计口径，不能从并行 wall time 直接
相减。suite 的 optional private asset 缺失应为 SKIP。

## World benchmark

典型入口：

```text
build/rasterfall --render-performance 12 --textures
```

它在 1280×720 离屏表面使用固定 Campaign 内容，比较 near/mid 与 0、10、30、60 个普通敌人；保留地图
actor、static props 和 Campaign fixture，不运行真实波次。每项先预热，再报告指定帧数的均值。

可用诊断消融包括 normal、generic-planar、constant-world、flat-planar、no-planar-v2、legacy-enemies、
no-actors。它们只用于定位，不是正常游戏选项；不同消融可能改变遮挡，收益不能简单相加。

benchmark 在路径 flush 后比较完整 framebuffer/depth hash、差异元素与最大误差。`WORLD-PERF` 的
`*_us` 可能是多个 worker 的重叠活跃时间之和；`raster_us` 才是主线程观察到的 raster wall。

该 frame 包含 world/entity submission 与两次 flush，不含 gameplay logic、begin、截图 IO、present、HUD、
交互和战斗 effects。`WORLD-LOGIC` 只对同一 snapshot 做独立 16ms tick，不代表持续 AI、导航 cache、
session/network 或完整 gameplay 帧。

## 正常窗口阶段

runtime 阶段墙钟互不重叠：

- `begin`：Core frame acquire/clear；
- `scene`：world light scope、世界与 flags submission；
- `enemies`：敌人、队友和 world label submission；
- `raster`：首个 world flush；
- `overlay`：interactables、effects、viewmodel、HUD 与后续 flush；
- `present`：Core end/present。

首个 flush 的 command/pixel/path 明细只解释 `raster`；后续 flush 归 `overlay`。较大的 present wall 也
不能在没有 queue/fence 证据时归因于 present API。历史现场见
[2026-09-14 开销调查](../archive/render-cost-investigation-2026-09-14.md)。

## 旧地图真实时钟与西侧按钮

`tools/gpu_scene_old_map_perf.ps1` 在 Windows package 上自动测量旧 Campaign，固定 1280×720，
保存 exe/map hash、参数、逐帧日志、实际敌人数以及操作系统进程/线程 CPU 时间。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_old_map_perf.ps1 -Stage Corridor -OutputDirectory tmp/corridor-perf
```

Corridor 比较空走廊、原随机按钮、无 Tank 按钮，默认测试 32、64 敌人，每种同时运行
fixed/realtime，默认三轮、每轮 360 帧，隔轮反序。`-CorridorEnemies 16` 可复跑原单次按钮
负载；PowerShell 内调用脚本时可传数组选择多个档位。显式 normal view 将观察位置设在走廊
`(-32000,0)` 并朝西；按钮场景先预热 60 帧，再用按钮位置的交互相机调用正式 session
交互入口，在第 61 帧连续调用 2/4 次原有每次 16 敌人的按钮，构造同时存活的 32/64 敌人。
这不模拟人工按键之间的时间间隔，不改变正式按钮的生成数量、随机池或出生区域。
玩家可被正常拉拽/击飞；不锁血、不停 AI、不覆盖敌人 HP 或复活已死敌人。
它是可重复的按钮负载诊断，不模拟玩家从出生点走到按钮的全过程，也不包含持续射击输入。

Stationary 提供 near 的低敌人数 fixed/realtime 对照及其他镜头；Auto 使用既有 `--auto`
移动、转向、开火和真实波次；All 运行全部。自然波次不能替代一次按钮整波。
时间口径、相机和实际存活/死亡人数来自 `--frame-audit` 下的 `SCENE-RUNTIME`；帧间隔跨越
完整两次采样点，补充较早打印的 `whole_loop_us`。输出保留冷热转换及首次按钮资源开销。

`gpu_scene_old_map_report.py` 检查完整 native 帧、零 bridge/readback/mixed、地图组件、按钮实际
生成数量及帧号、空预热和敌人种类，输出 mean/median/P95/P99、相机/存活/死亡/AI 范围，
以及按实际活敌人数分组的结果（32 与 64 档分开）。分组只包含未暂停的
PLAYING 帧；初始生成 30 不能当作持续 30。CPU 核占用是同一进程各线程累计 CPU 时间除以采样
墙钟，1.0 表示一个逻辑核，绝不是整机百分比；不把退出阶段停留的末帧日志算入游戏 CPU 窗口。
最忙线程不等于已由 OS 标识的 main thread，必须结合串行调用链归因。原始数据始终保留。

这些有逐帧日志及外部采样的测试是归因工具，不代替低扰动产品 FPS 验收；真实时钟会改变 AI、
死亡和特效演进，不能要求与 fixed tick 的逐帧 workload 相等。现场与优化建议见
[西侧按钮调查](../archive/west-corridor-performance-20260925.md)。

### 共享目标导航场诊断

默认使用共享场；同一 package 的 `RF_GAME_LEGACY_FLOW_NAV=1` 选择旧集团，
`RF_GAME_LEGACY_GROUP_NAV=1` 选择逐敌参考路径。避免同时设置对照开关。
`RF_FLOW_TEST=1` 配合 `--logic-test` 只运行共享场行为用例及正式西侧走廊 32/64 人新旧对照。
必须等待进程实际退出并检查 stdout、stderr 和 `rasterfall.log`，不能只读 PowerShell 的表面退出码。

`FLOW-TEST` 记录到达数、建场、边验证和旧搜索次数；`FLOW-CORRIDOR` 使用固定种子、相同目标与
图元，运行 1200 个固定逻辑步。咬击反推冷却在该专项中固定为长时，以免把到达后被击退计为未到达。
新旧策略的集结等待、出发时间和路程不同，须同时核对全员到达及整段工作量，不能只比较某一静止阶段。
该专项不包含 GPU 渲染，不代表帧率签收。

`SCENE-LOGIC-FLOW` 的 `builds/edges/hits` 为建场次数、物理边验证和连接缓存命中，
`samples` 是共享场/目标连接扫掠预留的有界采样工作量，直达短连接另看 `nav_short_queries/nav_samples`。
`waiting/repairs/evictions/overflow` 分别计没有共享路点而进入局部探索的调用、共享格细分、场淘汰及
节点容量不足。持续移动版本的 `waiting` 不代表敌人原地等待，也不等于实际停步时长。
`work_us` 只覆盖共享任务调度/搜索/细分，不包含全部个体直达与局部移动；它嵌套于 world 更新，不能
与 `world_us` 相加。`intent_us` 覆盖个体直达检查、近追可达候选选择和局部路点接入；共享场的
导航计时为 `work_us + intent_us`，集团对照为 `nav_group_us + intent_us`。这些计时不包含实际
位移碰撞、战斗和分离；读取总成本时仍须检查地面查询及完整 world 更新。

### Game 导航与扫描分段

西侧坡道的逻辑地形对照可在 Windows package 根目录运行：

```powershell
$env:RF_TERRAIN_BENCH='1'
.\rasterfall.exe --logic-test
Remove-Item Env:RF_TERRAIN_BENCH
```

该显式诊断入口跳过普通逻辑回归，使用正式地图的玩法图元、正式西侧按钮的刷怪矩形、
固定种子和全普通追击敌人。对照组只把西侧六段坡道／平台碰撞的高度差压为零，
保留图元类型、数量、顺序、其他地图内容及固定目标；0、16、32、64 人各运行
五轮交替顺序的 960 个 16ms Game tick，覆盖默认共享场追击及后续行进。`TERRAIN-BENCH-TIME` 是未绑定 Game
profile 的完整 world 更新墙钟；`TERRAIN-BENCH-NAV` 来自同初态的第二次运行，
用于解释导航候选、采样和碰撞扫描，不与前者相加。测试要求所有敌人保持存活且
首个敌人穿过坡道。两种地形可能选择不同路径，应先核对实际轨迹和工作量，
不能把总耗时差解释为单次高度计算成本。此入口不测试渲染、GPU 或真实时钟帧率。

独立 Scene 的 `--frame-audit` 额外输出 `SCENE-LOGIC-NAV`，报告工具将可用的
`SCENE-LOGIC-*` 分组汇总到 `report.json` 的 `logic_profiles`；旧日志允许缺少这些组。
`nav_search_us` 包含 BFS 初始化和搜索；`nav_paths_us` 包含沿父链尝试候选目标、
线段障碍检查及逐点地面/碰撞验证。两者属于 Game 更新内的嵌套计时，不能再与
`world_us` 或敌人耗时相加。`nav_paths_max_us` 是本渲染帧中单次父链验证的最大耗时。

`nav_searches/nav_nodes/nav_candidates` 分别计实际搜索、出队节点和候选目标；
`nav_segments/nav_samples` 计路径地形验证调用与实际采样点，含队友直接路径检查。
`nav_ground_queries` 只计父链验证内的地面查询，可与全帧 `ground_queries` 比较。
`ground_scans` 是地面查询遍历的图元数，`ground_heights` 是范围排除后实际求高度次数。
`body_queries/body_scans` 计高度碰撞查询和两个扫描循环实际访问的图元数；
`segment_queries/segment_scans` 计直线障碍检查及实际访问图元数。扫描量包含被标志过滤的项，
`ramp_transition_queries/ramp_transition_scans` 计坡道接缝检查及其实际扫描量。
扫描量不等于相交数，也不覆盖所有玩法碰撞函数。路径计时在函数阶段边界读取时钟，图元循环仅计数。
这些计数仍会扰动执行；关闭帧审计不绑定 Game profile，正式性能收益须另作低扰动验证。

导航采样默认复用同一次高度碰撞查询的地面结果；`RF_GAME_LEGACY_NAV_GROUND=1` 在 Game
帧审计下恢复重复查询及高台范围排除前的坡道扫描作诊断对照。该开关不改变候选路径或碰撞判定，
未绑定 profile 时不生效。`RF_GAME_PROFILE_NO_CLOCK=1` 保留查询/扫描计数并关闭 Game 内部读时钟；
计时附带的 world/敌人调用计数也为零。它可定位读时钟扰动，不能代表
关闭全部计数的低扰动运行。环境变量不由确定性 Game 层读取。

同包交替对照入口如下；每个 workload 紧邻执行复用／参考路径，第二轮将整个顺序反转，
固定 tick 逐帧核对相机、实际敌人种类/数量及导航/碰撞工作量，允许地面查询数量下降。
完整 Game 状态、随机数和事件的逐 tick 对照由 `--logic-test` 中的双随机池用例覆盖。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_old_map_perf.ps1 -Stage Corridor -Rounds 3 -CompareNavGround -ProfileTriangleUpdate -OutputDirectory tmp/nav-ground-reuse-ab
```

`-ProfileTriangleUpdate` 设置 `RF_GPU_PROFILE_TRIANGLE_UPDATE=1`，额外汇总成功的动态三角形
资源更新：`SCENE-TRIANGLE-UPDATE` 的 `validate_us` 包括动态输入校验及 bounds 计算，
`map_us` 包括必要的 staging 容量准备及首次映射，`copy_us` 仅计连续 `memcpy`，
`flush_us` 计非 coherent 刷新及其收尾，`transfer_us` 包括可选 copy 提交/等待及更新收尾。
顺序三角形资源创建时会持久映射 host-visible 顶点内存；无此内存时，首次更新创建并映射保留的
staging。映射在资源销毁时解除，热帧通常没有 map/unmap。`bytes` 是复制字节，
`flush_bytes` 是按设备 nonCoherentAtomSize 对齐后的实际刷新字节；coherent 更新为零。
`direct_flags` 和 `staging_flags` 是成功更新所用分配的 Vulkan memory property 位掩码累积值。
这些字段是 CPU 墙钟，覆盖敌人、程序角色及分层动态资源；
不含新建资源或失败更新，不能直接等同于 `enemy_upload_us`。`staging` 记录采用 transfer
路径的更新数；为零只证明这些成功更新没有 staging 提交。字段关闭时不额外读时钟。
报告的 `triangle_updates` 保留分段、字节数和资源数，`nav_ground_comparisons` 保留每轮配对差值。

## 诊断规则

独立 Scene 的固定场景成本工具不是空地图测试：默认加载 Campaign，保留 AI、展示内容和组件；
`tools/gpu_scene_cost.ps1` 的默认矩阵仍为 0/30/60 敌人。少量敌人问题可用
`tools/gpu_scene_preview.ps1 -Independent -Views near -Enemies 10 -Frames 96`
及 20 敌人档单独采样。预热帧、实际存活/死亡来源数和镜头必须同时记录。
该工具强制每渲染帧一个 16ms 逻辑 tick，不能据此排除真实时钟下 AI 补步的成本；
也不能把固定 near 镜头结论推广到任意地图位置。
旧地图光照与覆盖范围的源码核对和补测见
[2026-09-25 调查](../archive/old-map-lighting-investigation-20260925.md)。

1. 先固定 build、资产、分辨率、camera、seed、tick、worker 和 workload。
2. 先用阶段统计定位问题域，再选择一个独立消融。
3. 同时保存输出 hash、命令/像素规模和 timing；画面或 workload 变化时性能对比无效。
4. 预热后运行多次，报告每轮值与聚合方法；单次首帧不构成收益结论。
5. 改动后先跑最近的逻辑/像素/视觉回归，再按风险扩大到正常窗口或 GPU native。
