# 敌人视觉生成与验收

> 状态：当前操作指南

资源、预算和姿态合同见[敌人视觉参考](../reference/enemy-visuals.md)；玩法与 renderer 边界见[角色表现](../character-presentation.md)。

```sh
make rasterfall app-glb-inspect build/rfchar_runtime_test
build/rasterfall --enemy-visual-family block-infected
build/rasterfall --enemy-visual-family humanoid-infected
build/rasterfall --enemy-visual-family legacy
build/rasterfall --enemy-visual-capture tmp/enemy-visual-v2/mixed
python3 tools/enemy_visual_round.py --generate --capture --deterministic
build/rasterfall --logic-test
```

已有公开 RFM2 且不需重建 GLB 时，使用 `python3 tools/enemy_visual_round.py --capture --deterministic`。`--enemy-visual-capture <dir>` 默认捕获 AUTO mixed，也可与 family 参数组合。实际参数以 `build/rasterfall --help` 和脚本 `--help` 为准。

capture 使用正式 typed spawn 和正常 `render_enemies()`，冻结明确能力字段与位移相位；不是 gameplay 回放。检查完整 gameplay 结构未变、rig submission 数、资源失败和 command overflow。逐件捕获 idle/move、front/side/three-quarter、silhouette、world near/mid/far 和死亡；三特感还需 Smoker walk、Charger windup/charge/impact/recover、Tank windup/pre-impact/impact/follow-through/recover。命中边沿、重复 render 与冷却门禁随 capture 检查，网络 mask codec 由 logic test 验证。

`tools/enemy_visual_round.py` 在 `tmp/enemy-visual-v2/` 保存原始 BMP、组图、asset report 和导入/contract/runtime 日志；`--deterministic` 重拍比较 BMP。缺少新资产应使验收失败，不能以 legacy 回退图代替。组图仅供审阅，远景可读性须看原尺寸投影。真实地图会遮挡 100 m 视图；标记为 UNOBSTRUCTED DISTANCE 的独立画面只验证该距离投影，不代表正式场景视线。Windows 原生构建与实机签收遵循 [Windows Native 指南](windows-native.md)。
