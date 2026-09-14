# Architectural Environment V1

> 文档更新：2026-09-14
> 源码核对基线：2026-09-14 Campaign `rasterfall.map` 的 `env_arch_*` 与 object.y projection；资产沿用 2026-09-13 最终 panel/hatch、industrial RMESH、registry 与 scoped culling。

**Visual design = VISUALLY FROZEN。Engineering checkpoint = pending Sol。**
最终 service panel / floor hatch 已完成 runtime 视觉签收；保留原十件 CANONICAL，不再扩展或打磨。
恢复验收未修改 geometry、placement 或 renderer。证据及工程接力清单见
[最终视觉签收记录](archive/architectural-v1-final-visual-signoff-2026-09-13.md)。
签收限于固定离屏视觉场景，不代表碰撞、导航、动态镜头或 Campaign 性能验收。

## 冻结范围与设计判断

V1 冻结的是可复用建筑资产、连接尺度和表面语言，不是正式关卡、碰撞或一栋完整厂房。
原环境已有十件 V2 Hybrid 工业组件及动力机组、开放门架、控制柜；缺少的是它们之间的
承重、服务、围合关系。新语法是：**独立玩法墙/地面 + 薄墙壳 + 粗结构跨度 + 贴边服务带**。
开放工业战场仍为主体，建筑只提供局部压缩和功能分区。

沿用 `generate_rasterfall_props.py` 的 Builder、材质创建、八边形管体、导出与检查器。
新增几何不使用倒角堆量、复杂 PBR、磨损纹理或黄色轮廓。主体使用现有蓝灰/暗灰方向，
墙壳适度提高灰色明度；功能颜色仍由既有机组、控制柜、灯柱和编号承担。
建筑大面没有纹理，环境设备仍使用既有局部 Hybrid sign。

## Canonical kit

以下 `rf_arch_*` 均为 **CANONICAL**；registry 名称省略 `rf_`。尺寸顺序为 Blender X/Y/Z 米。
精确规格的执行所有者是生成器 `SPECS`、manifest 和 registry；渲染统计以生成/截图日志为准。

| 资产 | 设计尺寸 m | 保留理由与使用边界 |
| --- | --- | --- |
| `rf_arch_beam` | 6 × .5 × .6 | 一件粗工字梁解释柱间跨度、边跨和檐下骨架；两端必须落在承重柱/墙顶，不做悬空装饰线 |
| `rf_arch_support` | 2.4 × .8 × 2.8 | 双脚和宽膝撑解释平台/维修台承重；重复到平台两端，平台面仍归地图 surface；不是供 Tank 通行的低门 |
| `rf_arch_wall` | 4 × .24 × 4.2 | 可重复薄墙壳；连续墙脚、大面和端筋直接分区，缓解 primitive 的整块纯色；只承担视觉围合 |
| `rf_arch_doorway` | 6 × .3 × 4.2 | 4.8 m 宽、3.6 m 高的真实几何开口；门洞两肩与墙壳续接；严禁拿整块 profile AABB 当门洞碰撞 |
| `rf_arch_pipe_straight` | 2 × .62 × .62 | 两米标准干线，使设备不再依靠相邻摆放暗示连接；八边形粗管与双端法兰 |
| `rf_arch_pipe_elbow` | 1.31 × 1.31 × .62 | 平面直角换向；服务带转入端站，无需增加大量弯管型号 |
| `rf_arch_pipe_tee` | 2 × 1.31 × .62 | 主干保留连续、侧支接控制/检修设备；原型中替换一个 straight 而不是叠加在其上 |
| `rf_arch_service_panel` | 1.6 × .16 × 1.2 | 很薄的墙面检修门加三道宽服务口，提供设备与建筑的接口语义；不需要独立碰撞 |
| `rf_arch_cable_tray` | 4 × .24 × .32 | 一段覆盖一个墙壳模数的粗线槽；沿服务带连续，不能把每面墙都画成密集横线 |
| `rf_arch_floor_hatch` | 1.6 × 1.2 × .04 | 单个低矮维护盖，提供地面尺度和检修语义；放在设备带旁，不用它铺地 |

