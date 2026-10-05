# GPU 实时光照

> 状态：当前
> 所有者：GPU Scene presentation、Vulkan graphics
> 最近核对：2026-10-05

GPU Scene 使用独立实时光照。CPU 静态光照路径保留为遗产，按 2026-10-05 用户决策停止维护，不作为新增功能或视觉验收目标。GPU 不采样 V1/V2，不复制烘焙场，不以 CPU 的颜色、整数深度或光照乘数作为兼容目标。

## 数据与所有权

`render/rf_gpu_scene_lighting.inc` 从只读展示状态提取设施聚光灯、枪口/爆炸瞬时点光源和可选手电筒。固定设施优先使用 spot light，point light 限于短时效果和少量局部工作光。灯的位置、颜色、半径、强度与锥角进入 `rf_gpu_lighting`；最多 32 盏，按强度与相机距离排序截断。灯状态不写入 Game、地图碰撞或网络快照。

`gpu/src/rf_gpu_lighting.inc` 拥有每个 graphics slot 的光照缓冲、阴影目标、GPU 深度缓冲和后处理管线。光照在提交前复制，帧执行期间不可修改。resize 重建 HDR/LDR 目标和相关描述符；关闭在 GPU 排空后释放资源。

## 帧流程

### 建筑遮挡与灯具绑定

前哨站用 `light_ceiling` 吸顶灯和 `light_wall` 壁灯替换原路灯；布局由
`tools/outpost_storeys.py` 维护，覆盖 B1、一层、二层、楼梯间和入口。
`rasterfall_prop_light_profile()` 拥有模型局部 RFU 插口、发光材质编号、方向、锥角、颜色、强度和距离。
插口以模型底部中心为原点、+Z 为正面；位置与方向沿模型 yaw 旋转，位置和半径沿实例 scale 缩放。
灯罩材质自发光与照亮世界的 spot light 分别提交，但使用同一模型 profile；无游离世界坐标灯。

Scene 在 world/map generation 改变时，从实际 WORLD 的 wall、box、ramp、platform、floor、boundary
不透明几何重建遮挡结构；应用 primitive 的真实世界平移，不从碰撞盒猜测遮挡，不按主相机剔除。
软件路径对普通 BOX render 直接按其真实上下界解析求交，避免重复查询细分后的平面三角形；这不是模型包围盒，
门洞仍由独立墙段与过梁组成。其他建筑使用实际三角形，包括楼梯踏步。
`gpu/src/rf_gpu_architecture_light.inc` 验证公共输入并路由硬件/软件路径，最多 65536 个输入图元，
超限明确失败；帧在途时禁止替换，resize 保留，空世界清除，关闭排空后释放。辅镜头拥有独立缓冲。
片元通过双面建筑求交检查太阳、所有 spot/point 到表面的可见性，因此建筑遮挡不受两个动态
聚光灯阴影名额限制。门洞保留真实开口，有限楼板和楼梯使用真实网格，RTS 切顶不移除遮挡。

向上可见性将有顶室内的室外半球填充压到 10%，其余亮度由灯具承担；这是保留可读性的近似，
不是 GI 或人工灯反弹。静态道具、角色和透明物不进入本建筑结构；原太阳和两个 spot shadow map
继续承担不透明模型与角色的投影，其他局部灯仍不具备这些动态物体的完整遮挡。

WORLD producer 将已完整纳入当前建筑输入的 draw 标为 `architecture_occluder`（host metadata，
不进入 shader push constants）。非空建筑结构成功安装后，这些 draw 退出传统阴影图；硬件与软件
查询都适用。其余道具、角色和模型类别仍照常投影；BOX 类若存在解析输入未覆盖的退化或反向区间，
整类保留原投影。标记来源和建筑输入必须属于同一冻结世界；正常帧在提交前完成代际校验与结构替换，
替换失败不能继续提交该帧。清空结构恢复传统投影，无效替换保留原状态，resize 不清除覆盖状态。
`RF_GPU_ARCHITECTURE_SHADOW_MAPS=1` 在 graphics 初始化时恢复重复投影，仅用于同版本对照。
建筑仍由原先的二值射线遮挡决定阴影；移除重复 PCF 后，其阴影边缘不再叠加阴影贴图的过滤暗边。

