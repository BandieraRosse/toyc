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

普通启动也可步行测试：从前哨站南出口进入实验园区，沿北侧道路向东走到五枪循环区，
沿该区东侧道路向南，再由南侧横路进入编织机地块。道路、填充铺装和工作板都有独立玩法支撑面。

机器声音属于只读表现：附近可听到启动、低强度工作循环、暂停与完成反馈；离开范围后静音，
再次靠近不会补播已经发生的提示。控制台打开时制造及声音继续，全局暂停使工作循环淡出。
Runtime 提交状态与左右增益，唯一音频线程混合独立机器声部，不占用八个战斗音效声部。
原创 PCM 按实际输出采样率生成；换图和音频关闭会清理旧任务声音，不写入 Game 或网络快照。

要直接进入机器前方自由操作，在新的 PowerShell 窗口运行：

```powershell
$env:RF_WEAVER_VIEW='interaction'
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --skip-boot --renderer gpu-scene --gpu-normal-scene mesh-weaver 0
```

这是正常时钟与输入入口，不会自动开始制造；E 打开终端，方向键选择，Enter 执行，Esc 关闭。
该入口保留前哨站地图，不再切到旧战役诊断地图。
M 切换 RTS，默认选中玩家；滚轮缩放、WASD 平移，再按 M 返回第一人称。
Y 锁定玩家居中，WASD 平移解除锁定；滚轮缩放保持锁定。
T 进入传送选点，左键点击可站立地面立即传送玩家，成功后退出选点；再次按 T 取消。
被设备阻挡、没有真实地面支撑或不可站立的位置会拒绝传送，保留选点模式供重试。

## 资产与蓝图

- `python tools/mesh_weaver_assets.py --help`：生成和审计机器部件、清单与布局头。
- `python tools/mesh_weaver_service_links.py`：单独生成实验地块的供电线、RF1 数据线和端接件；`--audit-only` 核对量化面、真实插口接触和走线间隙。
- `python tools/mesh_weaver_blueprints.py --check`：校验提交的测量目录与运行资产、物理适配。
- `python tools/test_mesh_weaver_blueprints.py`：完整资产统计、缓存失效和体积边界回归。
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_mesh_weaver_geometry.ps1`：原生共享几何审计；真实贴面、遮挡、八头覆盖、孔口轴线、连续性和托盘接触。
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_mesh_weaver_gpu_cache.ps1`：不打开 GPU 的原生缓存审计；真实顶点来源、稳定抽样与活跃窗口、刚性绑定、超预算降级和完整实体保留。
- `python tools/mesh_weaver_lab.py --help`：复现前哨站独立实验地块。

源 GLB/Blend 在私有资产目录；公开 RMESH、生成器、清单和测量目录进入版本控制。
已有新版原生资产工具后，`python tools/mesh_weaver_assets.py` 完整重建总装 Blend、九件 GLB/RMESH、
布局和机械头，并核对量化几何、承托点及工作角域。作者色为 sRGB；生成器先转为线性 GLB 因子，
导入器按标准 sRGB 编码运行色。单件迭代结束后应执行这条完整命令，保持总装源与运行部件一致。
`python tools/mesh_weaver_assets.py --part tray` 仅重建托盘，并保留其他运行部件与机械布局头的字节。
中央承托顶面采用独立哑光中灰，外框保留已接受的运行色；这个入口检查实际三角面的坐标、法线、
绕序、接触高度及四枪承托点不变，避免新版颜色导入器顺带改变整机。前后 RMESH 与报告保存在
`tmp/mesh-weaver/tray-material-candidate/`，不自动 stage 或运行游戏。
服务线由 `tools/blender/generate_mesh_weaver_service_links.py` 原创参数化生成，无外部贴图或模型。
独立静态 `mesh_weaver_service_links` 保留机器地块原点，布局清单记录 RF1、电源机和机器插口坐标；
两线及少量压线夹沿设备后侧布置，电源机西侧接线盒直接贴壳，不进入动画网格或玩法输电网络。
移动这三件设备时须同步修改走线生成源并重新运行端接审计，不能只平移地图对象。
缺少私有源时应使用已验证缓存核对运行资产，不能无声重算一份假体积。
新增资产后 `build` 本身不保证运行目录已更新；`test` 或 `run` 的 stage 步骤会同步资源。

既有五枪早于 manifest schema 1，文件名及私有源路径以蓝图目录为准。仅刷新刚性导入颜色时，
先构建 `NativeCodex.ps1 asset-tools`，再运行 `python tools/assets/reimport_weaver_weapons.py`；
它要求全部原始 GLB 和双哈希匹配，调用正常 GLB importer 完整重导出，拒绝几何、物理材质参数、
尺度或制造输入变化，再生成并校验测量缓存。无纹理源的旧 slot 0 会规范为 `0xFFFFFFFF`，
仍使用常量色；此入口不 stage、不重写源颜色、不迁移文件名，通常新资产仍走 manifest 入口。

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

音频可先运行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/test_weaver_audio.ps1`。
需先完成 native build；脚本链接该构建的 `lib/portable/math.o`，与游戏使用同一数学实现。
该独立原生 CPU 测试只生成和混合 PCM，不打开音频设备或 GPU；检查采样率、循环接缝、状态边沿、
零增益、满队列停机、换图清理及线程交接。产物位于 `tmp/weaver-audio-test/`，近远距离和音量仍需实机试听。

音量验收使用同一输出设备与系统音量，试听近处制造循环、启动/暂停/完成提示、远离机器及战斗音效
叠加。当前混音沿用已有声音，降低音乐与战斗总线增益并提高机器声部，具体增益和峰值余量见
[普通机合同](../reference/mesh-weaver.md)。原生音频启动日志只证明设备成功启动；离线 PCM 的
无削顶、距离淡出和旧 API 字节一致性也不能代替实际听感，未试听时须明确保留此项。

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
默认固定 1920×1080 窗口，`-Width/-Height` 可覆盖，并校验采样日志中的实际尺寸。
保留相同地块、RF1、电源、镜头和天空。active 在预热中开始真实 AK 任务，进入采样前已有动态资源；
如果任务提前结束或暂停，样本无效。报告分别保存整帧、GPU、准备阶段、绘制数和上传量，
多轮交替运行，不能把单次差值当作全园区保证。

`-Executable` 可指定已保存的同平台基线 EXE，须先放到 staged `rasterfall.exe` 同目录；
Windows 启动层会以 EXE 目录定位资源，单改工作目录不够。比较不同版本时须保持该目录资源不变，
并核对输出中的 EXE 路径、哈希、地图及采样配置。

`NativeCodex.ps1 test` 与 `package` 会在构建和暂存前执行一次蓝图 `--check` 门禁。
缺私有源时仍须通过公开运行资产和物理适配校验；有源时额外核对完整源统计。发现过期缓存即失败，
不能仅重编主程序沿用旧计量；先用原始源重生成并审阅测量差异。
