# Hardware Graphics 开发计划

> 计划更新：2026-09-19
> 当前进度：HG-0、HG-1A、HG-1B、HG-2A、HG-2B 已按 Windows Intel Iris Xe 口径完成。HG-2C 已开始，先补齐 CPU/GPU 分项计时，再消除双向全屏 bridge、统一 frame command context 并引入多帧在途；HG-3A 在 HG-2C 门槛通过前暂缓。Linux/其他 GPU 未验收。
> 实施入口：[架构与基线](rasterfall/docs/hardware-graphics-architecture.md)；[HG-0 checkpoint 证据与限制](rasterfall/docs/hardware-graphics-hg0.md)。

| Checkpoint | 状态 | 交付/下一步 |
| --- | --- | --- |
| HG-0 | 完成 | ownership/Draw V0/数值合同、可复现 Windows 测量脚本、CPU/compute captures、native/Fog/正式地图波次基线 |
| HG-1A | 完成 | CPU planar 前置修复；普通 opaque static RMESH 按实例/submesh 提交 Draw，同步 reference；命令/color/depth 精确回归与 Windows Intel 基线通过 |
| HG-1B | 完成 | CPU bundle registry、generation、Core 帧 pin 与延迟释放；[实现与验证](rasterfall/docs/hardware-graphics-hg1b.md) |
| HG-2A | 完成 | 持久 device-local VB/IB/texels、flat/nearest、RGBA8/D32 离屏 indexed draw；[数值合同与验收边界](rasterfall/docs/hardware-graphics-hg2a.md) |
| HG-2B | 完成（Windows Intel） | [正常 Windows strict native 混合帧](rasterfall/docs/hardware-graphics-hg2b.md)、遮挡/层顺序 fixture、近/中距离窗口帧与四 extent resize 按视觉正常标准通过 |
| HG-2C1–2C4 | 进行中 | [mixed 帧诊断、共享 target、统一 command recording 与多帧在途](rasterfall/docs/hardware-graphics-hg2c.md) |
| HG-3A / HG-3B | 待开发 | opaque static props allowlist → 扩围 |
| HG-4A / HG-4B | 待开发 | Ground → map/boundary 几何 |
| HG-5A / HG-5B | 待开发 | Character geometry → GPU skinning |

HG-0 校正：显式 frame audit 现逐帧输出；normal near/mid 标签只控制初始相机，稳定位置以实际审计为准；
波次必须显式加载 Campaign 并验证活敌。原始 HEAD 的独立 texture full-scan fixture 已复现失败，
HG-1A 已定位为 CPU planar vertex-lit 忽略 alpha/no-depth-write，并补齐恒定与插值光照路径；
前置记录见 [HG-1A 前置修复](rasterfall/docs/hardware-graphics-hg1-preflight.md)，Draw 接入及验收见
[HG-1A Draw/reference](rasterfall/docs/hardware-graphics-hg1a.md)。Windows strict native 已启用 hardware normal-frame 接入；其他平台/模式仍按各自现有路径运行。

原始调研与阶段实施草案见 [归档](rasterfall/docs/archive/hardware-graphics-original-plan-2026-09-19.md)；当前实现与验收以各 checkpoint 文档和 CLI 输出为准。
