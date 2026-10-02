# GPU 天空 V2

> 状态：当前实现
> 所有者：GPU Scene presentation、Vulkan graphics slot

GPU 默认使用独立程序天空：连续大气、太阳盘、远景体积积云和稀薄高云。CPU 保留
`rasterfall_sky.c` 的旧天空。美术方向来自[天空设计研究](../reference/sky-v2-design-study.md)，
操作与复核见[天空指南](../guides/gpu-sky.md)。

## 参数与所有权

`render/rf_gpu_scene_lighting.inc` 从展示时间生成风偏移，选择 clear、rain、warm 气氛，
并将云量、密度、云底、厚度、固定种子、浑浊度连同太阳一起按值提交到 `rf_gpu_lighting`。
天空太阳盘、云受光、WORLD 直接光与阴影消费同一组归一化向光向量及太阳辐射。
warm 同时改变地面太阳；没有第二套天空专用太阳。

这些值只属于展示侧，不写入 Game、地图通行或网络快照；云漂移不使静态 WORLD 资源失效。
远景模式使用相机旋转和实际投影重建世界视线，不使用相机平移；太阳/天气固定时可通过冻结
展示时间复现云形。未引入近云穿行或联机天气权威状态。

## 求值与合成

1. GPU Scene SKY 来源仅生成覆盖目标的背景几何，不再调用 CPU 的 `rasterfall_sky_layout`。
2. `graphics_sky.comp` / `sky.glsl` 在 graphics slot 自有 storage buffer 中生成线性 HDR 天空。
   默认每轴四分之一分辨率，可选每轴半分辨率或全分辨率参考。向上取整后的实际 extent
   同时用于求值和采样，处理奇数窗口尺寸。
3. 大气采用有限地平线空气质量近似和 Rayleigh/Mie 型角度响应，属于解析美术模型，
   **不是 Hillaire 多重散射 LUT 实现**。太阳盘独立计算，薄高云先合成，主云使用曲面高度层、
   多尺度密度、沿太阳方向遮光与 Beer–Lambert 透射积分。质量档内固定步进避免采样数随视线跳变，
   全分辨率参考采用更密集的步进；远云逐渐衰减。
   `graphics_sky_noise.comp` 一次生成可平铺的 Worley 云团与多尺度值噪声，硬件三线性采样复用
   云形与天气分布。短距离太阳遮光探针复用当前步的低频天气覆盖，探针仍独立计算高度与云形，
   减少重复纹理采样；该近似不用于主视线各步。步进起点按世界方向确定，打散切片条纹，不随帧重新抽样。
4. compute 写入后通过 shader-write → fragment-read barrier；Scene SKY 双线性采样该缓冲，
   写入现有 RGBA16F 目标，不读写 WORLD 深度。随后 WORLD、透明层、特效和 viewmodel
   遵循既有分层；统一曝光与色调映射之后再绘制 HUD。

全屏求值不包含建筑或地面，因而上采样不会把世界边缘混入天空。没有随机帧抖动、时间重投影
或历史累积；转头、切地图和 resize 无历史失效问题。云形在世界方向中连续采样，未使用六面贴图。

## GPU 生命周期与边界

`rf_gpu_lighting.inc` 与 `rf_gpu_vulkan_graphics.inc` 拥有天空 compute pipeline、descriptor 和
每个 slot 的天空缓冲、64³ RGBA16F 噪声体积和线性 repeat sampler。噪声体积仅首次使用时生成，
compute 写入后建立 compute-read 依赖，resize 保留该体积。resize 在该 slot 无在途提交时先创建新资源，再更新 descriptor 并退休旧资源；
退出沿 graphics owner 的排空/释放路径处理。正常帧无天空 CPU readback；捕获和基准测试允许显式读回。
SPIR-V 内嵌在可执行文件，package 不依赖概念图或外部天空贴图。
Scene 时间戳在天空 compute 后单独记录结束点，`sky_compute_ms` 是 `world_draw_ms` 的子区间，
不含后续 HDR 合成；捕获与 native retire 均按同一查询布局读取。该数据只作展示性能诊断。

当前是远景天空首版。世界空气透视、地面云影、IBL、夜空、多重散射 LUT 与天气过渡未实现；
环境填充仍由[GPU 光照](gpu-lighting.md)拥有，不能把云色或环境填充称作 IBL。
质量档改变分辨率与步进密度，不改变共享太阳与深度合同。具体硬件预算必须以实测评估，不能从 shader
步数或单张截图推导整帧性能。