### 可选硬件 Ray Query

后端在 loader 和设备均支持 Vulkan 1.2 时，查询 `VK_KHR_acceleration_structure`、
`VK_KHR_ray_query`、`VK_KHR_deferred_host_operations` 及 `accelerationStructure`、
`rayQuery`、`bufferDeviceAddress` 特性。默认自动启用；不支持时保留原软件 BVH 着色器。
`RF_GPU_ARCHITECTURE=software` 强制对照，`hardware` 要求能力存在，否则初始化失败，不能静默替换。
此处软件仍是 GPU 片元查询，不是已停维的 CPU renderer。旧设备保留原 Vulkan 1.0 shader 路径。

`gpu/src/rf_gpu_architecture_ray.inc` 从同一建筑输入构建一个静态 BLAS 和 identity-instance TLAS；
真实 BOX 转为十二个表面三角形，其他建筑沿用原三角形。驱动构建使用 `PREFER_FAST_TRACE`，
scratch 地址按设备属性对齐；host coherent 输入、临时命令池和 fence 只存在于生成阶段。
BLAS 写入到 TLAS 读取、TLAS 写入到片元读取均有显式 barrier。成功提交并等待后才更新描述符，
原结构随之释放；失败返回错误，已提交资源排空后回收。空地图绑定已构建的零实例 TLAS。
BLAS/TLAS 和存储由各 graphics owner 保留，resize 不重建；不逐帧构建，不共享跨视图可变所有权。

硬件片元变体使用 GLSL 460 / Vulkan 1.2 SPIR-V，binding 7 为 acceleration-structure descriptor，
软件变体同一 binding 为 SSBO。射线保留法线起点偏移和有限光源距离，双面、不透明、首个命中终止。
只替换建筑可见性（太阳、局部灯、顶部填充），未引入光追 pipeline、SBT、动态 BLAS、GI 或软阴影。
两种路径在几何边界的浮点求交可有少量像素差异，不能假定所有场景逐位一致。

### 提交与合成

GPU 独立程序天空与体积云消费本页的同一太阳，compute 求值、HDR 合成与资源边界见[GPU 天空](gpu-sky.md)。CPU 继续使用旧天空。

天空 compute 后、阴影绘制前，`graphics_light_tiles.comp` 为每个 16×16 屏幕块生成一个 32 位候选灯掩码。
point 使用有限球体，spot 使用有限球扇形，对块视锥的四个侧平面及相机前平面做保守相交测试；
保留像素与世界空间容差，不消费深度、房间身份、接收面法线或建筑可见性。
因此透明物和楼梯开口沿用真实光源；精确半径、锥角、背光面和遮挡判断仍在片元执行。
片元按原灯索引顺序消费位掩码，不重排光照累加。混合相机/投影的 draw 和屏幕空间 viewmodel
退回完整灯表。各 graphics slot 独占掩码缓冲，resize 在退休后重建；compute 写入到片元读取有显式 barrier。
`RF_GPU_LIGHT_TILES=0` 在初始化时禁用筛选，供同版本画面和性能对照；默认启用。

1. 未由当前建筑查询完整覆盖的不透明 WORLD 几何绘制阴影：太阳采用三个相机附近的稳定正交范围；最多两盏聚光灯使用透视阴影。
   每张阴影图用独立光空间包围盒剔除，复用连续绘制的 pipeline/vertex/index 绑定；不套用相机可见性。
2. 阴影为 1024×1024 D32，深度在 GPU 内复制到 storage buffer；片元以 16 次深度比较求值连续移动的 tent PCF 核。
   权重随 texel 内的小数坐标变化，跨 texel 边界连续，不依赖硬件深度过滤扩展或随机抖动。
   此复制不经过 CPU readback。使用实际可见模型几何，碰撞盒不参与阴影。
3. WORLD、天空、透明、特效与 viewmodel 写入 RGBA16F 线性 HDR；世界深度为 D32 reversed Z。纹理先由 sRGB 解码，再参与过滤和着色。
4. compute 执行曝光与 ACES fitted 色调映射，将曲线肩部按其渐近值归一化，避免中高 HDR 值提前越过显示白而被硬裁切；
   最后编码为 sRGB RGBA8。极高亮度仍受八位输出量化限制。HUD 随后合成，保持界面颜色。正常帧直接 native present。

