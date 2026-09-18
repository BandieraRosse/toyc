# Rasterfall GPU 与 Windows Native Platform 阶段计划

> 文档更新：2026-09-18
> 源码核对基线：GPU-8B2d B2d-4e checkpoint（2026-09-18）
> 当前状态：A-AUDIT、B1-CONTRACT、B2-SKY、B3-WORLD 与 B4-POST-WORLD submission contract 已实现；GPU-8B2 retained consumer 已消除半帧提交，producer debt 已分解为 transparent、effects direct pixels、viewmodel commands/direct pixels 与 typed unsupported command。GPU-8B2d B2d-0..2 已完成并通过 CPU/full-scan/tile-binned differential；B2d-3 已完成 ABI BEGIN_TRANSPARENT marker、ordered CPU reference、RGBA 分类、跨多 WORLD flush 的连续 opaque→transparent retained span、基础 Core 放行与显式 material/texture/edge/overlay/generic reason bits；B2d-4a/4b/4c 已将普通 RFM2 material alpha、RGBA texel alpha 与 `texel × material / 255` 组合 alpha 接入真实 producer，RGB + alpha=255 保留 opaque 快路径；B2d-4d 已固定 authored transparent platform 与 enabled air-gate box 的 WORLD producer，B2d-4e 已固定 enemy death fragment/dust 的透明 EFFECTS producer，muzzle outer/lobe 与 enemy dissolve death fade 也已迁移；B2d-5 normal-frame 收口仍待完成，未支持输入继续整帧 CPU replay。GPU-8B1/GPU-9A 等待 Windows Intel normal-frame 冻结。

本文档是 GPU renderer 与 Windows Native Platform 的当前阶段入口。它只保留已冻结的能力边界、
当前架构、最终目标和待解决问题，不再记录逐次 bring-up 日志和过期性能数字。可复核的运行事实
以 CLI 输出为准；具体 ownership 和命令见 `rasterfall/docs/rendering.md`、`runtime.md` 与
`build-platforms.md`。

## 阶段结论

Rasterfall 已经拥有一条可运行的 GPU normal-frame 链路，而不再只是 hosted Vulkan 实验：

```text
normal world frontend
  → Raster Command ABI V1
  → CPU tile binning
  → Vulkan compute raster
  → optional Post-Raster V1
  → CPU color + coverage overlay upload
  → GPU composite
  → Win32 Vulkan swapchain present
```

已确立的边界：

- CPU renderer 仍是默认路径，并长期作为 compatibility renderer 和 differential oracle。
- GPU 由 RF Core 拥有；Game、session 和 gameplay 不持有 Vulkan object。
- `--renderer gpu-compute` 显式启用 GPU；`--gpu-required` 把不可用从 fallback 提升为失败。
- Windows native present 已去除 normal GPU frame 的 color readback 和 CPU framebuffer copy。
- Windows 窗口、输入和音频仍使用 SDL2；获取 HWND 创建 Vulkan surface 不等于 Windows Native Platform 已完成。
- GPU-9A 的 pass 本身已通过局部 oracle；当前风险在 normal renderer 上游语义、帧分层和 present ownership。

## 当前 checkpoint

