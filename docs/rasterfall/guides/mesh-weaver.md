# 网格编织机实验与诊断

> 状态：当前
> 所有者：Rasterfall Windows native lane
> 最近核对：2026-10-04

本指南配合[普通机合同](../reference/mesh-weaver.md)及
[Windows native 工作流](windows-native.md)使用。先从维护入口读取相关所有权，再修改资产或制造规则。

## 现场操作

实验单元位于前哨站五枪循环区南侧。靠近机器侧终端或相邻 RF1 按 E，
选择蓝图后执行“开始制造”。面板显示当前任务、所选蓝图、实际几何统计、工作量、
成型能量和按当前供给估算的时间。方向键或鼠标操作，Esc 关闭；面板打开时制造继续。

电源和 C1 开关可检验暂停续作，关闭 X1 可检验 CPU 独立吞吐。
这些操作不会补满实验电源；控制台显示有限余量。完成后靠近并面向出料托盘，
按 E 领取。领取替换对应武器槽，初始弹药只有已计价的标准弹匣；后续使用行动部补给。
离开观察范围不删除任务；换图、重开或退出不保存本实验任务。

## 资产与蓝图

- `python tools/mesh_weaver_assets.py --help`：生成和审计机器部件、清单与布局头。
- `python tools/mesh_weaver_blueprints.py --check`：校验提交的测量目录与运行资产、物理适配。
- `python tools/test_mesh_weaver_blueprints.py`：完整资产统计、缓存失效和体积边界回归。
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_mesh_weaver_geometry.ps1`：原生共享几何审计；真实贴面、遮挡、八头覆盖、孔口轴线、连续性和托盘接触。
- `python tools/mesh_weaver_lab.py --help`：复现前哨站独立实验地块。

源 GLB/Blend 在私有资产目录；公开 RMESH、生成器、清单和测量目录进入版本控制。
缺少私有源时应使用已验证缓存核对运行资产，不能无声重算一份假体积。
新增资产后 `build` 本身不保证运行目录已更新；`test` 或 `run` 的 stage 步骤会同步资源。

## 原生验证

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 test
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_mesh_weaver.ps1 -Blueprint idle -View quarter
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_mesh_weaver.ps1 -Blueprint ak -TimeMs 4500 -View gun
```

脚本参数以 `Get-Help tools/gpu_mesh_weaver.ps1 -Full` 及脚本校验为准。
定向镜头 `--gpu-normal-scene mesh-weaver 0` 使用同一正式地图和 Scene 提交。
脚本保存 native 日志、原始 Scene PPM、可执行文件哈希和参数；退出码、
`bridges=0`、`mixed_execute=0` 和实际截图必须同时成立。
审阅 idle、校准、早中晚成型、交付、待取与暂停，并覆盖前、后、斜上及发射头近景。
检查四柱八头、三瓣连接、光孔对齐、成品比例、真实三角面和托盘位置。

带捕获和 `frame-audit` 的帧包含诊断成本，不能直接作为正常帧性能结论。
性能对照需要相同镜头、分辨率、冷启动预热和实际 native present，分别测量
无机器、空闲、制造状态；记录 CPU 准备、GPU 时间、上传量与 draw 数。
逻辑回归负责账本、步长、暂停和单次领取；实机观察负责材质、机构与交互可读性。

`tools/gpu_mesh_weaver_interaction.ps1` 在正常时钟中通过真实 Windows/SDL 按键操作控制台、
断电恢复、领取和 Enter 射击。`MESH-WEAVER-INTERACTION` 仅作只读观测，不发送制造命令，
脚本保存输入记录、状态、原生 GPU 截图与退出码。截图请求只读回当时实际呈现的冻结帧，
不覆盖相机、时间或制造状态；只有请求帧允许 `readback=1`。`-NoScreenshots` 可仅检查输入与账本。
必须与其他 GPU 验证串行运行。

`tools/gpu_mesh_weaver_motion.ps1` 记录完整制造动作：先待机，再通过固定步诊断入口启动真实任务，
从同一 GPU 提交取得连续 PPM，保存每帧制造状态、帧号及哈希。默认生成可播放的 WebP，
需要 Python Pillow；`-SkipPreview` 保留原始帧与元数据。图像按真实 16 ms 逻辑采样间隔编码，
没有插帧或概念图。它包含显式读回，不能用录像运行的墙钟耗时推断性能。

`tools/gpu_mesh_weaver_perf.ps1` 做三状态正常呈现对照，使用独立临时地图移除机器作为 absent 基线，
保留相同地块、RF1、电源、镜头和天空。active 在预热中开始真实 AK 任务，进入采样前已有动态资源；
如果任务提前结束或暂停，样本无效。报告分别保存整帧、GPU、准备阶段、绘制数和上传量，
多轮交替运行，不能把单次差值当作全园区保证。
