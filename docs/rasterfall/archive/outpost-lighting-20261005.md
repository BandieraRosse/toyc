# 前哨站三层 GPU 灯具与遮挡（2026-10-05）

> 状态：历史现场；功能与限定镜头验证完成，性能限制见下文
> 当前合同：[GPU 光照](../architecture/gpu-lighting.md)、[前哨站](../reference/outpost-hall-v1.md)

用户要求停止维护 CPU，布置前哨站 B1、一层、二层照明，处理太阳与点光源穿墙，
替换旧路灯，绑定模型和光源方向；随后明确固定设施多用 spot、少用 point。

本轮移除前哨站全部 14 个 lamp_post，生成 34 盏吸顶灯和 15 盏壁灯，覆盖三层房间、
服务走廊、楼梯上下梯段、平台和外入口。两件模型由项目原创 Blender 参数化生成，沿正式
GLB→RFM2 导入器生产；光源与发光面共用 asset profile，位置/方向跟随实例平移、yaw 和 scale。
所有新增固定灯均为 spot；枪口、爆炸与编织机工作光继续保留原类型。

Scene 从实际不透明建筑绘制数据建立静态 BVH；BOX 使用其真实实体上下界，其他结构用三角形。
太阳和全部局部灯使用建筑可见性，RTS 切顶不删除光照遮挡。模型包围盒和玩法碰撞不参与。
室内顶部遮挡将室外填充压到 10%，没有新增 GI；天花板、背光设备仍偏暗。

## 验证

- Windows native 构建成功。原生逻辑回归初轮见 `tmp/outpost-light-logic.log`，到达
  `rasterfall: logic test passed`；最终复核见 `tmp/outpost-light-delivery-logic.log`。
- NVIDIA RTX 3050 Laptop 上的 `--gpu-lighting-test` 通过，最终日志
  `tmp/outpost-light-final-regression.log`。新增检查覆盖 point、超出两个动态阴影名额的 spot、
  太阳、有限线段和空世界重置；三个光源模式的真实盒体/三角形对照均 changed=0、max_error=0。
  对照曾暴露共边浮点裂隙，现通过包容共边的 barycentric 容差修复。
- `tmp/outpost-lighting-delivery/` 保存 B1、一层、二层、楼梯、研究翼五个正常 GPU 场景捕获及
  独立 stdout/stderr/report；五个真实子进程均 exit 0，正常帧 zero bridge / zero regular readback。
  这些是限定静止镜头，不代表完整上下楼路线、所有视角或 Vulkan validation-layer 签收。
- 初轮 `outpost-lighting-v1` 错误进入旧 Campaign，已排除；定向镜头随后加入前哨保留路由，
  捕获脚本会拒绝出现旧 Campaign 加载的结果。
- 资源暂存目录为 `build-windows/rasterfall-windows`；最终捕获 EXE SHA-256：
  `A23027A06D49E61932A542146CD43AE0C5C0B68A4FC2EE292C236721C4387C25`。
- 文档检查与 `git diff --check` 通过。未运行 CPU 视觉验收、编译器测试或更新 bootstrap。

## 成本与边界

以下均为 Windows native 正常时钟，一层固定视图，RTX 3050 Laptop，quarter 天空，120 FPS 上限，
预热 120 帧后采样 240 帧，无 frame capture。不同实现按先后采样，不是交替配对的系统性能基线。

| 实现／分辨率 | 轮数 | 整帧 P50 ms | 整帧 P95 ms | GPU P50 ms |
| --- | ---: | --- | --- | --- |
| 初始纯三角形 BVH，1280×720 | 3 | 23.159 / 23.189 / 23.264 | 24.279 / 24.241 / 24.435 | 15.931 / 16.049 / 16.087 |
| 实体 BOX 解析求交，1280×720 | 3 | 15.882 / 16.071 / 15.977 | 16.813 / 17.040 / 16.800 | 9.220 / 9.272 / 9.278 |
| 最终共边修订，1920×1080 | 1 | 25.467 | 26.573 | 18.126 |

证据分别在 `tmp/outpost-light-perf/`、`tmp/outpost-light-perf-box/`、
`tmp/outpost-light-perf-delivery/`。1080p 尚未达到稳定 60 FPS，不能据 720p 中位数宣称性能达标。
当前提交解决布光和建筑漏光，进一步降低逐片元可见性查询成本仍有必要。

静态道具、角色和透明物不进入建筑 BVH；太阳与最多两盏 spot 的旧 shadow map 仍负责
不透明模型投影。其他局部灯只有建筑遮挡，尚不能声称所有物体都完整投影。
每视图仍只选择 32 盏灯；固定设施共 49 盏不等于同帧全部参与照明。没有运行时开关、电网、
人工灯 GI、透明阴影或软半影模拟。