GPU timestamp 将 `world_draw_ms` 按命令区间拆为天空 compute、灯表 compute、阴影和主场景。
阴影区间含深度复制；主场景再拆为 WORLD/天空合成、透明/特效、viewmodel、后处理/视频合成及 HUD。
`detail_ms[0..5]` 依次保存灯表及这五个主场景区间；没有 draw 的层也写入边界时间戳。
这些区间不能直接称为 Ray Query 耗时。native、离屏和 capture 共用读回解释，离屏不等待未写入的 present 查询。

光照计数只在 `RF_GPU_LIGHT_PROFILE=1` 的独立片元变体中启用，同时要求设备的
`fragmentStoresAndAtomics`。计数缓冲按块累加已着色调用、候选灯、顶部/太阳/局部可见性调用、
局部可见结果及 PCF 核调用，GPU 完成后才读回。它包含 overdraw，且存储副作用可能改变早期深度优化；
不是最终可见像素数或 RT 硬件计数。普通 shader 没有这些原子操作，计数运行不得作为正常性能证据。
诊断消融可以分别跳过 roof/sun/local 可见性、PCF 或替换 BRDF，明确改变画面；
所得时间差只代表该改动后的边际成本，不是可相加的独立子系统耗时。

材质使用 GGX 镜面、Schlick Fresnel、粗糙度、金属度和标量自发光。平滑法线来自资源；无显式法线时用几何法线。风格化材质调整漫反射响应，仍消费同一组实时灯和阴影。当前粗糙度/金属度为 draw 标量；没有承诺 normal/ORM 纹理、IBL、GI 或完整动漫材质。

静态道具由 `rf_gpu_scene_world_gpu_prepare` 按 primitive 的 RFM2 v2 材质记录读取
金属度/粗糙度 u16 常量，编码到现有 graphics draw；粗糙度至少为 0.06，避免零值被视为
未配置并回退默认值。后续旧格式中相同字节承载 alpha/toon，不能作为 PBR 常量解读；
这些道具保持原有默认响应。几何、缓存身份、纹理和玩法状态不随材质接线改变。

环境填充按受光法线的世界 Y 分量，在较亮的冷色天空填充与较暗的暖色地面填充之间插值，
再消费上述建筑顶部可见性；阴影区保留表面朝向的明暗区别。不采样天空纹理，不代表间接光传输或 IBL。
直接光在太阳强度为零、PBR 背光面或聚光灯锥外时跳过无贡献的 BRDF/阴影求值；风格化材质保留原有暗侧响应。

## 边界与预算

- 太阳三个范围半径为 4096、20480、65536 RFU，即 8、40、128 m，512 RFU = 1 m。
  近级每纹素覆盖 15.625 mm，法线偏移保持为该级纹素宽度的 0.4 倍；光空间中心按整纹素对齐。
  每级光空间边缘向后续有效级别连续混合；
  零权重级别不读取阴影，内区剩余权重归零后停止。过渡带可能采样两级，预算不能只按单次 16 tap 估算。
- 最多两个聚光灯动态阴影名额按灯列表顺序分配；所有局部灯另有建筑 Ray Query / BVH 遮挡，点光源不再穿过已纳入的实体墙板或楼板。
- 半透明、粒子和 viewmodel 不投射 WORLD 阴影；不透明角色、静态物与实验区球体参与投影。
- 环境填充为法线相关半球颜色与顶部可见性近似，没有反射探针；金属只反射当前直接光。
- 旧 `light_q8` 等字段在部分几何存储中暂时保留，但 GPU shader 不消费烘焙亮度；旧整数兼容 draw 被拒绝，兼容索引上传已删除，旧 shader 不再编入 SPIR-V。
- 暂未建立本版本正式性能基线，不据短帧诊断承诺帧率。

实验区普通进入默认关闭，由 Runtime 独立开关和展示时钟控制球体与移动灯；性能隔离期间不提取实验灯、地图灯或瞬时灯，太阳与环境填充保留，阴影实际绘制另计数。登记与生命周期见[实验区合同](../reference/experiment-labs.md)。

实验区和验证入口见[光照指南](../guides/gpu-lighting.md)，当前交付顺序见[活动计划](../plans/README.md)。
