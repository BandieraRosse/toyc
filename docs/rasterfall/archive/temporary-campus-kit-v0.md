# Return to WHU — Asset Audit & Temporary Campus Kit V0

> 状态：历史；记录 2026-09-13 临时校园套件的清点、生成和合成场景验收。
> 当前入口：[资产规格](../reference/temporary-campus-kit-v0.md)、[生成与验收指南](../guides/temporary-campus-kit-v0.md)。

> 文档更新：2026-09-13
> 源码核对基线：2026-09-13 registry、工业/Architectural manifests、Builder、importer、static RFM2 v2 writer、Visual CLI 和 campus 独立 fixture。

## Audit 结论

清点对象为已注册、公开可加载的23件环境RMESH，以及现有无纹理ground/floor/platform/ramp/wall表达。
工业库存能组成维修、供电、补给与检查站空间；surface能形成道路、广场、操场和任意footprint地面。
缺的是校园的楼层墙窗、普通入口/柱/檐口、人行边缘、两种台阶、挡墙与树代理。
本轮补12件即可进入可撤回的Planar Massing V0，不需要再做一套铺地模型。

这些是 **Temporary Campus Kit**；合成街角不代表武汉大学任何实测宽度、坡度、标高或正式建筑。
不创建.map，不修改Reference JSON，不加gameplay、spawn、AI、网络、renderer或collision系统。

## A — CAMPUS_REUSABLE

| 现有资产/能力 | 能形成的校园空间及边界 |
| --- | --- |
| rf_railing（2.4 m长/1.1 m高） | 广场、台阶平台和操场边缘的通透边界；工业底脚可接受为临时代理，避免重复guardrail |
| rf_lamp_post（3.2 m高） | 人行道、入口的尺度与稀疏视觉节点；局部标牌不是WHU现实物件，灯面不提供真实光源 |
| rf_arch_wall（4 × .24 × 4.2 m） | 可复用为次要院墙/边界；暗端筋和高墙脚不适合逐层校园主立面 |
| rf_arch_beam（6 m长） | 必要时提供次要/隐藏承重跨度；不作为校园主要装饰 |
| 既有无纹理surface/primitive | 道路、广场、草地区块、操场/跑道、屋面和任意建筑footprint体量；颜色与边线直接分区，零新纹理、零铺地RMESH阵列 |

Architectural两件为有条件的次要复用，本轮主立面不用它们。
新plain wall提供3.2 m楼层模数与无暗筋表面，不重复旧4.2 m工业边界墙职责；
保留Architectural已冻结资产。

## B — RF_OVERLAY

| 资产 | 可形成的战时空间 |
| --- | --- |
| rf_crate、rf_ammo_container、rf_barrier、rf_short_wall | 补给垛、临时检查站、道路分流和阵地边缘 |
| rf_vent_unit、rf_workbench、rf_industrial_pillar、rf_pipe_module、rf_power_unit、rf_gate_frame、rf_control_cabinet | 工坊、临时供电站、维修点和工业设备带 |
| rf_arch_support、rf_arch_doorway、rf_arch_pipe_straight、rf_arch_pipe_elbow、rf_arch_pipe_tee、rf_arch_service_panel、rf_arch_cable_tray、rf_arch_floor_hatch | 维修平台、工业宽开口、连续检修/服务带 |

共19件；不把这些解释为武汉大学校园原生设施。
doorway的4.8 × 3.6 m工业开口、pillar的柱脚套环、support的维修语义
不替代普通校园入口/柱。以后overlay单独设计，不反向改变现实层。

## C — CAMPUS_MISSING / 本轮补齐的12件

尺寸顺序为Blender X/Y/Z宽/深/高，单位m；均为authored模数，不是WHU测量值。

