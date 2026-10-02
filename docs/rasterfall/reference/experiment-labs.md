# 前哨站实验区合同

> 状态：当前
> 所有者：Game Runtime 展示控制、session 基准世界生命周期
> 事实入口：`src/rf_experiment_labs.inc`、`src/rf_performance_lab.inc`、`assets/maps/outpost.map`

## 登记与展示

每个展示区登记一种用途、一份地图 surface、一个控制终端、一个诊断镜头和后端能力。
`rf_experiment_labs.inc` 是控制登记表；Runtime Map 是地面边界和终端位置的事实来源，登记表不复制坐标。

| 用途 | 地图 surface | 控制对象 | 诊断镜头 | 后端 |
| --- | --- | --- | --- | --- |
| 感染体姿态 | `character_lab` | `lab_showcase_button` | `character-lab` | CPU / Scene |
| 感染体往返 | `walk_lab` | `walk_lab_showcase_button` | `walk-lab` | CPU / Scene |
| AI 动作 | `actor_actions_lab` | `actor_actions_button` | `actor-actions-lab` | CPU / Scene |
| AI 往返 | `actor_walk_lab` | `actor_walk_button` | `actor-walk-lab` | CPU / Scene |
| RF 模型 | `rf_model_lab` | `rf_model_lab_button` | `model-lab` | Scene |
| GPU 光照 | `rf_light_lab` | `rf_light_lab_button` | `lighting-lab` | Scene |

普通启动全部关闭；靠近终端按 E 切换，提示使用统一 ENABLE / DISABLE EXHIBIT。
CPU 对 Scene 专属区显示 GPU SCENE REQUIRED，不修改其请求。
显式诊断镜头仅开启对应展区。离开前哨站或重载普通世界清空展示请求。

Runtime 持有用户请求和各区展示时钟，按世界、后端与性能测试独占状态生成有效开关。
暂停、关闭或性能隔离期间时钟不推进；渲染只消费有效开关与时钟。
关闭的展示不运行展示动画或生成动态来源。恢复时保留请求，步行展示可从参考位置重新开始。
CPU 与 Scene 共用角色展示值，实验展示不创建玩法 actor。
GPU 球体保持 GPU owner 缓存，光照区关闭后不提交球体和实验灯；最终按 graphics 生命周期释放。

## 空间与导视

实验区用 `lab` 复合定义声明中心原点、宽深、用途和围合类型；区内记录以 `attr.lab=<区域ID>`
绑定，X/Z 为局部坐标。地面、边线、物件、试样标签和展示源随原点平移。六个展示区与四个性能场
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

导视和试样标签使用共享程序几何投影终端：低矮投影基座、悬浮边框、分类图形和独立字形。
CPU 与 Scene 共用几何发射器；文字间隙为空，不遮住后方试样。标题大屏用于中距离辨认，
低位小屏标注模型、动作或材质。控制终端旁的小屏与入口大屏通过同一 `channel` 显示实时状态。
按 E 保持统一启停；普通入口默认关闭，暂停和性能隔离显示 `SUSPENDED`，不支持的后端显示
`GPU REQUIRED`。性能屏显示运行状态和最近结果均值，详细结果仍在原交互页面中。

`render kind=sign` 的 `attr.style=2` 为大投影、`3` 为试样小投影、`4` 为可更新内容屏；
可更新屏按宽度选择基座尺寸。`attr.texture_u=1..4` 选择上述分类图形，`color` 为图形与字形色。
`attr.channel` 是显示身份，与 object 交互身份分离。Runtime 调用
`rasterfall_render_terminal_set(channel,text)` 更新展示内容；每帧复制到只读绘制值。
Scene 的可更新屏走 WORLD 动态几何，文字更新不重建整个静态世界；静态标题与标签保留网格缓存。
这套组件当前显示 ASCII 文字和四种几何图案，不是任意纹理/视频终端。

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

正常体验保留 120 FPS 提交上限；UNCAPPED 仅移除测试期间的主动节流，实际 swapchain present 模式仍记录。
结果比较必须固定 scope、节流、图形设置、build、资产、分辨率、设备和驱动。
主绘制项与实际阴影绘制数分开报告；GPU 均值只除以有效 GPU 时间样本数。
敌人数量增加或下降、窗口大小/present 模式/灯数/阴影图数量变化、采样容量耗尽、取消测试均使结果无效。
GPU 缺少有效时间样本时该均值无可用证据，自动采样脚本拒绝签收。

诊断 `RF_PERF_LAB_INTERFERENCE=1` 仅在 OUTPOST 生效：保留用户展示请求并开启光照区，
恢复地图灯与瞬时灯，供同包背景干扰 A/B 使用。该结果不能混入正式隔离基线。
运行入口、计时与采样见[性能诊断](../guides/rendering-performance.md#前哨站游戏内性能实验场)。
