# 紧凑接收缓存与 GPU 蒙皮提交

> 状态：历史现场，2026-10-06
> 归档原因：记录本轮实现、限定设备验证和性能取舍，不作为稳定架构合同
> 当前入口：[GPU 光照架构](../architecture/gpu-lighting.md)、[GPU 渲染架构](../architecture/gpu-rendering-architecture.md)、[定向对照指南](../guides/gpu-lighting.md#低成本间接光候选对照)

用户授权继续减少角色/显示重复准备，连接主视图与 AUX 消费的 GPU 蒙皮，并针对 WORLD 定位
遮挡、过滤、材质、缓存查找与读取成本。GI 优先降低单点大小，保持当前空间精度。

## 实现与容量

- 角色静态材质检查、衣物与身体骨架兼容检查、纹理使用扫描按资源 generation 缓存。
  衣物直接复制已经校验并打包的身体 palette，冻结姿态和世界变换仍逐帧检查。
- 审核显示层既有完整几何失效键：相同 retained packet 已跳过打包和上传；实际文本、布局、
  位置或动画变化继续失效。本轮没有把仅内容类似的显示来源错误复用。
- 蒙皮 batch end 封存输入，首个主/AUX 消费者将 dispatch 与绘制录在同一 command buffer。
  保留 transfer→compute 与 compute→vertex/transfer 屏障；只有成功 queue submit 才认可姿态缓存。
  所有 reader 退休后才能更新或释放资源，取消批次不认可新姿态。冷创建仍同步，仍为单帧槽。
- 接收采样点由 **352 B 降为 128 B**：32 B 静态位置、探针格引用及可见性位图，96 B 为人工灯与
  太阳/天空六方向 FP16 光照。静态区与动态区使用同一 SSBO 的独立区域；邻居复用探针格表，
  三线性权重按位置计算。没有先加粗采样间距，也没有改 cell/BSP 拓扑。
- 增加相同拓扑的 224 B/sample FP32 照明诊断模式，供精度和读取成本对照；它不是旧 352 B 实现。
- 可选真正三维灯表复用原屏幕/深度表，并测试光源体积与联合空间 cell。保留既有深度预通道、
  透明层、远段和混合相机回退；额外 compute 两阶段和片元读取均有屏障。
  1080p 额外占 5,222,400 B/owner。该轴未证明稳定净收益，因此默认关闭。
- 补 `receiver` 与 `receiver_read` 消融，以及 GPU 蒙皮区间；合并蒙皮进入总 GPU，独立蒙皮
  不在 reference 的主绘制 query 内。对照须同时检查整帧、CPU 准备及提交/等待计数。

车间缓存保持 31,889 leaves、73,509 nodes、255,112 samples、1024 RFU（2 m）间距，
unresolved 为零。单 owner 缓存由 94,504,016 B 降至 **37,358,928 B**，约 90.1 → **35.6 MiB**，
节省 60.5%；单点减少 63.6%。这些是实际分配和拓扑计数，不是推算的帧率收益。

## 验证

证据在 `tmp/optimization-*`；日志、截图和程序不提交。当前 Windows native 构建及 staged exe
已更新，着色器离线生成并通过 20 个 Vulkan SPIR-V 变体校验。retained display 的 CPU 合同通过。

NVIDIA RTX 3050 Laptop 完整 graphics 回归通过 7146 checks，覆盖共享蒙皮、取消、姿态复用、
设备顶点、纹理、深度、辅助视频视图；日志确认 Synchronization validation 启用，无 VUID、
SYNC-HAZARD 或 Validation Error。光照专项也确认相同的 layer/sync 激活，无 VUID/同步错误；
20 组二维/深度/三维灯表颜色与深度一致，11 组深度预通道对照一致。FP16 与 FP32 受控房间
保持 22,592 samples，HDR 中心读数的相对误差为 0（输出本身为 RGBA16F）；薄墙、封闭零照度、
正反绕序、保留几何的固定灯改色/关灯、resize、无效替换和清空回归共同通过。
最终资源专项通过 120 帧：CPU backing 增长、两次真实窗口缩放、world generation 在 submit 后
失效、fence 完成前保留 pin、退休后重载与 backing 复用，以及 runtime map GPU cache；
最终 live/retired/pinned 均为零、bridge 为零，Synchronization validation 无错误。
原始日志位于 `tmp/optimization-native-regressions-v3/resource-generation.*`。
该专项发现旧精确位置判定与当前 CPU double/GPU FP32 palette 的量化边界不匹配；原 HEAD
角色准备代码配合同一蒙皮 SPIR-V 复现相同的 12 个位置差异，最大 1 个模型存储格，法线及 UV 为零。
仅专项位置门槛修正为最多一格，保留 mismatch/max 日志和法线/UV 精确判定；未改蒙皮计算。
原准备代码对照日志在 `tmp/optimization-native-regressions-v2/prep-reference.*`。
最终验收包 EXE 为 `f9cf8f69bb848e1bdee726a0a34938c208c3ba94359b06c09ca4c6132f0359a2`。
正式 world-cycle 120 帧通过，覆盖第 30/60/90 帧替换及连续独立 Scene native 提交，
bridge/readback/mixed_execute 为零，日志确认 Synchronization validation 且无错误。
五类 present 故障通过：acquire-out-of-date、present-out-of-date/suboptimal 恢复后各完成四帧；
record-failure 与 submit-failure 按预期非零退出，只报告此前的一帧成功呈现。逐项检查实际退出码、
连续帧、layer/sync 激活和错误日志，未发现 VUID/同步危险。
真实双 AUX 正常原生采样通过：120 帧预热后采样 120 帧，两路 slot 各刷新 29 次，
UI 与整帧均 valid=1、实际退出码为零；日志在 `tmp/optimization-dual-native/`。
该检查关闭 validation，不能替代同步层证据。此前双 AUX 开启同步层的长采样在 300 秒到期，
当时约 6.5 秒/帧，尚未完成预热；到期前未报告 VUID/同步危险，但不能记为通过。
共享双 AUX 的 GPU 回归已在同步层下完成，正式 native 主视图、换图及故障回归也已完成；
真实双 AUX 的完整同步层长采样仍未签收。

相同最终 package 的 FP32/FP16 固定捕获覆盖车间二层、上层楼梯与前哨站一层，均为 1280×720。
三组图每个 RGB 通道最大绝对差值均为 **1/255**，最大通道平均差值分别为
0.00185/255、0.00342/255、0.00421/255；差异像素分别为 3,938、8,210、10,824 / 921,600。
已检查捕获图，未见新的照明边界或色阶异常。这只限定三个视角与相同拓扑的照明精度差异。
图像和激活日志在 `tmp/optimization-fp32-images/`、`tmp/optimization-fp16-images/`，
逐像素结果在 `tmp/optimization-image-comparison.json`。

## WORLD 定位与性能限制

初始车间二层 1920×1080、120 帧预热、120 样本消融中，正常总 GPU 16.116 ms，WORLD
14.250 ms；关闭 local 建筑遮挡后总 GPU 10.701 ms，关闭 PCF/BRDF 分别为 16.203/16.179 ms。
这些改变画面，差值是边际成本，不能相加或当作独占耗时。参考计数每着色调用约 11.42 个候选灯、
3.70 次局部遮挡逻辑查询；计数使用原子操作、包含 overdraw，不能代表普通帧的可见像素或硬件射线数。

三维灯表连续采样与冷却采样两组对照均记录热限频。增加每次启动前 45 秒冷却的三轮 AB/BA 后，仍在正式着色
阶段到达 86–87°C；总 GPU P50 reference 为 17.907/18.501/18.629 ms，三维表为
18.245/18.992/18.645 ms。WORLD 对应 15.878/16.339/16.564 ms 与 16.061/16.638/16.408 ms；
灯表 compute 约 0.05 → 0.15 ms。未观测到稳定净收益，保留默认 factorized 表和三维表诊断开关。
不把热机差异解释为结构性 GPU 回退，也不将缓存刷新小 pass 的缩短冒充 WORLD 优化。

最终 package 中 FP16 与蒙皮轴各两轮 AB/BA，1920×1080、120 帧预热后采样 120 帧，
每次启动前冷却 20 秒，关闭 validation、计数及捕获。两侧都观测到热限频；下表是各轮分位数
的中位数，没有合并所有帧重算。
性能包 EXE SHA-256 为 `7a961b3b497fa793ce89d7a6d1bf2402754df56237e960d333439f01b199088a`；
每个比较轴独立保存 package、地图、SPIR-V 哈希及实际激活日志。

| 同包模式 | 整帧 P50 | 总 GPU P50 | WORLD P50 | 缓存刷新 P50 | CPU batch end 均值 |
| --- | ---: | ---: | ---: | ---: | ---: |
| FP32 照明，224 B/sample | 27.846 ms | 18.512 ms | 16.420 ms | 0.044 ms | 0 ms |
| FP16 照明，128 B/sample | 28.044 ms | 18.325 ms | 16.256 ms | 0.035 ms | 0 ms |
| 独立蒙皮提交 | 27.982 ms | 19.003 ms | 17.047 ms | 0.037 ms | 0.576 ms |
| 合并蒙皮提交 | 28.235 ms | 19.077 ms | 16.967 ms | 0.036 ms | 0 ms |

FP16 对照只隔离照明精度，不能将它称为相对旧 352 B 结构的完整收益；首批旧包刷新约 0.104 ms
与新包约 0.035 ms 的观察来自不同阶段和热状态，也不作稳定帧率结论。合并后的蒙皮 GPU 区间
约 0.152 ms，reference 的独立蒙皮没有进入总 GPU query；CPU prepare P50 为约 4.382 → 4.078 ms。
直接回归证明一个封存批次减少独立 skin submit/wait，以上整帧样本未证明稳定 FPS 提升。

最终 package 的九模式定向消融（每项一轮、120 样本、启动前冷却 10 秒）如下；所有模式
均观测到热限频。它们用于指明调查方向，没有正式性能或视觉签收含义。

| 消融 | 总 GPU P50 | WORLD P50 |
| --- | ---: | ---: |
| none | 19.637 ms | 17.411 ms |
| roof | 20.203 ms | 18.012 ms |
| sun | 18.953 ms | 16.685 ms |
| local | 13.418 ms | 11.175 ms |
| pcf | 20.388 ms | 18.222 ms |
| brdf | 20.729 ms | 18.458 ms |
| rays | 12.884 ms | 10.810 ms |
| receiver：跳过缓存 | 19.508 ms | 17.161 ms |
| receiver_read：保留树查找 | 20.915 ms | 18.528 ms |

local 的约 6.2 ms 边际变化与首批旧包消融方向一致，优先调查真实局部遮挡查询。fast 普通
路径已跳过旧顶部射线，不能从 roof 的差值推断顶部缓存成本。PCF/BRDF 和缓存两项的小差值
受温度/频率漂移干扰；receiver_read 没有更快也不能解释为负的照明读取成本。本批无法可靠
拆出树查找与缓存读取的独占毫秒，保留诊断轴供稳定热状态下复测。

三维表独立计数对照均值：shaded 为 8,678,078，候选灯由 99,100,375 降为 78,250,136，
即每着色调用约 11.42 → 9.02，减少 21.0%。局部遮挡逻辑查询两边均为 **32,127,767**，
可见局部光两边均为 **10,699,444**。三维表主要剔除了原本会在半径/锥角检查中早退的候选，
没有减少这批实际遮挡查询；这支持保留默认 factorized 表、继续调查遮挡本身的取舍。
计数 shader 包含原子操作并可能改变早期深度行为，运行明确为 valid=0/diagnostic=1，
其约 350 ms GPU 时间不参与普通性能结论。

初次最终验证的临时 PowerShell runner 在内部管道漏接 NativeCodex 的 Console 输出，导致
同步日志判定误报；native 光照进程已成功结束，完整外层日志保留并恢复专项记录。
随后统一 PATH/Path 键并使用显式等待、独立 stdout/stderr 的 native runner，未据误报修改 GPU 代码。

## 后续边界

本轮完成 FP16 与邻居冗余消除；同连通光照区域角点共享、墙体/楼板边界拆分以及局部自适应密度
尚未实现。不能仅按坐标合并跨遮挡边界的角点。后续先解决这些拓扑合同，再考虑 RGB packing、
量化权重和 16-bit index。完整战斗路线、软件 BVH、核显和跨设备性能未签收；代码未自动提交。