梁与墙模数承担不同任务：墙是 4 m 面板，结构为 6 m 跨度；按功能分区，而非每条墙缝都加柱。
现有 `industrial_pillar` 高 2.8 m，可直接承接梁底；`gate_frame` 继续提供较高的开放入口。
更长跨度应由同一梁 generator 的明确长度 variant 补充，不能无条件整体放大而改变梁厚。
本轮不新增长度 variant，因为两个原型不需要它。

## 连接契约

保留 Blender Z-up/-Y-forward、GLB Y-up/+Z-forward、bottom-center pivot 与 applied transforms。
GLB 真实米制，RMESH 232 units/m，registry 在展示边界换算到 512 RFU/m。
不要从模型预览 CLI 的 `front/back` 文件名推断源轴向；这些名称属于诊断相机。

现有 `pipe_module` 是**双立管设备**，并不是直干线；其管体半径 .24 m、法兰半径 .31 m、
厚 .12 m 被新管线复用。旧设备顶端有端盖，V1 不假装它已有可插拔的竖直连接口。
新件仅支持水平布管，地图 object 的 yaw 无法表达竖直转接。设备外壳上的连接是视觉服务接口，
没有流体、电力或交互模拟含义。

下表为 GLB 本地米制端口中心，法线向外。弯头/T 因 bottom-center pivot 与弯管几何中心不同，
**不能只把资产包围盒相贴**。按端口对接，m → RFU 仅在 placement 边界换算。

| 管件 | 端口中心 `(X,Y,Z)` 与出向 |
| --- | --- |
| straight | `(-1,.31,0)` → -X；`(1,.31,0)` → +X |
| elbow | `(-.655,.31,.345)` → -X；`(.345,.31,-.655)` → -Z |
| tee | `(-1,.31,.345)` → -X；`(1,.31,.345)` → +X；`(0,.31,-.655)` → -Z |

每个端口保留闭合低模端面；对接时端面互相接触，法兰不叠成第三个套环。支路末端应进入
设备外壳或明确的壁面接口，不能悬在通道中。不要将干线穿过门洞来追求“所有设备相连”。
电气线槽与机械管道是两种功能线；控制柜可以拥有管道控制接口，不把线槽说成流体连接。

墙面板正面朝 GLB +Z；落位时让正面朝维护通行带。墙体不能靠两片近乎重合的整面 shell
叠加出分区；V1 墙脚、竖筋和主体由相邻几何直接构成，减少斜视深度竞争。
连续外墙可利用背面的平色与贯通墙脚，不需背面复制完整检修装饰。

## Wall Surface V1

冻结两种用法，不另做一套纹理库：

- **Industrial shell**：中明度冷灰大面、低位维护带、稀疏竖向结构节奏；使用 wall/doorway。
- **Service wall**：同样的大面，在真实设备带加 service panel 和一条连续 cable tray。
  不是新 wall mesh，也不要求每个面板都有检修门。蓝灰检修件负责局部功能差异。

大面积连续 gameplay wall 的推荐正式实现是复用现有 flat render/surface，由 Sol 将相同
面板节奏并入地图渲染记录或批处理。RMESH 墙壳适合局部建筑和边界转折；不要在全地图既有
实心墙前再满铺一层不可见背板。facility painted wall 作为配色方向保留，暂未冻结第三种模型。

## Floor Surface V1

保留模块化基因，取消高反差棋盘：约 4 m 大板、同色板面、很浅低对比接缝；维护区用一个
较浅区域色，外场采用较暗冷灰。板材节奏只提供尺度，不让每格轮流变色。
检修盖只放在设备侧，编号/警戒边界优先复用设备和门架上的现有信息，不铺黑黄边框。

