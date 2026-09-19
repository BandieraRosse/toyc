# HG-2A：单 mesh hardware indexed draw proof

> 文档更新：2026-09-19
> 源码核对基线：`9cecfc13` 加 HG-2A 工作区；`gpu/include/rf_gpu_graphics.h`、`gpu/src/rf_gpu_vulkan_graphics.inc`、`gpu/shaders/graphics_v0.vert/.frag`、`gpu/src/rf_gpu_graphics_test.c` 与 Windows Intel 实测。

HG-2A 已完成独立离屏 proof。真实 `vkCmdDrawIndexed()` 消费持久 VB/IB，vertex shader
执行实例/相机变换，固定功能执行裁剪与光栅化，D32 attachment 执行深度比较和写入。
该入口不链接 Rasterfall 逐三角形 frontend，也不生成该 mesh 的 RasterCmd。
正常帧仍由 CPU/compute 消费原命令；HG-2B 是接入正常 static props 前的强制门禁。

## 所有权与入口

| 内容 | 所有者/入口 |
| --- | --- |
| 设备、队列、Vulkan loader、内存选择 | `rf_gpu_vulkan_backend.c`；context 的 `require_graphics` 请求同一 graphics+compute queue，默认 compute 选择不变 |
| proof CPU 输入和逐实例快照 | `gpu/include/rf_gpu_graphics.h`；这是有界诊断合同，不替代 `rasterfall_draw.h` |
| 持久 mesh、texture、descriptor、pipeline | `rf_gpu_vulkan_graphics.inc` 的 `rf_gpu_graphics`；复用 backend device/queue，必须先于 backend 销毁 |
| 尺寸相关资源 | 独立 target：RGBA8 color、D32 depth、image views、framebuffer 与诊断 readback buffer |
| 变换、逐 primitive 光照与 opaque 着色 | `graphics_v0.vert/.frag`；shader 不读取 gameplay 或 renderer 全局 scope |
| 数值、图像、生命周期 fixture | `rf_gpu_graphics_test.c`，Windows 证据包装为 `tools/hardware_graphics_proof.ps1` |

每个 proof owner 只允许上传一份 immutable mesh 与一张 RGB texture。注册时校验索引、
范围、triangle-list 和 primitive normal 一致性；corner vertex 保存三个源 normal，
允许按 primitive 复制顶点，但每次绘制仍按 index range 提交。GPU 数据为 device-local
VB/IB/storage texels，经临时 host staging 上传并等待 fence 后释放 staging。
这是 HG-1B 两层 ownership 中 backend 一侧的最小证明，尚无 registry handle/generation
适配、多资源 cache、延迟 DrawSpan 或 normal-frame replay。

帧内只上传实例 push constants。每帧预检全部 draw 后才记录命令，范围/容量/数值非法直接失败，
没有 CPU fallback。单队列、单帧在途、同步 fence 完成后才能复用/释放；提交失败使 owner
不可继续绘制，销毁时等待 queue。resize 先成功创建替代 target，再释放旧 target；不会重传
mesh/texture，相同 extent 无操作。device 重建需要销毁旧 proof owner 后重新注册，尚未做自动恢复。

## 冻结的 proof 数值与目标合同

- 输入位置为有界整数，UV 为 `[0,65535]` Q16，normal 为有界 Q15；接口预检范围确保 shader
  的 32-bit 运算不溢出。yaw Q10 旋转后再做 milli scale/底部 pivot/平移，所有整数除法向零截断。
  相机 yaw/pitch 与现有 `world_to_view()` 顺序一致。
- 三个源 normal 分别旋转并转 signed short，再求平均；form light 使用现有
  `(-13377,26755,-13377)`、ambient 136、directional 120。flat 先乘 form 再乘 scene，
  保留两次 Q8 截断；textured 使用 `scene*form/256` 后调制 texel。textured 的 RGB 来自 texture，
  不消费 flat base color；材质 tint/ambient/specular、透明、toon、sphere 等均未纳入此 proof。
- 固定 WORLD near=64、focal=`width*3/4`；hardware 使用浮点齐次投影与像素中心采样，
  没有声称与 CPU 的整数屏幕投影/采样位置完全一致。固定功能负责 near clipping 和单/双面剔除。
- texture 为 opaque RGB storage texels，fragment shader 做 nearest/repeat 和整数光照。
  不引入 sampler/mipmap/透明纹理；源资产转换适配仍属后续阶段。
- color 为 `R8G8B8A8_UNORM`，无 sRGB 转换；readback 是 RGBA byte order。depth 为 `D32_SFLOAT`，
  clear=0、compare=`GREATER_OR_EQUAL`、write 开启。shader 把 noperspective inverse-Z 截断后除
  16384 写入 `gl_FragDepth`，实际遮挡由 depth attachment 执行。该比例为 2 的幂。
- render pass clear/store 后转 `TRANSFER_SRC_OPTIMAL`；attachment writes/early-late depth →
  transfer read、transfer write → host read 均有显式依赖。host 非一致内存执行 flush/invalidate。
  color/depth 格式能力在创建前查询，诊断 readback 被允许且不属于 native-present 性能结果。

