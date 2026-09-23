# GPU Scene 渲染地图首次复现

> 状态：历史；单次现场归档，运行通过，视觉差异待定位，不构成基线批准
>
> 日期：2026-09-23
>
> 当前执行入口：[活动计划](../plans/gpu-scene-renderer.md)；复现工作流：[fixture 指南](../guides/gpu-scene-fixture.md)

## 输入与证据

Windows native 构建/package 与完整 `--logic-test` 通过后，串行运行同一 package 的 CPU 和
required-native GPU。两侧使用 `--map rasterfall/assets/maps/gpu_scene_render_fixture.map`
（实际命令传 package 内绝对路径）、`--gpu-normal-scene map-ramp 0 --gpu-normal-fixed-tick
--frame-audit --frames 1`。CPU 另加 `--renderer cpu --dump-frame <cpu.ppm>`；GPU 另加
`--renderer gpu-compute --gpu-required --gpu-native-present --gpu-frame-capture <gpu.bmp>
--gpu-capture-frame 1`。进程以 `Start-Process -Wait -PassThru -WindowStyle Hidden` 启动并等待退出。

最终证据目录为本地 `tmp/gpu-scene-fixture-final-20260923-220552/`，含 `cpu.ppm`、`gpu.bmp`、
`cpu.stdout.log`、`gpu.stdout.log` 及各自 stderr 日志。生成物不提交；指南提供重新生成方式。
CPU dump 实际为 PPM P6，不能仅凭扩展名按 BMP 解码。

| 对象 | SHA-256 |
| --- | --- |
| package 地图 | `54AC2AA4343214792590835762E702FA3CCABC3AE61C4D463DE95E3BE0C6C25D` |
| package executable | `1DCB212C64809D76554402503E9D1C799F8D7DA701E5E106CF67AFE696A3B44C` |
| CPU 原图 | `48D0F4889D0C19275C6470B4F55C776AC9FCC5E9F6FEF02421D8DC8E560970CC` |
| GPU 原图 | `62F9DAD883DAEDF54636BAF8F69E87E30763A3B2BA4C178C188FDD5925543903` |

两侧退出码均为 0；日志确认同一地图、frame=1、camera=(0,0)、direction=(0,1024)、
pitch=(0,1024)、extent=1280x720、ticks=1、seed=1。GPU 报告 `path=gpu-native`、
`attempted=1 rendered=1 cpu_fallback=0`，map-draw 为 wall=2/36/1、box=8/44/1、
ramp=4/46/1、platform=1/12/1。此为单帧 capture，包含初始化与诊断 readback，不能推断性能、
生命周期或普通帧零 readback；未冻结驱动及整包内容哈希，不构成正式性能基线。

## 观察与后续定位

CPU 与 GPU 原图已目视核对，最终复跑图像哈希与先前目视核对图一致。坡道和左墙有明显几何差异：
GPU 坡道表现为较高的板状面，CPU 坡道延伸至画面下方；薄墙与平台也有位置/轮廓差别。SIGN 两侧
可见，LABEL 在此镜头均未确认可见。当前只确认差异，不将根因归于任何已确定的矩阵或深度错误。

后续先用相同输入定位 persistent Draw 与 CPU 路径的变换、投影和遮挡；补 LABEL 的可见镜头与
air gate 关闭状态。不能把 GPU 当前截图直接批准为新 Scene 基线，也不能把一张图当作全部内容覆盖。
旧 mixed renderer 的运行通过不代表新 GPU Scene 已有可执行资源、pass 或 native present。