原型用现有无纹理 quad 模拟板面与下方灰色缝底，未修改地图 renderer/material 系统。
正式集成推荐把地面分区批量生成或作为便宜的 surface/material 表达；避免几百件 floor RMESH。
近共面的整片底色 overlay 在软件深度路径出现闪线，原型用有间距的缝底消除；正式地表
应直接分区/批处理，不将这一诊断做法复制为多层覆盖的全图地板。

**Heavy-duty/ramp floor** 冻结为视觉规范：延续同色大板，接缝沿坡向投影；只在上下平台边界
保留少量粗防滑分段或安全提示。坡道 top 仍由原 surface 描述；不新增一片悬浮 ramp RMESH。
坡面 shader/材质绑定及正式地图效果留给 Sol，不声称本轮已经验证全套坡道材质。

## 组合原型与验收入口

正式 Campaign 现在通过 `rasterfall.map` 的 `env_arch_*` 使用本套件：西侧是南向宽入口、
西侧连续服务带与短东侧回墙的半开放维修巷；东侧用南北双入口、东侧梁柱与设备带保留较宽
中央空地；南侧动力场只设局部后墙和开放入口。没有完整屋顶或校园构件，也不在原实心墙前
满铺墙壳。少量原设备和立柱重新落位，墙模数保持 2048 RFU、梁跨度保持 3072 RFU，
梁底由 1434 RFU 高柱/支撑承接。

这些建筑仅为视觉边界，不新增实体阻挡：原 collision、surface、region、交互和 actor spawn
保持不变，玩家及敌人仍可穿过视觉壳。若后续需要实体围合，必须另行设计左右门肩 collision
并验收导航，不能由 profile AABB 自动生成。地图 object 的 y 为相对地面的 RFU 高度；
projection 保留它，renderer 在 `-900 + y` 放置梁、管线、面板和线槽。
正式接入验收使用 `--environment-capture`，独立 `arch-*` fixture 仍仅验证资产和原型。

`arch-*` 是 process-only Visual CLI fixture，使用正常 static prop、modular Rifleman 和
真实 Common/Charger/Tank renderer；背景为独立 surface study，没有 session tick、碰撞、
AI/波次或 Campaign 改写。它验证视觉空间，不证明导航、碰撞或实战吞吐已经通过。

**A / Maintenance Bay – Service Alley**：两侧薄墙壳，门架与墙洞前后贯通，侧边梁柱形成
半封闭节奏。机组、控制柜、vent、主干与支管集中在一侧；另一侧为墙面检修与旧立管设备。
场地原型约 8 × 16 m，入口为现有 gate；中央连续可读，避免每跨加一个低横门。
单独的管件不具备建筑意义，但管线沿同一高度进入设备之后，服务带第一次拥有连续功能关系。

**B / Open Workshop – Factory Hall**：复用同一套墙壳、门洞、服务带，扩为约 14 × 20 m；
前后两出入口，中央空地，侧跨梁与贴边工位，角落用双 support 解释局部维修平台。
上方保持开放，不制作完整屋顶与小房间迷宫。它是开放式厂房骨架，不是完整封闭室内关卡。
共同零件依靠围合尺度、入口、设备带位置和结构密度形成不同用途，而不是随机旋转/调色伪装复制。

每个原型提供入口视角、正常眼高内部、远景和反向视角；family 与逐件四视角补充观察。
`arch-asset-<name>` 增加按真实包围盒取景的俯斜单件视角，尤其用于 floor_hatch 和水平管件；
脚本输出 `details.png`。原有四视图侧重旧模型构图，低矮件容易只见薄边，应以新单件视角验收。
正常距离下检修盖把手、管端细部不承担识别责任；保留它们的粗分区，不再继续加细节。
最终检修盖省去把手小几何；墙面检修件与盖板也采用相邻面分区，避免浅叠层在远距离竞争深度。
建筑颜色保持低饱和，普通感染体与两种特感的暖色/灰绿轮廓独立于背景；警示色只占设备小面。
远处首先应读到开口、结构和设备主体，而非接缝或标签。没有额外批准密集墙面线槽或整片格栅。

