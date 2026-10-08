# 校园地图退役与表面缓存首次保存等待

> 状态：历史现场
> 日期：2026-10-08
> 原因：用户要求删除不再开发的校园地图，并比较 Campaign 01、前哨站与边缘站点加载。
> 当前入口：[地图架构](../architecture/maps-and-world-content.md)、[光照架构](../architecture/gpu-lighting.md#固定光照烘焙)

## 地图退役

删除校园 Planar Massing V0 的 `.map`、World Content、指挥桌/命令目录、专用交互与截图分支，
移除专用地图回归。world ID 2 留空，其他 ID 不重排；世界移交回归继续覆盖五个目录地图。
通用校园组件、合成组件展示与历史资料保留，不继续开发校园地图。
几何诊断 MapOnly 的空内容 policy 改为既有 `performance_empty`。
本机 staged 包的两份校园文件同步删除，没有清空光照缓存或其他资产。

## 原因定位

在 RTX 3050 Laptop GPU 上复现 Campaign 01 约 42 秒进程时间。独立缓存目录保留接收拓扑
及空间 GI，只缺表面数据；校验版本中表面数据约 26.6 MB，逐字节 checksum 耗时
32788 ms，文件写入 3401 ms。此前 `SCENE-PREWARM` 的约 1.5 秒没有涵盖后续保存等待。

原因是表面保存直接把 HOST_COHERENT 的 GPU 映射地址交给哈希与文件写入。该属性保证
一致性，不保证 CPU 缓存；独显映射上的连续小读取极慢。改为在 GPU 提交退休后一次性
`memcpy` 到普通主机内存，解除映射后再校验和写文件，沿用空间 GI 已有的快照方式。
数据布局、着色器、缓存键与图像算法均不改变。

修复后该地图复制约 1335 ms，checksum 30 ms，文件写入 20 ms。新增日志分别记录
`surface-readback copy-ms` 与 `save hash-ms/write-ms`；复制仍有成本，未宣称完全消除加载。

## 三地图对照

Windows native，NVIDIA 独显，640×480，`--skip-boot --frames 3 --frame-audit`，固定天空。
修复前后分别使用独立目录，复制同一套拓扑/光场缓存；每张图先缺表面缓存运行，再热运行。
所有进程等待真实退出并返回 0。计时包含启动、三帧和退出，不等同于菜单部署时间或 GPU 烘焙时间。

| 地图 | 修复前缺表面缓存 | 修复后缺表面缓存 | 修复前热缓存 | 修复后热缓存 |
| --- | --- | --- | --- | --- |
| Campaign 01 | 41.561 s | 8.241 s | 5.526 s | 5.518 s |
| 前哨站 | 31.379 s | 6.167 s | 5.396 s | 5.113 s |
| 边缘站点 01 | 28.103 s | 7.354 s | 6.284 s | 6.244 s |

三个地图的表面文件修复前后 SHA-256 全部一致，第二次启动命中持久缓存。
Windows 原生构建、完整逻辑回归（含五地图 WORLD-REQUEST）及光照回归通过。
光照回归确认实际加载 Khronos validation layer 并开启同步检查，无 VUID、SYNC-HAZARD
或 Validation Error；持久化、损坏回退、改灯失效和 clone 检查全部通过。
这解释了先前已缓存的前哨站/边缘站点体感较快；问题属于通用首次表面保存路径。
每格为一次受控采样，没有温控和多轮长尾统计；没有测量全部缓存为空时的完整冷烘焙。

本机证据位于 `tmp/map-loading-comparison/`、`tmp/map-loading-hash-diagnostic/`、
`tmp/map-loading-fixed/`，包含逐行 stderr 时间线、stdout 和进程计时 JSON。
构建记录为 `tmp/map-loading-fixed-build.log`，回归记录为 `tmp/map-removal-logic.log`、
`tmp/map-removal-lighting.log`。这些本机文件不作为仓库资产交付。
