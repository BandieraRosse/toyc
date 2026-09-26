# GPU Compute Raster 退役

> 状态：历史资料
> 所有者：Rasterfall Core Host、Vulkan graphics 与 Windows 验收
> 决策日期：2026-09-26

## 目标

以硬件 Scene 作为 GPU 渲染主线，保留 CPU 软件渲染。移除生产源码、构建和当前验收中的 GPU Compute Raster、mixed executor 与 Draw/Raster bridge；旧路线的设计和诊断记录仅留在 `docs/rasterfall/archive/`。CPU renderer 使用的 `toy_raster_cmd` 不属于退役范围。

## 执行顺序

1. 使独立 Scene 初始化、帧提交与关闭不依赖 mixed executor、GPU Raster target 或旧 registry pin。
2. 让 GPU 运行入口直接选择 Scene，移除旧正常帧 mixed 提交、旧 CLI、诊断及其 Core 状态。
3. 从 Vulkan graphics/backend 分离并移除 compute raster、bridge shader、buffer、pipeline 与 ABI；保留 Scene graphics、skinning、present 和 GPU 资源 cache。
4. 清理 Makefile、Windows package、测试和脚本的旧依赖；把历史路线说明归档，更新当前架构、指南和入口。

## 当前进度

第 1 步已落地：Core 为独立 Scene 使用单独的 renderer mode，初始化跳过 GPU Raster target 与 mixed executor；runtime 按 Scene 模式提交，不再以 mixed executor 是否存在作为门禁。

第 2 步的公开入口已切换：`--renderer gpu-scene` 是等价于 `--gpu-scene-play` 的单人入口，`--renderer gpu-compute` 已从 CLI 移除；Windows NativeCodex 的 `gpu-test` 和 `acceptance` 改为提交独立 Scene。Core 现仅接受 CPU 与 Scene mode，已删除 mixed executor 的初始化、正常帧提交、关闭、相关状态及旧 Raster present 调用；Windows 玩家目标不再链接 `rf_gpu_mixed_executor.c`。旧 retained frame 与 pre/post 逻辑测试、专用 hosted mixed 测试暂留，compute raster 与 bridge 仍在 Vulkan backend 和构建中，第 3–4 步仍待实施。

## 完成门槛

- Windows native CPU 构建与逻辑回归通过；CPU 软件帧可正常呈现。
- Windows native 硬件 Scene 完成互动、combat、resize、world cycle、故障注入和长帧运行；逐帧保持零旧命令、零 mixed draw、零 bridge、零常规读回。
- 生产构建不链接 mixed executor、GPU Raster pack/bin/compute shader 或 bridge pipeline；当前源代码不再保留这些路线的实现和入口。
- `--help`、当前文档、脚本与实际运行一致；历史资料在 archive 中注明 Scene 替代入口。