## 生命周期与被否决方向

- **CANONICAL**：上表全部 `rf_arch_*`；原有工业组件维持原身份。
- **EXPERIMENTAL**：目前没有另行注册的实验 mesh。竖向管接头、梁长 variant、painted wall
  和 ramp 表面实现是保留的设计方向，尚未生产，不应伪装成已完成资产。
- **RETIRED**：本轮没有完整可复用资产被退役或删除。曾否决过密横向门架、穿后门干线、
  叠层薄墙表面；这些是原型落位/错误几何迭代，已收敛到同一资产，不创建重复库存。
- **LEGACY**：保留旧 industrial V2 full-albedo/crate 对照生成路径；本轮不改变它们的发布身份。

没有为了数量制造 pipe fitting、一次性厂房、杂物或多种地板 mesh。未来退出 canonical 的资产
必须保留 generator、源输出、manifest、验收和退役理由；RETIRED 永远不等于物理删除。

## 重现、成本与交接

```sh
make app-rasterfall
python3 tools/architecture_round.py --generate --capture --deterministic
make test-asset-pipeline
build/rasterfall --logic-test
build/rasterfall --help
git diff --check
```

脚本只编排现有 Blender/importer/Visual CLI，并使用 Pillow 拼图，不引入第二套资产 IR。
首次生成构建转换器一次，后续 importer 使用已有 `--no-build`；不要让多个 Linux make 同时
操作同一个 build 目录。`--generate` 显式重建/安装当前 suite，旧探索应使用另一个 output 目录。
GLB 两次独立生成、原型两次渲染逐字节比较；Blend 自身不承诺字节确定性。

默认 `tmp/architecture-v1/` 保留源 Blend/GLB、两轮 GLB、逐件四视图、原型 BMP/PNG、
组图、import 日志与 runtime SHA256。源 GLB 和 Blend 同时保存在本地 industrial source 目录；
公开的 generator/manifest 和 RMESH 足以重建，私有目录与 tmp 不提交。
既有模型预览会自动对每件归一构图，只有组合原型提供真实尺度；不得把逐件预览当通行验证。

所有新件只有两个 flat material/primitive、零纹理；没有 TTEX 是正确结果，不是纹理遗漏。
当前静态 GLB 转换器输出 RFM2 v2，不保存 sidedness；默认双面绘制在远处薄墙上产生背面穿透。
本轮最小修复在 `rasterfall_render_static_prop()` 对这批闭合建筑 ID 启用 scoped backface culling，
`rasterfall_render_frontend_state` 持有逐次提交开关、调用后恢复；不改 importer、资产格式、深度精度
或其他模型规则。generic `--model-static-views` 不经过 registry，仍可看到旧双面路径的局限；
`arch-asset-*` 和组合原型才是本套最终 runtime 验收入口。
墙大面没有装饰 overlay，支撑不做细密桁架，地面不是独立 mesh 阵列。原型日志区分
environment commands（包括旧设备/地面）与加上角色后的总提交，记录实际单帧 raster flush；
该时间包含首次离屏配置，不能当作 Campaign FPS，更不能据此承诺高压战斗帧率。
root embedded 与 Windows package 递归资源规则覆盖新增公开 RMESH；未新增编译单元。

Sol 的优先接入顺序：先选择一个现有设施岛，把机组/柜/vent 整理为贴边服务带，接入
连续干线、壁面检修和宽入口；然后才给关联大墙/地面赋予 Surface V1。先确认固定相机的
敌人可读性和门口净空，再扩大到第二处开放工坊。每一区域分别比较同相机开启/关闭新内容的
实例、primitive、屏幕覆盖和帧时间；外场不应铺满墙壳或服务细节。
正式碰撞、平台/坡道承载、Charger/Tank 穿门与转向、Campaign 地图数据、密集尸潮性能
和 Windows package 最终发布由 Sol 完成。V1 object 仍需连续 projection index；门洞、
支架、管线和线槽不能自动按 profile AABB 生成实体碰撞。
