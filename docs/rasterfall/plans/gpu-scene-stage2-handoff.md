# 阶段 2 迁移收尾与后续事项

> 状态：阶段 2 按用户决定收尾；遗留问题供重构后修复，隶属[唯一活动计划](gpu-scene-renderer.md)
> 最近核对：2026-09-24

用户最新决定：本轮继续快速收尾，网络玩家仅验证逻辑路径，实机问题延后到 GPU 完成后专项修正。
后续决定：不再强求 CPU/mixed 画面一致性；已发现差异只记录，重构后修复，立即进入阶段 3。
本文以下未完成的整图/基线条目是遗留事实，不再作为进入阶段 3 的阻塞门禁。
最终九组矩阵、成本、Campaign 捕获限制及修复清单见[收尾决定归档](../archive/gpu-scene-stage2-decision-20260924.md)。
网络实测不再阻塞当前迁移。阶段 2 的完整画面合同尚未退出；正常呈现仍走 mixed，
Scene 是同帧独立离屏 WORLD 审计。不能把本轮结果当作完整画面合同或性能验收。

## 已实现范围

- 程序角色冻结职业、动作、颜色、武器、光照和双面状态后独立提取几何；覆盖八名程序角色 fixture。
- 补 legacy 敌人、不透明死亡变换、阴影及 Smoker 舌头/束缚圈；渐隐死亡身体留给透明层。
- 增加 Scene WORLD 提取、资源预备、上传量、draw 和 GPU 时间戳诊断；bridge 计数应为零。
- 网络客户端和额外模块化队员已尝试冻结实际 pose/装备。补充身份仅在单帧诊断内有效，不能替代持久网络身份。
- 连接页补 native overlay 帧生命周期；显式 frame-audit 由脚本超时约束，关闭交互帧的 200 ms 看门狗。

## 验证记录与限制

2026-09-24 整图收尾补充：新增 WORLD opaque 专用捕获与全图报告；mid 0 首次整图对照
有约 47% 像素变化，包括远离轮廓的大面积墙面亮度差。mixed 的 integer-compatible
fragment shader 未消费 `material.w` 指示的顶点光照，Scene generated world 管线会消费。
这是待解决的光照合同差异，不能按边缘舍入批准。专项运行 PASS 不代表新基线批准。
最终采集目录为 `tmp/stage2-close-final/`，此前试采目录不作为最终版本签收。
正常完整 Scene、透明层、特效、VIEWMODEL 和 HUD 接入属于阶段 3，不列入阶段 2 剩余工作。

本轮 Windows package、逻辑回归和 pose 专项曾通过。早期八镜头 RTX 3050 validation/sync 通过，
记录在 `tmp/gpu-scene-stage2/`；该批次早于双面状态修复与舌头 pull timer 修复，不能代替最终舌头验证。
后续程序角色 validation/sync 与 GPU 时间戳通过，记录在 `tmp/stage2-final/`，
五个内部采样像素与 mixed 相同；这不是整图基线批准。
最终收尾 package、logic-test、gpu-scene-pose-test 均退出 0，日志见 `tmp/stage2-wrap-*`；
最终死亡与舌头镜头均通过 RTX 3050 validation/sync，捕获和日志见 `tmp/stage2-wrap-gpu/`。
日志与截图位于忽略的临时目录，不进入版本控制。

## 网络逻辑验证与后续专项

`--gpu-scene-pose-test` 直接调用真实 `render_network_teammate`，覆盖 host/guest 正常及倒地角色，
核对展示位置、朝向、武器和动画，验证冻结项可独立提取几何且玩法及网络输入不变；
另检查 guest 排除本地玩家、host 排除断线玩家、双方排除未激活 actor。用例不建立网络连接。
最终 package、logic-test 与 gpu-scene-pose-test 均退出 0；新增用例输出 `scene network logic PASS`。
日志保存在 `tmp/stage2-logic-only-*`。此次未运行实机联机测试。
完整 `--logic-test` 继续覆盖已有网络 packet/pipeline。以下实机事项全部延后，不能视为逻辑测试已经证明。

复现：更新 Windows package 后运行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_network.ps1`。
最近失败记录在 `tmp/stage2-network-verified/`：host 和 guest 均正常退出并产生捕获，
脚本仍报 `guest did not freeze a remote player`。guest 日志为 `items=4`、`supplemental_modular=4`，
脚本预期程序角色至少五名、补充模块化八名。尚不能判断是漏角色还是镜头可见性使数量预期不成立。

1. 先用实际 host/guest 位置、可见性与来源逐一对应冻结项，避免单纯放宽断言或强行补足八名。
2. 核对远端玩家、guest 队友、装备与客户端展示状态；验证 frame-local identity 的适用边界。
3. 修正接线或固定测试场景后，重新验证两端原生呈现、validation/sync、同帧图像及真实退出码。

GPU 主线进入阶段 3，推进完整帧层序、独立来源、动态资源复用和正常 Scene 呈现；
WORLD 整图差分及基线/容差审批按用户最新决定延期。
未开展 LAN、relay、丢包或完整帧性能 A/B；网络专项延后是用户指定的范围调整。
