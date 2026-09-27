# Host Rack V2

> 状态：当前实现；视觉冻结待用户验收
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

## 槽位接口与硬件边界

地图模块 `attr.length=1..6` 表示从下往上的视觉槽号；零或范围外作为独立模型显示。
`rasterfall_host_bay_asset(asset, bay, active_slots)` 只按活动槽数选择原模块或 blank panel。
`rasterfall_host_set_active_slots(cpu_slots, memory_slots)` 接受两个独立的 0..6 值；-1 恢复六槽展示预览。
所有合法占用数均由逻辑回归覆盖；机架、身份区、utility 和线缆不参与计数。

默认两柜六槽全开用于 V2 美术审阅。平台物理核心与内存查询继续保留，并首次打印到 HOST-RACK 日志，
但不再使用 V1 的“一个核心/4 GiB 一槽”换算。真实 hardware → active slot count 的量化策略待多机测试后确定。
本机硬件及展示覆盖不进入 `toy_game`、网络权威状态、Runtime Map 或碰撞/导航。
CPU 和 Scene snapshot 共用 `rasterfall_prop_presented_asset()`；空槽不产生活动几何。

## 动态展示所有权

`rasterfall_host_activity()` 接受展示实例、显式毫秒时间及 quad callback，生成少量世界空间几何。
CPU 在静态 prop 绘制后消费，GPU Scene 在 layer 来源提取时消费冻结的 prop 值与帧时间，均处于 WORLD 深度域。
静态 RMESH 不因闪灯失效；GPU 活动几何复用现有 layer resource 更新路径。

- Power 常亮；独立 activity 通道每次亮 100–400 ms，周期和相位由柜位置与槽号确定。
- CPU 蓝灯条平滑改变长度，四个侧窗风扇持续旋转。
- Memory 使用八段绿灯条及分组弱 bank 闪光。
- 展示是合成运行状态，不声称代表实时 CPU 利用率或内存负载。
- 正常运行采用单调展示时钟；GPU 固定 capture 使用帧号 × 16 ms，可复现不同时间的画面。

## 大厅与验证

CPU 原点 `(2700, 0, 3400)`、Memory `(3650, 0, 3400)` RFU，保持 198° 朝向（偏转 18°），
入口侧可同时看到正面与侧窗。模块、铭牌及线缆均无独立碰撞。

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
