# Research Renovation V1

> 状态：当前
> 所有者：Research 环境资产与 Outpost BX18 空间合同

Research 属于 stylized industrial/military 世界：冷灰、蓝灰主体，深灰框架，少量青白功能标记。
八件设备使用独立几何设计和四种 flat 材质；粗大标识合并进共享 ink primitive，不逐字增加材质。
不使用霓虹、高频格栅、螺丝阵列或 PBR 噪声。生产与验证见 [Research 指南](../guides/research-assets.md)。

## 资产合同

尺寸为 Blender 宽 X / 深 Y / 高 Z，单位米。源为 Z-up/-Y forward，GLB 为 Y-up/+Z forward，
bottom-center pivot，identity exported transform。RMESH 232 units/m 在 registry 展示边界换成
512 RFU/m；地图 scale=1000。碰撞由独立 RFU 模板定义，不读取 visual AABB。

| 资产（前缀 `rf_`） | 尺寸 m | 功能与结构 |
| --- | --- | --- |
| research_compute_rack | 1.0×0.9×2.1 | R01 / COMPUTE，两块横向实验算力模块 |
| research_build_rack | 1.0×0.9×2.1 | R02 / BUILD，双竖向 Core/build 模块 |
| research_power_cooling | 0.65×0.9×1.85 | PDU，粗散热片和双电力接口 |
| research_terminal | 1.15×0.85×1.8 | hero；独立支架、显示框、操作面和机体 |
| core_analysis_station | 2.4×1.15×1.95 | hero；大显示框、双分析模块、底座及操作面 |
| research_prototype_bench | 2.1×1.0×1.6 | hero；龙门测试架、模块支座和侧置分析区 |
| research_status_panel | 1.25×0.18×0.8 | RESEARCH / R01 大字状态牌 |
| research_wall_service | 1.8×0.22×0.65 | 检修模块与两条宽服务轨 |

预算例外归 [环境资产艺术约束](environment-art.md) 所有。Hero 是结构层次等级，不要求填满面数。
标识和静态屏幕都不代表实际 UI、实时数据或光源。

## BX18 布局与碰撞

`outpost.map` 的研究地面 X −10240…−4608、Z −3072…3072 RFU，约 11×12 m。
东门 X −4608，净开口 Z ±1229，过梁底 1843 RFU。精确边界通过 layout/query 复核。

| object ID（前缀 `research_bx18_`） | X / Z RFU | yaw | 区域 |
| --- | --- | ---: | --- |
| compute | −9250 / 2520 | 180 | 西北集群，面向南 |
| build | −8520 / 2520 | 180 | 西北集群，面向南 |
| power | −7880 / 2520 | 180 | 集群辅助 |
| core | −9400 / 0 | 90 | 西侧焦点，面向入口 |
| prototype | −8200 / −2500 | 0 | 南侧原型区 |
| terminal | −5330 / −2170 | 270 | 东门南侧，面向房内 |
| status | −4766 / 2320 | 270 | 东门北侧，底高 650 RFU |
| service_prototype | −8200 / −2904 | 0 | 南墙，底高 960 RFU |
| service_cluster | −9120 / 2950 | 180 | 北墙，底高 1230 RFU |

六个落地实体通过 `attr.collision=component` 各生成一个普通 box，非 walkable。
三个壁面实例显式 `none`。Core 台下按整盒阻挡，不逐件增加小模块碰撞。
东门和中央通道保持开放；原地板舱盖作为少量基础设施保留。新记录无 legacy_index、
模型路径或 renderer 状态；Terminal ID 只预留物理入口，没有新 interaction。

## 接线与所有权

稳定 registry ID 53–60，kind 为表中不带 `rf_` 的名称。源在本地
`rasterfall/private-assets/source/props/research/`，manifest 在
`tools/assets/manifests/props/research/`，公开 RMESH 在 `rasterfall/assets/models/props/research/`。
生成器为仓库自有程序资产；源 Blend/GLB 不提交，公开 RMESH 可由生成器复现。

CPU static prop 与独立 GPU Scene 都按 registry 加载普通 RMESH，Scene 通过现有资源缓存与
静态实例提交。没有 Research 专用 producer、RasterCmd、bridge 或 gameplay 状态。
根 Makefile 的递归资源依赖及 Windows package 的递归复制覆盖 research 目录。
Render Control UI、Desktop、Terminal application 与研究玩法均不属于 V1。
