# GPU Scene 几何与本地来源切片

> 状态：历史现场记录；不是新 Scene 视觉基线批准或阶段 1B 签收
>
> 日期：2026-09-23
>
> 当前入口：[活动计划](../plans/gpu-scene-renderer.md)；复现：[fixture 指南](../guides/gpu-scene-fixture.md)

## 已定位根因

1. `persistent_map_mesh_finish` 的顶点 Y 已是世界高度；通用 Draw 变换仍减去 mesh `min_y`。
   原地图实例 Y=0，没有补回 foot origin：墙和坡道整体上移 900 RFU，平台则按自身最低高度
   偏移。现由 `persistent_map_instance` 设置实例 Y=`min_y`；不改相机、shader 或 viewport。
   floor 的局部网格原点另有合同，不套用此修复。
2. 持久 box 的 min-X 面使用 `color-0x080808`，CPU `draw_box/draw_cuboid` 使用加色；已统一为加色。
   这解释近 box 大面积平坦颜色差分，不能归为像素边缘容差。
3. LABEL 直接写 CPU surface 时，WORLD 命令尚未 flush，文字会被之后的几何覆盖，也未进入
   native overlay coverage。现移到正式 OVERLAY、HUD 前；无世界深度。SIGN 仍是世界牌面和文字。

CPU 与 Draw 使用相同 yaw/pitch、focal=`width*3/4`、near 和向下屏幕 Y 约定。通用 Draw 对
model foot origin 的处理与世界几何 producer 的原点合同不一致才是大面积几何偏移的原因。
normal fixed tick 会用站立眼高重新派生 camera Y，不能把启动时临时赋值 -350 当作最终相机高度。

## 验证与证据

- Windows native build 退出 0；完整逻辑回归退出 0，日志 `tmp/scene-logic-final.stdout.log`。
- `persistent map projection: PASS vertices=36`：坡道、墙、抬高平台的实际 mesh packing 与生产
  placement，覆盖正视、yaw 与 pitch；逐点 world/view/screen/逆深度零差异。输出 MAP-PROJECTION
  坐标，旧 instance Y=0 会失败。
- `scene local source: PASS`：使用真实 Game actor 构造器，覆盖创建、销毁、两次 freeze 间同 ID
  替换、slot 移动、world 切换、展示输入、重复 ID 事务失败与 epoch 溢出；另实际执行 session
  reset、Campaign→Outpost→Campaign、unload，确认 load 的 memset 不重置身份计数器。
- 最终完整地图九镜头：`tmp/gpu-scene-final-map/`；单 render 缩减九镜头：
  `tmp/gpu-scene-final-single/`。均显式使用 MapOnly policy；各含 CPU PPM、GPU BMP、PNG、
  diff、日志、输入副本与 SHA256。原始 Campaign policy 的前期对照保存在
  `tmp/gpu-scene-geometry-final/`，不能和 MapOnly 指标混算。
- `tmp/gpu-scene-source-native.stdout.log` 保存真实来源连续三帧 native 审计：epoch=1、generation=1、
  slot=1、items=1 保持稳定，真实动作时间为 16/32/48 ms；3 帧均 rendered，fallback、readback、
  CPU framebuffer copy 和 invalid transitions 为零。它与地图隔离输入分别检查；mixed bridge 仍存在。

doctor 的系统 GPU 枚举权限不可用，本轮未重新冻结设备/驱动身份。所有 capture 用 required-native，退出码、native 路径与生成物均
逐次检查。capture 本身包含诊断 readback，不能当作普通帧零 readback 或性能测量。
package wrapper 的目录清理曾遇到旧 exe 删除权限问题；本轮以原生 Copy-Item 将已构建 exe、assets
及存在的 private-assets 放到标准 package root 后运行。没有以旧 zip 或旧 executable 冒充候选。

## 结果边界

LABEL/SIGN 已分别目视确认；air gate 开/关保持完全相同相机，开启时坡道遮挡其下部；近 box
没有高度错位或反转；远墙窄线可见且被平台局部遮住。残留图像差异仍原样保存，未放宽阈值。
完整地图与单物体的 LABEL 字形掩码两侧均为 194 像素，位置完全一致；单物体 LABEL 全帧零差分。
CPU gate 开关分别改变完整地图 7521 像素、单物体 39231 像素，确认两种状态确实生效。
硬件浮点覆盖、CPU 整数边缘与插值并非逐像素一致；当前数据用于继续制定 Scene 画面合同，
不自动批准这些差异。Campaign 队员的额外差异也未用 MapOnly 结果掩盖。

本地 adapter 生成真实 actor 的 snapshot、pose/附件所需展示语义和 Scene 元数据；它没有生成
finalized palette，也没有独立 Scene target、资源 pin/submit/retire 或 native present。
这些仍是下一步 1B 工作。本轮没有正式性能收益结论，没有宣称同步验证或跨设备 Scene 签收。

## 最终原始差分统计

以下为 1280×720 全帧变更像素数，不是通过阈值；完整 MAE/max/哈希见各目录 `diff.json`。

| View | Full map changed pixels | Isolated changed pixels |
| --- | ---: | ---: |
| map-gate-off | 2848 | 0 |
| map-gate-on | 3511 | 1374 |
| map-label | 3716 | 0 |
| map-near | 3114 | 580 |
| map-platform | 1454 | 42 |
| map-ramp | 2523 | 190 |
| map-sign | 3511 | 2 |
| map-thin | 2671 | 354 |
| map-wall | 1714 | 552 |

Executable SHA256: `EDA7F764ECF07896FB8745C26F69C0EFA567A573C5C8AAAE5D481C30B8620D39`.