| 新资产 | 尺寸m | 组合用途 |
| --- | --- | --- |
| rf_campus_wall_plain | 4 × 0.24 × 3.2 | 盲墙、侧墙、转角和按楼层叠放的干净外壳 |
| rf_campus_wall_window | 4 × 0.24 × 3.2 | 整层墙窗；窗底1 m、窗高1.4 m、1 m窗格节奏 |
| rf_campus_window_strip | 4 × 0.24 × 1.4 | 供图书馆等自定义低细节外壳使用的独立窗带，不强制整层墙高 |
| rf_campus_entrance | 4 × 1.2 × 3.2 | 替换一段墙的2 m宽/2.5 m高几何开口与简洁雨篷 |
| rf_campus_roof_edge | 4 × 0.6 × 0.3 | 连续檐口/屋顶轮廓收边；完整屋面仍用surface |
| rf_campus_column | 0.4 × 0.4 × 3.2 | 同楼层高度普通柱，避免工业柱脚或非均匀拉高 |
| rf_campus_stair_short | 4 × 1.2 × 0.6 | 4级、.30 m踏步/.15 m踢面的小平台转换 |
| rf_campus_stair_long | 4 × 2.4 × 1.2 | 8级、同踏步尺寸的较高平台转换 |
| rf_campus_retaining_wall | 4 × 0.4 × 1.2 | 台阶旁平台边缘和连续挡土墙 |
| rf_campus_curb | 4 × 0.2 × 0.15 | 道路与人行道之间可读的高度边缘 |
| rf_campus_sidewalk | 4 × 2 × 0.15 | 有厚度、可按4 m连续拼接的人行道 |
| rf_campus_tree_proxy | 3 × 3 × 5 | 极简绿化带的树干/树冠尺度与远景识别 |

未生产候选及理由：

- guardrail：已有railing覆盖。
- road_surface、grass_patch、track_surface：现有无纹理surface覆盖。
- 广场/完整屋面：现有surface覆盖，不新增floor mesh。
- 没有为数量增加材质变体、小道具或完整建筑；没有删除用户资产。

wall_window负责默认楼层；独立window_strip供自定义层高/低细节图书馆外壳，
两者职责不同。column用于入口/架空承重，不用工业柱非均匀缩放。

## 拼接与运行契约

统一底面中心pivot、identity transforms、米制源尺寸、4 m长边模数；
placement只用yaw和scale=1000，authored楼层为3.2 m。
Blender Z-up/-Y-forward → GLB Y-up/+Z-forward → RMESH 232 units/m →
registry 2207 milli-scale → world 512 RFU/m。
坐标量化约4.3 mm，加整数展示换算允许毫米级误差，不能声称数学零误差。
模块没有额外装饰缝；窗带按1 m节奏连续拼接。
拼接时使用完整性报告的snap_dimensions_rfu（按现有整数展示换算得到的实际边界）。
例如3.2 m墙高转换为742 RMESH units、1637 RFU；楼层pivot按1637 RFU递增，
避免按浮点3.2 m再次取整产生亮缝。4 m长边的实际拼接间距为2048 RFU。
这是记录既有量化后的拼接规则，不改变源比例、缩放或renderer。

楼梯GLB +Z端低、-Z端高；yaw=180时从-Z向+Z上升。
pivot在投影底面中心，不在第一步前沿；短/长低端距pivot为.6/1.2 m。
踏步只展示合理玩家尺度，不说明校园真实阶数或坡度。

色块为灰白墙、浅灰混凝土、低饱和蓝灰不透明窗、灰道路、绿地和土红跑道；
无黑黄警戒边、大管线、PBR新能力或新纹理。

当前static RFM2 v2双面路径会使薄闭合box背板/底面产生远距离深度竞争。
本套box主动省去背面和底面；墙窗不保留层间隐藏顶面，窗带去掉内部相接侧面，
窗/框前面直接相邻分区。台阶只包含外露踢面、踏面和整体阶梯侧面，不保留内部box面。
这是双面可见的开放视觉壳，不是封闭实体、水密模型、室内或物理体积。
不修改renderer，不扩展Architectural专属culling，不修改模型格式。
树冠为固定三角化粗面代理；源三角形规范排序，消除Blender join分配顺序差异。

