# HG-1B：资源身份与生命周期

> 状态：历史
> 归档原因：阶段完成或已由当前文档取代
> 当前入口：[GPU 渲染架构](../../architecture/gpu-rendering-architecture.md)

> 文档更新：2026-09-19
> 源码核对基线补充：2026-09-19 HG-2B 在 registry 增加单调 `frame_epoch`；每次成功 `frame_begin` 递增，上限拒绝回绕。混合帧保存 epoch，跨帧重 pin 同一 generation 不能恢复旧计划的执行资格。正常 pin/退休/释放规则不变，详见 [Core 帧计划](hardware-graphics-hg2b.md#core-混合帧计划基础)。
> 源码核对基线：2026-09-19 工作区；`rasterfall_render_resources.h`、`render/rasterfall_render_resources.c`、`rasterfall_draw.h`、`rf_core_host.c`、`rf_game_lifecycle.c` 与 Windows 验证入口。

普通 static prop 的 CPU 资源已由 renderer registry 持有。Draw/reference 保持 HG-1A 的
同步整数 lowering；本 checkpoint 没有 GPU mesh cache、retained DrawSpan 或 indexed draw。
下一项为 HG-2A，normal-frame hardware 接入仍须等待 HG-2B。

## 所有权与身份

- `rasterfall_render_resources()` 提供单 renderer registry。原 `static_prop_models[]` 和
  attempted 数组已移除；prop profile 的规范模型路径作为注册键，同路径的实例共享一份 backing。
- 每个 slot 拥有堆分配的 `rasterfall_model_asset`，包括 mesh、material table 和所有 texture
  assets/views。这是一个不可分割的资源 bundle；材质、纹理的持久身份为 mesh handle 加表索引，
  不另建能独立卸载的纹理 slot。现有 Compute Texture V1 的逐帧纹理编号不变。
- handle 为 slot + generation；generation=0 无效。失效后，未 pin 的旧 backing 立即释放；
  pin 中的 slot 退休，不能再用于新 pin 或新注册，同帧旧引用仍能 resolve。完成后旧 handle 失效。
  slot 再利用会递增 generation，达到 unsigned 上限后永久停用，不能回绕命中旧引用。
- 容量固定为 256 个 bundle，路径最大 255 字节；耗尽、无效路径和分配失败返回失败。
  文件加载失败按路径缓存，world 失效后允许重试，不在每实例/每帧反复加载失败资源。
- 正常 Draw 带 registry handle，consumer 在解引用 mesh 前校验身份。无注册 handle 的借用
  backing 仅保留给同步诊断 fixture。实例位置、朝向、scale、scene/form light 等仍是独立快照。

## 帧与 world 生命周期

`rf_core_begin_frame()` 开启单帧引用期；static prop 首次使用资源即 pin，同帧重复使用只记一次。
旧 producer 的透明/特殊 prop 同样 pin，因为 lowering 后的 RasterCmd 仍可能持有纹理指针。

`rf_core_end_frame()` 成功完成 CPU flush 或现有同步 native GPU present 后，结束引用期并回收
退休资源。提交/呈现失败不会释放 pin；Core shutdown 先拆 GPU backend、renderer 和 retained
数据，再取消帧引用并回收。将来改为异步 GPU 或多帧在途时必须替换此完成合同，不能沿用成功返回
即释放的假设。

Game init、成功的 `rf_game_request_world()` 和 Game shutdown 使旧 world 资源退休；
无效 world 请求不触发失效。Game shutdown 可先于 Core shutdown，pin 会保护尚未完成的帧。
CPU renderer resize 不触发资源失效；GPU target/device 缓存尚未实现，不能据此声称已验证 GPU
持久资源重建。显式 `rasterfall_resources_invalidate()` 也是当前资源重新加载的入口。

`--frame-audit` 新增 `draw-resources`：live、retired、pinned、failed 是当前 bundle 数，
loads 是累计文件加载尝试，releases 是累计成功 bundle 释放数。审计发生在 present 完成后，
正常帧 pinned/retired 应归零，固定世界的 loads 应稳定。它们不是 GPU 上传量，也不代表性能收益。

## 验证与复现

`--logic-test` 中的资源 fixture 使用受控 loader，实际执行 registry 分配和模型/纹理析构。
覆盖去重、无帧 pin 拒绝、重复 frame begin、旧 generation、退休后同帧纹理可读、重新注册、
重复帧复用、加载失败缓存、退出回收和 generation 耗尽。Draw/reference fixture 还检查无效
registry handle 在发出命令前被拒绝，并保留 HG-1A 的命令/color/depth 精确比较。

Windows hosted 工具 `tools/rasterfall_resource_test.c` 使用真实资源、Game 生命周期和
static prop renderer，验证 Outpost/Campaign 反复切换、同帧旧/新 generation、CPU surface
尺寸变化、缺失模型、无效 world 请求及 Game shutdown 时仍有 pin。它不创建 native 窗口。
从仓库根目录运行：

```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& C:\msys64\usr\bin\make.exe -j8 -f windows/Makefile build-windows/rasterfall-resource-test.exe
& .\build-windows\rasterfall-resource-test.exe
powershell -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 package
powershell -ExecutionPolicy Bypass -File tools/hardware_graphics_baseline.ps1 -OutputDirectory tmp/hg1b-final -Checkpoint HG-1B
powershell -ExecutionPolicy Bypass -File tools/hardware_graphics_resize.ps1 -OutputDirectory tmp/hg1b-resize-final
```

新资源编译单元已加入根 Makefile 的正常/self 规则和 Windows 编译列表；hosted 工具仅有 Windows
目标，不进入玩家 package 或 Toyc app 自动扫描。没有新增运行资产或宿主 libc 依赖。

首轮完整基线在 `tmp/hg1b-accepted/`；最终基线在 `tmp/hg1b-final/`。构建、集成工具日志分别为
`tmp/hg1b-final-build.log`、`tmp/hg1b-resource-final-build.log`、`tmp/hg1b-resource-final-test.log`。
manifest 保存 exe/资产 hash、adapter/driver、命令和退出码。生成物不提交。

最终 Windows package、完整逻辑回归、CPU/compute differential、四组 selected world stream、
Intel strict native/Fog 和 Campaign 波次通过，manifest.result=PASS。与 HG-1A 最终基线的
33 份 BMP、stream、texture sidecar 和完整 depth 数据逐文件 SHA256 一致，记录在
`tmp/hg1b-final-before-after.json`。真实 world/CPU resize 集成工具退出码为 0。

`hardware_graphics_resize.ps1` 仅操作它启动的进程窗口，native/Fog 运行中改变三次尺寸。
`tmp/hg1b-resize-final/manifest.json` 为 PASS、退出码 0；140 帧均为 strict GPU-native，
实际 extent 为 1280×720、964×581、1284×741、804×601。所有帧 readback/copy/fallback
为零，资源 loads 恒为 23，帧末 retired/pinned/failed 为零；日志保存在同目录。
首轮 resize 脚本曾因 PowerShell 异步 Process.ExitCode 返回 null 拒绝验收；现保留进程
handle 并读取原生退出码，首轮失败证据保留在 `tmp/hg1b-resize/`，没有将缺失退出码算作成功。

Linux freestanding、其他 GPU、minimize/restore 和 device 重建本轮未验证。