数值 oracle 使用独立双精度投影与重心坐标，固定排除距边缘 1 像素的覆盖带；纹理 cell 边界
仅排除 1e-4 UV 带。内部颜色要求精确一致；非近面交叉深度允许最多 1 个 inverse-Z 整数单位
的插值浮点舍入。遮挡测试另行精确检查常深度前后关系与同深度后画覆盖，不能以整图误差抵消。
近面交叉检查硬件裁剪覆盖与深度有效范围，**没有验证近面新顶点与 CPU clipped inverse-Z 的等价性**。

HG-2B 必须解决/验证上述采样、近面量化深度差异，包括顶点在相机后方的情形；当前 proof 的
depth mapping 仅是候选方案。还需实现 compute CLEAR/LOAD_EXISTING、GPU color/depth 双向 bridge、
Draw/Raster 稳定顺序、VIEWMODEL coverage、一次 Post/overlay 和 strict native present。
这一合同通过前，不能把当前 pipeline 直接放进 normal frame，也不能声称已获得玩法性能收益。

## 构建与复现

Windows 从仓库根运行：

```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& C:\msys64\usr\bin\make.exe -f windows/Makefile gpu-graphics-test
powershell -ExecutionPolicy Bypass -File tools/hardware_graphics_proof.ps1 -OutputDirectory tmp/hg2a-new
```

也可直接运行 `build-windows/rf-gpu-graphics-test.exe [capture-prefix]`；无资产依赖、无窗口，
退出码非零即失败。wrapper 要求全新输出目录，保存 adapter/driver、exe hash、原始 stdout/stderr、
PPM、完整 float32 depth 和 manifest；仅物理 integrated/discrete GPU 可以得到 checkpoint PASS。

根 Makefile 提供 Linux hosted `make gpu-graphics-test` 与 MinGW `make win-gpu-graphics-test`。
这是 hosted Vulkan 工具，不进入 Linux freestanding/self、Toyc app 自动扫描或 Windows 玩家包。
Windows normal backend 的自动依赖与根 hosted backend 目标同时跟踪新增 header/inc/shader。

构建无需 Vulkan SDK 或运行时 shader 编译器。生成源为：

```sh
python3 tools/generate_gpu_graphics_abi.py path/to/vk.xml
python3 tools/generate_gpu_graphics_spirv.py path/to/glslangValidator
```

ABI generator 校验 [Khronos Vulkan-Headers v1.3.290 registry](https://github.com/KhronosGroup/Vulkan-Headers/blob/v1.3.290/registry/vk.xml)
的固定 SHA256，仅导出实际使用的 graphics 结构、函数指针和常量；device/queue 相关既有 handle
采用原 minimal ABI 的类型别名。SPIR-V 为 Vulkan 1.0；本次使用 glslang 16.3.0，两个 shader
均通过 `spirv-val --target-env vulkan1.0`。MSYS2 Python 应在 PATH 含 `/mingw64/bin:/usr/bin`
的 MSYS shell 内运行，保证其子进程能找到 compiler DLL。

## 本次证据和覆盖限制

- `tmp/hg2a-proof/manifest.json` 为 PASS；Intel Iris Xe、vendor 8086/device a7a0、queue 0。
  flat、yaw/scale/pivot/camera、混合源 normal、Q8、nearest/perspective、winding/单双面、
  远近/同深度、完全近面后方和近面交叉覆盖均通过。
- 重复绘制和 `128×96 → 320×240 → 128×96` 后原 color/depth 字节一致；VB/IB 累计上传
  452 bytes、texture 16 bytes，整个绘制与 resize 序列不再上传静态数据。此数值只描述小型 fixture。
- `tmp/hg2a-validation-final.log` / `tmp/hg2a-validation-loader-final.log` 记录成功加载
  `VK_LAYER_KHRONOS_validation` 的实机执行，无 Validation Error/VUID。首次验证层加载因 DLL
  搜索路径失败的日志保留在 `tmp/hg2a-validation*.log`，不作为 validation 通过证据。
  按 [LunarG 同步验证入口](https://vulkan.lunarg.com/doc/view/latest/windows/synchronization_usage.html)
  设置 `VK_VALIDATION_VALIDATE_SYNC=1` 后另跑 `tmp/hg2a-sync-validation*.log`，未报告同步 hazard。
- 新增 graphics ABI 的所有结构大小及字段偏移与官方同版本 C 头对照一致，原始布局表在
  `tmp/hg2a-tools/{ours,official}_abi.txt`。
- `tmp/hg2a-package-build.log` 和 `tmp/hg2a-differential-build-final.log` 为成功构建记录；
  `tmp/hg2a-regression/manifest.json` 为 PASS，覆盖现有完整 CPU/compute differential、
  `--help`、`--logic-test`、selected world captures、strict native/Fog 和 Campaign 波次。
  与 HG-1B final 的 33 份 BMP、stream、texture sidecar 和 depth 文件逐一 SHA256 相同，
  对照在 `tmp/hg2a-before-after.json`。

Linux hosted/freestanding、其他 GPU、自动 device 恢复与 graphics native present 本次未验证。
正常游戏的 resize/swapchain 合同仍以既有 compute 记录为准；本项 resize 只验证离屏 graphics target。