| Checkpoint | 状态 | 当前契约 |
| --- | --- | --- |
| GPU-0 ～ GPU-5 | DONE | Vulkan service、capability contract、Core-owned framebuffer、Raster ABI V1 与 compute rasterizer 已建立 |
| GPU-6 / 6.5 | DONE / FROZEN | 正式 CPU oracle、artifact/replay、color/depth differential 与保序 tile binning 已冻结 |
| GPU-7A / 7B | DONE / FROZEN | normal frontend capture，flat opaque 与 vertex-lit planar 命令已纳入 Raster V1 |
| GPU-7C / 7D | DONE / FROZEN | Core-owned normal GPU frame 与 Texture V1 已接入；unsupported batch 仍整批 CPU fallback |
| GPU-8A | DONE / FROZEN | Win32 surface/swapchain、BGRA8 transfer copy、resize 和零 readback native presentation 已验收 |
| GPU-8B1 | IMPLEMENTED / NOT FROZEN | CPU screen-space truth 上传 XRGB8888 color + 8-bit coverage，GPU source-over composite；还需 normal-frame 实机视觉与 timing 验收 |
| RenderFrame B1 / B2 | IMPLEMENTED / LOCAL PASS | camera 与六层有序描述已建立；sky 参数背景命令在 CPU reference/full-scan/tile-binned GPU 零差异，待 Windows 实机冻结 |
| RenderFrame B3 | IMPLEMENTED / LOCAL PASS | normal world batch 的 opaque/transparent command 已显式写入各自层；多 WORLD flush 在 retained stream 中一次稳定分成连续 span；不改变排序或 fallback |
| RenderFrame B4 | IMPLEMENTED / LOCAL PASS | 逐层 cursor 拒绝跳层/逆序；effects/viewmodel 分别 flush 且位于 post 前；overlay 入口统一 surface/renderer target。GPU consumer 仍明确 unsupported |
| GPU-8B2 | IN PROGRESS / LOCAL PASS | retained consumer 和整帧 replay 已建立；frame audit 区分各层 command/direct-pixel debt 并记录 fallback reason；effects opaque producer 已完成 command 化并建立 direct-pixel 零门禁；Transparent V1 B2d-0..3 已通过 ABI/CPU/full-scan/tile-binned/logic 门禁，B2d-4a/4b/4c 已将普通 RFM2 material alpha、RGBA texel alpha 与组合 alpha 接入真实 producer，B2d-4d 已固定 transparent platform/air-gate WORLD producer，B2d-4e 已固定 enemy death fragment/dust 透明 EFFECTS producer；muzzle outer/lobe 与 enemy dissolve death fade 已迁移，B2d-5 待完成 |
| GPU-8B2c Phase 5 | IMPLEMENTED / LOCAL PASS | LOCAL_VIEW muzzle core 与 outer/lobe 复用 VIEWMODEL Contract V1 projection/depth/coverage；remote/AI muzzle 保留 world EFFECTS，outer/lobe 使用真实 material alpha；local/world 分流与 layer/direct/fallback fixture 已覆盖 |
| GPU-9A | IMPLEMENTATION COMPLETE / LOCAL PASS / ACCEPTANCE BLOCKED | 独立 device-local `post_color`；identity 和 inverse-depth Fog V0 通过 oracle；尚未冻结 |
| WIN-1 / WIN-2 | NOT STARTED | 仍为 MinGW + SDL2；未建立自有 Win32 window/input/audio/runtime |

## 已冻结的核心契约

### Raster 与正确性

- Raster ABI V1 是 fixed-width、pointer-free、versioned word stream；GPU shader 不解码 C struct。
- CPU 和 GPU 消费同一 packed stream 与 Texture V1 table，color 与 signed inverse-depth 逐元素比较。
- CPU bbox binning 按原始 command order 建立 tile lists，不改变 raster semantics。
- normal world flush 在消费前完整分类。基础 Transparent V1 command 可与 opaque 一起 pack；edge、overlay、
  other 或未支持的 material/texture 仍使整批回退 CPU，不在 GPU depth 上补画遗漏命令。
- differential 只证明“相同 packed input 的 CPU/GPU 执行一致”，不证明 normal frontend 生成的输入本身正确。

### Native presentation 与 overlay

- Core 通过无 SDL 类型的 native handle contract 获取 HWND/HINSTANCE。
- swapchain 只接受实际 query 支持的 `B8G8R8A8_UNORM + SRGB_NONLINEAR` 和 `TRANSFER_DST`。
- normal native frame 不回读 color/depth，不复制 CPU framebuffer。
- screen-space UI 继续以现有 HUD、Console、GUI 和 `fb_draw`/`fb_font` 为唯一 truth。
- overlay 使用 XRGB8888 color 和独立 coverage 0..255；Console alpha=190 语义保留。
- resize 使用 replacement-first resource 重建；零尺寸窗口不开始 frame。

### Post-Raster V1

```text
raster color + signed Q20 inverse-Z depth
  → optional post pass
  → separate device-local post_color
  → overlay composite
  → swapchain copy/present
```

- bypass 时 presentation color 直接选择 raster color，不 dispatch、不复制。
- enabled 时 post 只读 raster color/depth，只写 `post_color`，不进行原位读写。
- Fog V0 使用 `inv_z = 1048576 / camera_z` 的反深度阈值，不把 depth 当线性米制距离。
- HUD、Console 和 Desktop 在 post 之后 composite，不进入 post effect。

## 当前执行计划