stable presentation IDs 24–35，registry collision_size全零；
现有collision_dimensions拒绝生成其AABB，logic gate覆盖此行为。
后续legacy prop须显式collision=none；玩法承载另用显式collision记录，
不能把入口/楼梯包围盒当通路碰撞。本轮不搭正式地图。

## 所有者、来源、重现

| 职责 | 入口 |
| --- | --- |
| 几何/flat材质/GLB | tools/blender/generate_campus_kit.py，复用工业Builder/export和断言 |
| 离线导入 | tools/assets/manifests/props/campus/ → tools/assets/import_asset.py |
| 公开RMESH | rasterfall/assets/models/props/campus/ |
| registry/无默认碰撞 | rasterfall/include/rasterfall_prop.h、src/rasterfall_prop.c |
| 独立实际光栅验收 | src/dev-tests/rasterfall_visual_capture.inc 的campus_capture() |
| 自动化/组图/检查 | tools/campus_kit_round.py，复用architecture_round.py日志和Pillow sheet |

资产为仓库自制程序化临时几何，无第三方图片、校园照片或外部纹理输入。
GLB/Blend保留在private-assets/source/props/campus与tmp，本地生成物不提交。
Linux embedded递归依赖和Windows package递归assets复制覆盖公开目录；
无新增编译单元。Windows构建与交互式窗口不属于本轮验证结果。

```sh
make rasterfall
python3 tools/campus_kit_round.py --generate --capture --deterministic --audit
make test-asset-pipeline
build/rasterfall --logic-test
build/rasterfall --help
git diff --check
```

脚本先构建转换器一次，然后importer --no-build；避免同时运行多个make。
完整性门验证manifest、RMESH/纹理引用、v2布局长度、索引范围、米制量化边界和面数预算。
GLB两次独立生成；全部新增截图两次逐字节比较，Blend不承诺字节确定性。
现有23件检查RMESH/纹理契约并输出四视图，Architectural另用实际arch-family路径审阅。
逐件视图是归一/合成取景，不能替代真实尺度组合验收。

## 独立视觉验收与交接

输出位于tmp/campus-kit-v0/：

- kit-sheet.png：12件独立俯斜视图。
- campus-corner.png：总览、near/mid/far组图；captures/保留BMP、PNG和repeat BMP。
- campus-ground.png：从操场侧观察广场、两种台阶、挡墙、草地和跑道边缘。
- existing-assets.png、existing-architecture.png：已有库存审阅。
- integrity-and-fingerprints.json：运行产物指纹、量化边界；每次CLI/import/generation日志同目录。
- build.log、asset-pipeline-test.log、logic-test.log、help.txt：实际工程证据，测试数量不写入稳定导航。

合成街角包含六段连续墙窗、两层叠放、入口、转角盲墙、连续檐口/人行道/路缘、
道路、广场、短/长台阶与各自平台、连续挡墙、操场绿地/跑道边线、树和复用栏杆/灯柱。
1.7 m直立灰色尺提供玩家尺度，不创建战斗角色。
审阅结论：窗带有连续节奏；道路与人行道高度边缘可读；台阶、平台比例清楚。
中远景保留墙窗、入口、道路、绿化与操场色块；工业语言仅残留在复用灯/栏杆的小部位。
消除了首轮薄板背面三角穿透和近共面背景闪线；正常低分辨率边缘仍有光栅锯齿。
没有明显长边脱节；不声称任意yaw拼接、正式校园拓扑或大量实例性能已签收。

资产能力已足够搭17/18舍的通用外部体量、图书馆低细节外壳、广场、道路和操场平面；
没有阻塞Planar Massing V0的新mesh缺口。
现实道路宽度、高程、坡度、图书馆高度和footprint精度继续依照Reference V0保持未知。
需要这些真实参数的阶段须等待取证，不能把本合成验收场景变成校园事实。
未开始正式地图，未提交，等待用户审阅。
