# 前哨站实验区合同

> 状态：当前
> 所有者：Game Runtime 展示控制、session 基准世界生命周期
> 事实入口：`src/rf_experiment_labs.inc`、`src/rf_performance_lab.inc`、`assets/maps/outpost.map`

## 登记与展示

固定战斗实验复用南排最西侧空性能场，新增 `combat_control_terminal` 和 `combat_result_terminal` 作为同一终端交互的专用入口。
它由 `rf_combat_lab.inc` 管理真实 actor/enemy 实验生命周期，与下表纯视觉展示分开；战斗和性能测试不能同时使用场地。
场地 region 仍是 `perf_low_area`，坐标由 Runtime Map 读取；具体操作、五类预设与结构化结果见[战斗实验场](../guides/combat-lab.md)。

每个展示区登记一种用途、一份地图 surface、一个控制终端、一个诊断镜头和后端能力。
`rf_experiment_labs.inc` 是控制登记表；Runtime Map 是地面边界和终端位置的事实来源，登记表不复制坐标。

| 用途 | 地图 surface | 控制对象 | 诊断镜头 | 后端 |
| --- | --- | --- | --- | --- |
| 感染体姿态 | `character_lab` | `lab_showcase_button` | `character-lab` | CPU / Scene |
| 感染体往返 | `walk_lab` | `walk_lab_showcase_button` | `walk-lab` | CPU / Scene |
| AI 动作与装备组合 | `actor_actions_lab` | `actor_actions_button` | `actor-actions-lab` / `equipment-lab` | CPU / Scene |
| AI 往返 | `actor_walk_lab` | `actor_walk_button` | `actor-walk-lab` | CPU / Scene |
| RF 模型 | `rf_model_lab` | `rf_model_lab_button` | `model-lab` | Scene |
| GPU 光照 | `rf_light_lab` | `rf_light_lab_button` | `lighting-lab` | Scene |
| RF 电子产品 | `rf_electronics_lab` | `rf_electronics_lab_button` | `electronics-lab` / `electronics-case` | CPU / Scene |
| 三模型持枪循环 | `rifle_cycle_lab` | `rifle_cycle_lab_button` | `rifle-cycle-lab` | Scene |

普通启动全部关闭；靠近终端按 E 切换，普通展示提示使用 ENABLE / DISABLE EXHIBIT。
电子产品控制台按 E 循环关闭、600、1200、1800 RPM；屏幕和入口牌显示当前档位。
CPU 对 Scene 专属区显示 GPU SCENE REQUIRED，不修改其请求。
显式诊断镜头仅开启对应展区。离开前哨站或重载普通世界清空展示请求。

AI 动作区的 Humanoid 台位保留步枪手站立、行走和射击，随后为突击、侦察、医疗、工程、重装、
普通枪手与精英枪手的实际装备组合，台牌标注身份。它们使用已有 idle/walk/fire 动作，不提供未创作动作。
`equipment-lab` 是同一区域的近景镜头；`RF_GPU_EQUIPMENT_STATION=0..9` 选择上述台位，
`RF_GPU_EQUIPMENT_VIEW=front|quarter|side|back` 选择视角。固定截图或 fixed-tick 运行固定镜头，
普通启动仍可移动观察。`tools/gpu_equipment_review.ps1` 记录原生退出码、逐帧来源、截图和资源哈希。