GPU-8B2 以“逐类消除 `pre_post_cpu_fallback`”为主线，不改变 viewmodel barrier 的
唯一整帧决策权，也不允许 GPU 先消费后由 CPU 补画未迁移层。

1. **B2a — producer debt 审计与 opaque effects：** frame audit 分别记录 effects/viewmodel
   command 与 direct pixels，并输出 transparent、direct producer、viewmodel 以及
   material/texture/edge/overlay/generic unsupported 的 reason mask。支持的 transparent command
   不产生 fallback reason；已能被 Raster V1 表达的 opaque effects command 直接进入 retained stream；
   effects facade 现在独立返回 direct producer 统计，triangle command 的逻辑结果数不再
   被误计为 direct pixels。
2. **B2b — effects producer 收敛：** world-space ray、ribbon、billboard、particle 按实际 depth/
   blend 语义转成明确 raster input；damage vignette 等 Post 之后效果显式归 overlay。完成标志为
   `effects_direct_pixels=0`。billboard、普通 hit/fire/explosion particle 与屏幕线 ray 已改为使用
   投影 `inv_z` 的 opaque raster triangles；固定 fixture 覆盖两条 command 的矩形、四条 command 的
   本地 tracer 双段以及越过屏幕边界的 ray，并断言 direct debt 为零。该完成标志已达到。
3. **B2c — viewmodel：** 依次迁移 opaque weapon geometry、hands/pill/attachments 和
   viewmodel-local effects；保留独立层、投影/depth policy 和既有 animation/frontend 所有权。
   Contract V1、CPU oracle、GPU VIEWMODEL span 与 LOCAL_VIEW muzzle core/outer/lobe 已完成；remote/AI
   muzzle 保持 world EFFECTS，outer/lobe 使用 Transparent V1 的真实 material alpha；enemy dissolve
   death fade 仅在最后 380ms 进入 source-over/no-depth-write；透明 world/RFM2 与其他 producer 仍属于
   B2d 后续迁移。
4. **B2d — transparent consumer：** Raster V1 显式表达 material/texture alpha、source-over、
   depth test 与 depth-write policy；透明 pass 保持 frontend 原始顺序，不在首版引入 OIT 或自动重排。
5. **normal-frame 冻结：** 在 Windows Intel 实机完成 GPU-8B1/GPU-9A 的视觉、Console/
   Desktop、resize、timing 和零 readback 验收；之后才开始 SDL-free Windows Native Platform。

### Windows 证据闭环

GPU-9A 暂不冻结。A-AUDIT 已使日志独立包含 frame/path/pose/layers/timing，B1/B2 已把 sky 从隐式
CPU framebuffer 写入迁为显式参数层。下一步在 Windows 真实异常现场完成以下闭环：

1. 正常运行加 `--frame-audit`，记录 world、camera x/z、sy/cy、pitch、extent、fixed-step ticks/
   accumulator、update/render/present/whole-loop 以及 GPU submit/fence/native-present timing。
2. 使用实际坏帧的 exact pose 和 extent 运行：

   ```text
   --normal-frame-audit <x> <z> <sy> <cy> <pitch-sy> <pitch-cy> <width> <height> <output.bmp>
   ```

3. 核对 GPU-native 与 transparent CPU-fallback 切换时天空无闪变，并核对 command coverage、fog/source 和保存的 BMP。
4. 将约 200 ms frame wall time 拆分到 update、frontend、pack/binning、GPU raster、post、overlay upload/
   composite、copy/present 和 fence wait。
5. 根据证据将问题归入 frontend semantic resolution、sky/world submission、command packing、post/composite 或
   present ownership，不用固定方位 capture 替代坏现场。

验收完成条件：坏姿态可精确重放，根因已定位并修复，normal native frame 视觉正确，帧分层无遗失，
color readback 和 CPU framebuffer copy 仍为零，且重新通过相关 differential、resize 和 normal-frame 门禁。

## 验收与后续边界

### P0：阻塞当前冻结

- Windows Intel normal gameplay 的真实坏姿态尚未留下完整 audit 记录。
- 局部 Raster/Post differential 通过，但 normal frontend → semantic resolution → packing 的上游正确性未被该门禁覆盖。
- sky 未提交、world coverage 不完整与 present ownership 异常尚未用同一坏帧证据排除。
- 约 200 ms 的 frame wall time 尚未归属到具体阶段，不能据此宣称 native GPU frame 达到性能目标。

