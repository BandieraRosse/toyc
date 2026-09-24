# GPU Scene 首轮运行成本与动态资源复用

> 状态：历史现场；不代表正式 FPS 或完整生命周期签收
> 日期：2026-09-24
> 当前顺序：[活动计划](../plans/gpu-scene-renderer.md)

## 范围与归因

先补 `SCENE-FRAME-COST` 的 world、正式角色、敌人/程序角色、分层准备、提交/退休墙钟与动态资源计数。
初始 near 30 调查中，敌人准备约 155–162 ms，CPU 几何提取约 8 ms，GPU draw 约 6.3 ms。
最大问题是每帧销毁/重建动态 GPU buffer、纹理和 descriptor，而不是敌人姿态求值本身。

本轮改为单 slot 退休后复用容量，只上传活动顶点；保留顺序索引、白色纹理与 descriptor，容量不足时替换。
资源槽不代表 actor 身份，所有活动顶点、draw 材质、有效索引范围和位置边界都会更新。
暂时不活动的槽保留容量，直到 Scene owner/world 关闭；这是以保留容量换取减少驱动分配的明确取舍。
没有引入多帧流水，也没有减少敌人、角色或可见内容。

压力测试同时发现旧分层路径在 near 60 第 63 帧拒绝整帧：屏幕外粒子投影 X=-34928 超出 GPU 上传范围。
等深度屏幕矩形现先裁剪视口再上传，保留可见交集。最终 A/B 两侧包含相同裁剪修复，不把裁剪收益计入资源复用对比。

## 同包 A/B

Windows native、RTX 3050 Laptop GPU、1280×720、固定 near 0/30/60、固定 tick。
`tools/gpu_scene_cost.ps1 -OutputDirectory tmp/scene-cost-final-ab2` 运行三轮交替 rebuild/reuse，每轮 64 帧，
排除前 8 帧。rebuild 用 `RF_GPU_SCENE_REBUILD_DYNAMIC=1` 强制逐帧重建；两侧共用相同可执行文件和资产。
每项结果取三个单轮分位数的中位数，不是把全部样本混在一起；原始单轮数据保存在 `report.json`。

| 敌人数 | 帧墙钟 median：重建 → 复用 | P95：重建 → 复用 | P99：重建 → 复用 | median 降幅 |
| --- | --- | --- | --- | --- |
| 0（仍有程序队员） | 122.656 → 44.832 ms | 140.055 → 54.299 ms | 155.140 → 60.397 ms | 63.4% |
| 30 | 250.577 → 63.784 ms | 274.407 → 78.736 ms | 328.682 → 82.016 ms | 74.5% |
| 60 | 368.313 → 90.582 ms | 401.823 → 113.197 ms | 414.327 → 121.068 ms | 75.4% |

敌人/程序角色准备 median 分别从 69.594/182.810/292.121 ms 降至 3.892/11.385/21.099 ms。
逐帧 draw、敌人、剔除、程序角色和补充模块化来源数量一致；所有复用轮预热后 `dynamic_created=0`，
每帧复用 18/48/78 槽。near 30 总上传 median 从 16,869,568 降至 15,154,648 bytes，
说明主要收益来自减少创建/销毁，仍有大量角色输入与动态顶点上传。

帧墙钟是主循环开始到 Scene 退休后输出计时行之前，包含逻辑与来源提取，不包含后续日志及循环尾部。
测量有逐帧诊断输出，未控制 GPU 时钟，只有三轮短样本，不能作为阶段 5 低扰动五轮或产品 FPS 签收。
GPU draw timestamp 不包含先前的上传/skinning。最终 package exe SHA-256：
`D924ACC2BBDDA4E6F941050A81D74705D61B68C8E56F2710B050E7F5C63BEE00`。

## 本轮验证

- Windows native package 构建通过；最终包哈希与 A/B、画面和生命周期证据一致。
- `tmp/scene-cost-verify-{rebuild,reuse}/`：near 30、特感、死亡渐隐、程序/补充角色、特效五组各四帧，
  validation/sync 通过；第 4 帧 PPM 两侧逐字节一致。已查看 near 画面，确认世界、敌人、武器和 HUD 内容。
  `pixel-comparison.json` 保存五组哈希。只有捕获帧有显式读回，其余帧零读回、零旧 producer/mixed/bridge。
- `tmp/scene-cost-pressure-native/`：near 60 连续 96 帧通过，覆盖原第 63 帧屏幕外粒子失败点及后续尸体退出。
  长采样工具改为首帧核对初始敌人数，此后核对冻结来源与提交数量一致，允许战斗推进后尸体正常消失；
  A/B 仍逐帧比较两侧完整数量序列，不能以此放宽 workload 一致性。
- `tmp/scene-cost-lifecycle/`：单人启动 4 帧、真实暂停/设置/恢复/Tab/移动与 resize 80 帧、world-cycle 120 帧、
  实际到达存活敌人的连续战役 160 帧、五类故障全部通过。交互驱动轮询改为 100 ms，避免帧率提升后输入尚未完成就退出。
- `tmp/scene-cost-faults-sync/`：五类故障另外通过 validation/sync，录制/提交失败按预期退出 1，其他退出 0。
- `tmp/scene-cost-tests/`：逻辑、Scene pose、GPU graphics 回归通过；graphics 明确加载验证层并启用同步检查，
  新增三角形资源更新/增长请求/非法输入/活动范围缩小/顶点读回及重新创建资源的 color/depth 对照通过。
- `tmp/scene-cost-pressure-sync/` 原定 96 帧的长验证层采样因验证开销过大，在第 7 帧正常请求关闭，
  脚本按帧数不足拒绝该次结果；不计作 96 帧通过。上面的短同步专项与 96 帧无验证层原生运行分别记账。

文档检查与 `git diff --check` 通过。没有运行 Full、10,000 帧 soak、实机联机或正式五轮性能签收。

## 后续瓶颈

复用后正式模块化队员准备 median 约 31–36 ms，是当前主要剩余开销；near 60 的敌人准备约 21 ms、
提交/退休约 19 ms。下一轮先拆正式队员的 load/pack、bind/palette 上传和同步 skinning，再决定资源复用与提交合并。
本轮不宣称稳定 60 FPS、完整架构或阶段 4 已完成。