RF 模型区东侧的 `AI RIFLE CYCLE` 以三条同步路线展示 Block、Humanoid 和 RF-C01，全部持 AK。
每一端先静止，轮流展示平视、左右偏转与上下仰俯的瞄准和射击；随后低位持枪边走边向多个角度射击，
到端点平滑转身、停步瞄准，再沿原路返回，持续循环。试样屏显示模型、当前阶段及 pitch/yaw。
普通入口通过东北终端按 E 启停；暂停与性能隔离冻结循环，不产生实际弹丸、伤害或 AI 目标。
区域片段由 `tools/rifle_cycle_lab.py` 生成；固定镜头与时间采样见[Scene 工作流](../guides/gpu-scene-fixture.md#三模型持枪循环区)。

Runtime 持有用户请求和各区展示时钟，按世界、后端与性能测试独占状态生成有效开关。
暂停、关闭或性能隔离期间时钟不推进；渲染只消费有效开关与时钟。
关闭的展示不运行展示动画或生成动态来源。恢复时保留请求，步行展示可从参考位置重新开始。
CPU 与 Scene 共用角色展示值，实验展示不创建玩法 actor。
GPU 球体保持 GPU owner 缓存，光照区关闭后不提交球体和实验灯；最终按 graphics 生命周期释放。

## 空间与导视

前哨站采用[园区 V3 总平面](outpost-lab-layout-v3.md)：全部实验地块位于测试庭院以南，
原四列三排以 6 m 道路连接，第一排东侧另接三模型持枪循环地块；性能控制与结果终端位于第二排南侧路边。
展示诊断镜头与区域内容一样按 Runtime Map 原点平移，不依赖旧世界坐标。

实验区用 `lab` 复合定义声明中心原点、宽深、用途和围合类型；区内记录以 `attr.lab=<区域ID>`
绑定，X/Z 为局部坐标。地面、边线、物件、试样标签和展示源随原点平移。八个展示区与四个性能场
均已迁移，稳定 surface/object/render ID 保留。普通展示源以区域中心为局部原点；性能场出生空间
由区域边界向内收缩，观察位在西界外侧，整体移动区域时一同平移并保持当前 workload 观察距离。

| 用途 `category` | 颜色 | 图形 |
| --- | --- | --- |
| `model` 模型 | 蓝紫 | 菱形 |
| `animation` 姿态、动作、往返 | 青绿 | 阶梯箭头 |
| `lighting` 光照、材质 | 琥珀 | 光线十字 |
| `performance` 性能 | 橙色 | 柱状图 |

`enclosure=open` 表示开放地面，`backdrop` 表示光照观察背景墙，`walled` 表示实体围墙场地。
分类只保存空间意图，实际可见墙和 collision 分别声明；类别不隐式改变碰撞。
新标准场地由 [区域生成工具](../../../tools/experiment_lab.py)生成地面、64 RFU 边线、北侧入口、
东北控制位、南侧标题、入口大屏和试样小屏。默认展示位置为局部 `(0,0)`；往返路径、台位偏移
由各展示源拥有。既有光照场和性能场保留专用尺寸，避免改变观察、遮挡和测试 workload。

导视和试样标签使用共享程序几何投影终端：完整落地机壳、保护脚、内凹光学槽、开放角标、分类图形和独立字形。
CPU 与 Scene 共用几何发射器；字形后方增加深色半透明背景，保留后方试样的透视。
底座光学槽到投影间有淡色光锥/扇形光束与细射线，亮度缓慢向上流动并轻微变化。
信标光束跟随投影浮动，大标题、试样小标题与可更新内容屏均自动带光束和背景；
物理像素屏与检修玻璃不套用此效果。光束和背景只参与展示遮挡，无碰撞或实际照明。
标题大屏用于中距离辨认，
低位小屏标注模型、动作或材质。控制计算机与入口大屏消费同一展示状态，分别使用富文本与简短单行 channel。
按 E 保持统一启停；普通入口默认关闭，暂停和性能隔离显示 `SUSPENDED`，不支持的后端显示
`GPU REQUIRED`。性能屏显示运行状态和最近结果均值，详细结果仍在原交互页面中。

`render kind=sign` 的 `attr.style=2` 为大投影、`3` 为试样小投影、`4` 为可更新内容屏；
可更新屏按宽度选择基座尺寸。`attr.texture_u=1..4` 选择上述分类图形，`color` 为图形与字形色。
`attr.channel` 是显示身份，与 object 交互身份分离。Runtime 调用
`rasterfall_render_terminal_set(channel,text)` 更新展示内容；每帧复制到只读绘制值。
Scene 的可更新屏走 WORLD 动态几何，文字更新不重建整个静态世界；标题与标签的机壳、图案保留网格缓存。
全部投影字体使用同一提亮规则：原色向白色混合 45%，Scene 字形自发光，保留类别色调；
背景生成规则、透明度和光束颜色不受字体强化影响。
这套组件当前显示 ASCII 文字和四种几何图案，不是任意纹理/视频终端。

## 标准地块铺装与角标

标准模板区分实际工作区和规划地块：`--width/--depth` 定义前者，
`--plot-width/--plot-depth` 定义后者（默认 32×20 m）。工作区尺寸仍按用途选择；
模板校验工作区及四角投影器能够放入规划地块，不拉伸试样或扩大实验规则的边界。
退让空间由四侧互不重叠的填充板覆盖；已有入口、观察支带和服务口袋保留独立铺装。
预留地块也铺填充板。模板为规划地块显式声明零高度 surface 与不可见、非阻挡的 walkable flat。
正式前哨站园区已全部铺满，主园区使用连续支撑，东侧新增地块用重叠的不可见支撑接入旧东路，避免狭窄填充和道路接缝
无法完整容纳玩家足迹；不得把这一支撑延伸到园区外或带孔洞、高差的地形。world 范围不提供
默认地面，仅有 render 的填充不能作为空中移动的支撑。围墙、安全区和可见铺装仍独立声明。

地板保留原通用样式，另有冷蓝灰实验板、石墨道路板和浅灰填充板。
实验板以宽接缝呈现检修模块，道路以浅色边带划定净宽，填充板采用低对比接缝。
三者共享原有单层地面分区与 CPU/Scene 几何；不叠加共面贴片，也不依赖新纹理资源。
每个实际工作区四角外侧放置低位投影信标，底座位于地块内；阶梯底座、顶部光学槽与
两个正交悬浮菱形框标识工作边界，颜色沿用区域类别。投影终端样式 7 共用 CPU/Scene 几何，
从东西及南北道路均可辨认。底座独立声明不可见、阻挡且可站立的粗盒，顶部为地面以上
292 RFU；玩家可跳上底座，投影不声明碰撞。交叉菱形框绕竖轴每 8 秒旋转一周，
以 3.2 秒周期上下浮动 ±48 RFU；动画只进入展示，不创建动态光源或玩法实体。
性能场的西侧观察带、围墙和出生范围保留原合同。

## 模块化控制计算机

展示开关与电子产品整机使用 `assembly` 组合：独立 `lab_computer_stand` 支架、`case` 机壳、`board` 主板、
`cpu`、两根 `memory`、`compute` 计算卡、`cooling` 散热器、`display` 显示器和 `keyboard` 键盘，
另有独立参数化透明检修窗和动态屏幕面。RF 第一代采用冷灰金属、石墨结构、深绿 PCB、青色功能件与铜色触点。
主板沿机箱内侧竖装，元件朝桌外的 -X 透明侧板；背面 -Z 为通风、主板接口、扩展卡挡板与电源插口。
支架 object 保留旧控制 ID 与交互锚点，拥有整机简化碰撞；其他部件不另加碰撞。
组合语法与局部高度换算见[地图格式](map-format.md#机器组件组合)。

### RF 第一代装配合同

尺寸为 RFU；组件生成时以“板宽、向上、离板厚度”创作，再统一转为面向 -X。
主板安装基准为 assembly 局部 `(-240,680,-30)`，机壳基准 `(-320,578,-30)`。
主板以基板底边为高度零点；CPU、内存、散热器、计算卡也各有底部安装原点，装配位置由
`tools/lab_computer.py` 唯一维护，不在运行时添加逐件位置补偿。

| 产品 | 尺寸与接口 | 识别细节 |
| --- | --- | --- |
| RF B1 主板 | 260×300×6 基板；82×82 C1 插座、双 152 长 M1 槽、170 长 X1 槽 | 六处安装柱、扣具、供电相、电容、芯片组散热片、铜走线和后 I/O |
| RF C1 CPU | 64×64 基板；56×56 金属顶盖 | 分层封装、金属散热盖、方向标记、C1 标识与边缘元件 |
| RF M1 内存 | 144 长 PCB，34 离板宽；同型号双条安装 | 两面存储芯片、金手指、端部卡扣和青色顶部散热脊 |
| RF X1 计算卡 | 224 主体板长，向后延伸至挡板；插入 B1 下部槽 | 双开放七叶风扇、铜热管、鳍片、背板、辅助供电座、RF X1 标识 |
| C1 配套散热器 | 144 高支架，CPU 接触底座与侧向风扇 | 铜底座、四条热管、独立鳍片、开放扇框和紧固件 |

CPU 源 GLB 使用四倍创作尺寸，assembly 的 `scale=250` 恢复上述尺寸，以避免静态 importer 的微型模型
量化分支；其余组件 `scale=1000`。所有公开 RMESH 保持 232 units/metre，加载、内嵌和 package 沿用递归资源规则。
这是原创近距离检修展示资产，允许超过普通环境 prop 的低模预算；上限由生成器断言约束，不扩展到普通场景家具。
型号铭牌采用紧凑字符、独立底牌及字符间距；B1 具备插座至内存、芯片组与扩展槽的分组铜走线及过孔。
型号为游戏内产品设计名称，未给出未经实现的计算性能或真实硬件兼容性承诺。

### 产品展区、风扇与状态灯

`tools/rf_electronics_lab.py` 生产独立产品展区：位于园区第一排第三列，北侧入口连接公共横路。
四个实体展台陈列 C1（6 倍）、M1（3 倍）、B1（2.5 倍）与 X1（3 倍）；标签明确比例，
另有原尺寸完整工作站和东北控制台。它们均复用同一组资产；实验开关控制运行状态，关闭时静态产品仍可查看。

`lab_computer` 组件的 `attr.length=1..8` 是展示组，按感染体姿态、感染体往返、AI 动作、AI 往返、
RF 模型、GPU 光照、RF 电子产品、三模型持枪循环排序；0 是未绑定的静止预览。它不再表示物理尺寸；其他资产的 length 语义不变。
组内机箱、计算卡与散热器共享供电、时钟和连续相位。普通实验计算机随所属实验开关运行，
转速采用电子展区选定的全局档位，默认 600 RPM。电子展区关闭会停止该区设备，其余已开启实验仍保持运行。

Runtime 积分相位，切换档位不跳回初始角度；暂停或隔离时不累计停顿时间，恢复时继续。
每个 CPU 散热器一个七叶转子，每张 X1 两个七叶转子；静态模型不再包含重复叶片。
机箱前面板和外侧下沿提供绿色电源灯、蓝色与琥珀色活动灯，活动灯使用不同周期和实例相位闪烁。
这些灯参考 Host 的外观与共享几何方式，表示合成展示活动，不读取或冒充真实 CPU/内存负载。

`sign style=5` 是不透明机器屏幕；`texture_u/texture_v` 指定逻辑宽高（各 1–1024），
物理宽高必须是逻辑宽高的正整数倍。当前计算机为 512×320 RFU / 256×160 像素，即每像素 2×2 RFU。
字形是世界表面的像素分区，随距离、透视与遮挡自然变化；字符增加时只裁切，不缩小字号。
可改用例如 1280×800 RFU / 640×400 像素的机器屏幕，无需改变字体或交互实现。

内容通过 `rasterfall_render_terminal_set(channel,text)` 发布，帧冻结时复制。当前每 channel 最多 255 字节（地图静态属性最多 95 字节）；
ASCII、换行及 ESC 指令支持大小和样式：`ESC 1/2/3` 为 1/2/3 倍字形，`ESC B/N` 为粗体/普通，
`ESC A/M/F` 为分类色/次要色/浅色。字库沿用 RF 8×16 点阵，像素发射器位于
`render/rasterfall_machine_screen.h`。此接口可承接输入系统提供的字符串；本次实验区仅 E 启停，未增加键盘输入模式。
屏幕 channel 为 `<surface>_computer`，入口大屏继续用 `<surface>`。

`sign style=6` 是独立的淡青透明检修面，alpha=42/255，CPU 和 Scene 均执行深度测试且不写透明深度。
窗面支持 Z 固定的 X/Y 平面，或 X 固定的 Z/Y 平面；必须恰有一个水平轴跨度为零。
屏幕及窗面均为单层平面，机壳与边框由独立模型提供；透明窗进入 TRANSPARENT，屏幕进入动态 WORLD，
静态部件复用原有 RMESH 缓存。投影标签保留样式 2/3/4，不套用机器屏幕的固定像素规则。

重建与预览：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 asset-tools
python tools/lab_computer.py --build-assets --blender 'E:\Blender 5.2\blender.exe'
python tools/lab_computer.py --id sample_button --x 4000 --z 4000 --lab sample_area --channel sample --output tmp/sample_computer.map
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --renderer gpu-scene --map rasterfall/assets/maps/outpost.map --gpu-normal-scene lab-computer 0
```

`lab-computer` 取整机关闭视角，`lab-computer-close` 取开启状态的近屏视角，均从 Runtime Map 的控制 object
求位置。搭配 `--gpu-frame-capture <绝对路径>` 固定镜头并输出原生 GPU 的 `<路径>.scene.ppm`。普通自由游玩仍通过 E 切换。
`lab-computer-side` 检查透明侧板及 RF 第一代内部件，`lab-computer-rear` 检查后部通风和接口；两者保持展示关闭。
`electronics-lab` 为开启状态的展区总览，`electronics-case` 为运行整机侧面；固定 capture 使用 16 ms 帧时钟。
相同帧输出可复现，不同帧用于检查叶片和灯状态；旋转速度与暂停契约另由逻辑回归覆盖。
生成器位于 `tools/blender/generate_lab_computer.py`，组合生产者为 `tools/lab_computer.py`；
GLB/Blend 留在私有 source，公开 RMESH 与 manifest 随仓库维护，无外部模型或纹理依赖。

普通角色区使用 20×18 m 开放地面、64 RFU 地面边线、6 m 连接通路和东北侧独立终端。
surface、collision 和可见绘制分别声明；边线与内部地面不重叠。
光照区因材质球、遮挡柱和移动灯轨道采用约 24×18.85 m 专用地面，沿用边线和终端合同。
性能场保留约 26.37×17.58 m 的实体矮墙场地，避免改变已用 workload 的观察距离和出生空间。
区域标题说明用途；试样标签说明类型与参数，地图对象 ID 保持稳定。

## 性能独占与基准世界

性能控制和结果终端仍位于前哨站。默认 ISOLATED 经 `rf_game_request_world()` 加载专用地图，
完成或 Esc 取消后经同一 session 生命周期重载前哨站，恢复本地玩家状态、位置、视角、种子和展示请求。
前哨站其他 session 内容重新初始化；这不是整局存档恢复。测试不在联机中开放。
地图加载或提交失败传播错误；加载失败先尝试恢复前哨站，无法恢复则退出错误路径。
进程关闭只退休资源，不为退出额外加载地图。

| 配置 | 基准地图 identity | 敌人 | crate |
| --- | --- | --- | --- |
| 基础 | `performance_empty` | 0 | 0 |
| 敌人 | `performance_empty` | 64 | 0 |
| 组件 | `performance_components` | 0 | 24 |
| 复合 | `performance_components` | 64 | 24 |

两份地图的基础地面、观察点、边界与出生区域一致，组件版只增加 crate 对象及其组件碰撞。
出生区域另有同边界的 floor 颜色记录，供兼容投影使用；分区地板合成为一个平面，不产生重叠面。
空内容文件不启用 Null、队友、旗帜或其他展示。敌人使用正式 AI、导航、动画和 Scene 来源。
观察点在安全区，测试不锁血、不关闭敌人 AI；固定站位与零玩家命令延续原测试规则。

OUTPOST 使用原四个场地作环境实测，背景静态几何和全局碰撞扫描仍计入成本。
两种正式口径都停用非测试展示、手电筒和动态灯，使用默认太阳、环境填充与三张太阳阴影图；
测试隐藏 viewmodel，保留测试 UI。材质与过滤使用当前请求，报告中记录。

FULL SCENE 是保留背景的第三种正式口径，原四种负载仍在其场地生成，其他展示、动态灯、天空与 viewmodel
继续正常渲染。它不启用性能独占，不额外隐藏物体或改变背景的可见距离。
全景巡检使用该口径，分“当前展示”和“全部展示”两种预设，五个观察点各预热 2 秒、采样 4 秒。
全开预设临时设置全部展示和 600 RPM，结束/取消恢复原展示请求和转速；背景时钟按正常时间推进。
测试保留原世界，完成后恢复本地玩家与视角。逐点结果必须完整，不能用容易的视角掩盖重负载视角。

正常体验保留 120 FPS 提交上限；UNCAPPED 仅移除测试期间的主动节流，实际 swapchain present 模式仍记录。
结果比较必须固定 scope、节流、图形设置、build、资产、分辨率、设备和驱动。
主绘制项与实际阴影绘制数分开报告；GPU 均值只除以有效 GPU 时间样本数。
敌人数量增加或下降、窗口大小/present 模式/灯数/阴影图数量变化、采样容量耗尽、取消测试均使结果无效。
FULL SCENE 允许正常动态灯和视角带来的灯/阴影图数量变化，但展示请求变化仍使结果无效。
GPU 缺少有效时间样本时该均值无可用证据，自动采样脚本拒绝签收。

诊断 `RF_PERF_LAB_INTERFERENCE=1` 仅在 OUTPOST 生效：保留用户展示请求并开启光照区，
恢复地图灯与瞬时灯，供同包背景干扰 A/B 使用。该结果不能混入正式隔离基线。
运行入口、计时与采样见[性能诊断](../guides/rendering-performance.md#前哨站游戏内性能实验场)。