### GPU-8B2 完成门禁

- 正常第一人称 gameplay 不再因 effects/viewmodel/transparent 的已知路径整帧回放。
- 每类 producer 都有固定 fixture，断言最终 path、retained count、direct debt、reason mask 和 layer cursor。
- unsupported fixture 仍按原层顺序完整 CPU replay，不存在部分 GPU 成功。
- transparent 的 CPU/GPU oracle 覆盖重叠面、texture alpha、tile 边界和 resize。

### P2：Windows Native Platform

- 实现 Core-owned Win32 window lifecycle、event pump、keyboard/mouse/input 和 framebuffer/native-present 协调。
- 迁移音频并移除 normal Windows runtime 对 SDL2 的依赖。
- 完成 package、resize/minimize/focus/DPI、shutdown 和输入行为回归。
- 在 SDL-free runtime 完成前，不将当前 HWND bridge 称为“Windows Native Platform 完成”。

### P3：后续 GPU 能力

只在 P0 闭环、GPU-8B1/GPU-9A 冻结且 GPU-8B2 边界明确后排期：

- GPU vertex transform/skinning；
- dynamic lighting 与 shadow；
- GPU light field；
- large particle systems；
- screen-space effects 与更高 internal resolution；
- advanced material model；
- native Linux Vulkan presentation/release validation。

## 最终目标

### Renderer

- CPU renderer 作为稳定兼容后端、可移植参考和 GPU correctness oracle 长期保留。
- GPU renderer 成为高性能 normal gameplay 后端，覆盖完整 world、transparent、viewmodel、effects、post 与 UI composition。
- normal GPU frame 从 raster 到 present 保持 device-local，不依赖每帧 color/depth readback。
- 每个 GPU 扩展先建立可重放输入、明确 capability/fallback 和可自动判定的正确性门禁。

### Core 与平台

- RF Core 唯一拥有 GPU service、resources、frame synchronization 和 presentation lifecycle。
- Game/session 只提供确定性状态与 presentation input，不持有平台或 Vulkan 细节。
- Windows 最终使用 RF 自有 Win32 platform backend，normal runtime 不依赖 SDL2。
- Linux 保留 freestanding CPU 路径，并在条件成熟时增加 native hardware Vulkan backend，不因 Windows 路线破坏现有可移植性。

### 用户可见结果

- CPU/GPU renderer 选择、optional fallback 和 required failure 行为可预期。
- GPU normal gameplay 具有完整帧分层，不丢 sky、world、viewmodel、effects、HUD、Console 或 Desktop。
- resize、minimize、focus 变化和 shutdown 不崩溃、不使用失效 resource，不在帧路径中引入隐藏 readback。
- Windows 物理 GPU 提供性能事实；WSL llvmpipe 仅用于 correctness，不用于宣称硬件性能。

## 验证入口

完整实时参数清单以 `build/rasterfall --help` 为准。当前阶段的主要入口：

```sh
make gpu-overlay-test
build/rf-gpu-overlay-test

make rasterfall
build/rasterfall --logic-test

# Windows package 目录
rasterfall.exe --renderer gpu-compute --gpu-native-present --frame-audit
rasterfall.exe --renderer gpu-compute --gpu-native-present --gpu-post-fog --frame-audit
rasterfall.exe --renderer gpu-compute --gpu-native-present \
  --normal-frame-audit <x> <z> <sy> <cy> <pitch-sy> <pitch-cy> <width> <height> <output.bmp>
```

`gpu-overlay-test` 同时覆盖 overlay composite、Post identity 和 Fog V0 differential。Windows normal-frame 验收
必须从 package 目录运行，以确保 exe-relative 公开资产可见。缺少 Windows 物理 GPU 环境时，必须明确报告
未覆盖 normal native-present、resize 和硬件 timing，不用 WSL llvmpipe 结果替代。

## 后续文档维护规则

- 只在 checkpoint 边界、所有权、最终目标或剩余问题变化时更新本文档。
- 不在本文档累积每次测试数量、临时 timing 或调试日志；它们以当次 CLI 输出和提交记录为准。
- 阶段冻结时，同步更新 `rasterfall/docs/README.md`、`runtime.md`、`rendering.md` 和
  `build-platforms.md` 中受影响的 ownership/验证边界。
