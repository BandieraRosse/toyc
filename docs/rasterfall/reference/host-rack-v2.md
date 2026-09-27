# Host Rack V2

> 状态：当前实现；实时数字与布局视觉验收待确认
> 所有者：Host 环境资产与只读展示
> 事实入口：`tools/blender/generate_host_rack.py`、`rasterfall_prop.c`、`assets/maps/outpost.map`

## 固定规格

CPU 与 Memory 共用 0.8 m 宽 × 1.06 m 深 × 2.2 m 高框架，约 410 × 543 × 1126 RFU。
固定六槽从底部填充；每模块高 0.26 m，槽距 140 RFU，底部独立 0.30 m utility 区，顶部独立身份区。
底面中心原点、GLB +Z 正面、232 RMESH units/m 和 512 RFU/m 沿用公共 prop 合同。

| 资产 | 职责 |
| --- | --- |
| `host_rack_frame` | 深色四柱框架、前 rail、后部 PSU 和出线口；承担整柜碰撞 |
| `host_cpu_header` / `host_memory_header` | HOST 家族标识及 CPU / COMPUTE、MEMORY / BANK 类别；粗几何字形 |
| `host_blank_panel` | 封闭的无灯空槽板，不留黑洞 |
| `host_cpu_module` | 大面积进气栅、蓝色铭牌、双银灰鳍片散热器及 socket、四个风扇框 |
| `host_memory_module` | 正面重复 bank 结构、两组共 16 根 DIMM，含插槽和颗粒 |
| `host_rack_fan_panel` | 底部固定 COOLING / POWER，两个排风区与 ready 灯，不计入资源槽 |
| `host_power_bundle` / `host_data_bundle` | 后上方电源和数据线束，接向大厅顶部设施 |

每模块侧面采用厚金属边框和深色内腔，观察开口约占侧面长度 67%、高度 50%。
当前静态 RMESH 不具备玻璃混合透明合同，因此使用开放的深色观察区域，未实现有色玻璃。
CPU 的散热器轮廓和 Memory 的薄板阵列负责结构区分；蓝绿只用于小面积识别和活动显示。
资产每件限制 6000 三角形，生成器验证尺寸、原点、材质和预算。

## 槽位与八柜排布

大厅东北角预置两排四列候选位：列中心 X=2450、3000、3550、4100 RFU，
北排 Z=3600、南排 Z=2300，两排均朝南（yaw=180）。机柜均摆正，
排间净距约 1.48 m，列间净距约 0.27 m。东两列为 Memory，东北角首先生成；
西两列为 CPU，靠大厅外侧。每类按北到南、同排东到西编号 1–4。
`tools/host_rack_layout.py` 生成地图候选 object。`attr.length` 在 CPU 使用
`100+(柜号-1)*10+槽号`，Memory 使用 `200+(柜号-1)*10+槽号`；槽号 0 表示机架附属件，
1–6 从下往上。Runtime Map 保留所有 authored 候选；展示 resolver 决定是否呈现，
未启用机柜的生成碰撞在玩法投影中关闭。已经存在的机柜仅未安装槽位使用 blank panel。

一柜对应最多六个物理核心或六个 4 GiB 内存容量槽；每类最多四柜、24 槽。
柜数分别为 `ceil(物理核心数/6)` 与 `ceil(ceil(可用物理内存 MiB/4096)/6)`，
均截到四柜。CPU 槽 `C01..C24` 对应 Windows 按处理器组、核心顺序枚举的物理核心逻辑编号；
同一核心的 SMT 逻辑处理器负载取平均。Memory 槽 `M01..M24` 每槽最多 4096 MiB，
最后一槽用真实剩余容量。主机超出 24 核或 96 GiB 时，最后一槽编号附 `+`，日志报告原始总量；
机柜只展示前 24 槽，不将超出量伪装为槽内资源。
硬件展示不进入 `toy_game` 或网络权威状态。`rasterfall_host_set_active_slots()`
保留诊断覆盖，-1 恢复硬件自动选择。

## 动态展示所有权

`rasterfall_host_activity()` 接受展示实例、显式毫秒时间及 quad callback，生成世界空间几何。
CPU 在静态 prop 绘制后消费，GPU Scene 在 layer 来源提取时消费冻结的 prop 值与帧时间，均处于 WORLD 深度域。
静态 RMESH 不因闪灯失效；GPU 活动几何复用现有 layer resource 更新路径。

- Power 常亮；独立 activity 通道每次亮 100–400 ms，周期和相位由柜位置与槽号确定。
- CPU 蓝灯条按每个物理核心的实测百分比改变长度，四个侧窗风扇持续旋转。
- Memory 已用量从 M01 向上装填；每槽八段绿灯的亮格数为
  `ceil(8 * 该槽已用 MiB / 该槽容量 MiB)`，零占用全灭。
- 每槽使用低分辨率等宽粗体点阵：静态 HOST 标题为灰白，CPU/MEMORY 类别及槽位标签用对应蓝/绿，
  动态数值为高亮浅白并右对齐，进度条位于同一显示区下方。`M` 表示 MiB。
- 外壳主体 `#111418`、边框 `#1B2026`；屏幕凹槽 `#0A1116`、数据衬底 `#0D151B`；
  数值 `#DCE8F2`、次级文字 `#8FA4B5`。CPU 主色 `#36A8FF`、暗条 `#1E6FAF`；
  Memory 主色 `#4BE38A`、暗条 `#1E8E59`。CPU 核心实测负载达到 80% 用 `#F2C14E`，
  达到 95% 用 `#FF5C5C`，仅改变该核心的数值和亮条；Memory 不应用高负载警示色。
- 实时字形采用五列七行点阵，零带斜线、一有旗脚；静态铭牌沿用粗几何点阵。
  层级为机柜标题、类别/槽位标签、数值；最高亮度留给数值，不加大面积泛光。
- 正面文字按读者面对机柜时的左右方向排列：模型局部 +Z 是正面，读者的右方是局部 -X。
  字符顺序和每个字形的列都按此方向放置；机柜旋转后仍应正读，不得镜像。
- 数字与条使用约 1 秒一次的平台采样；活动小灯和侧窗运动仍是视觉动画。
- 正常运行采用单调展示时钟；固定 capture 使用稳定的示例指标与帧号 × 16 ms，
  不把截图中的示例百分比当作实机采样证据。

## 大厅与验证

模块、铭牌及线缆均无独立碰撞。现有八个候选位置及朝向以上述地图坐标为准。

```powershell
python tools/host_rack_round.py --blender 'E:\Blender 5.2\blender.exe'
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 package
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 test
```

公开九件 RMESH 位于 `assets/models/props/host/`，manifest 位于 `tools/assets/manifests/props/host/`；
源 GLB 和 Blend 保存在私有源目录。根 Makefile 递归资产依赖和 Windows package 递归复制覆盖新增文件。
CPU `--environment-capture` 包含 `host-racks`、`host-side`；GPU 可用同名 `--gpu-normal-scene` 视角，
搭配 `--map rasterfall/assets/maps/outpost.map --renderer gpu-scene --gpu-frame-capture` 和不同 capture 帧。
实机运行按 [Windows Native](../guides/windows-native.md) 等待真实进程退出，检查日志和返回码。

视觉冻结要求：不依赖颜色文字仍能区分硬件结构；中距离读出 CPU/MEMORY；观察 2–3 秒能感到运行；
整体保持服务器机柜外观。静态截图只能验证其中一部分，不能代替动态实机观察。
