# 前哨站目标格预约验证

> 状态：历史
> 日期：2026-10-09

用户追加格预约，并要求 RTS 指定格子作为移动终点，已占格则命令失败并短暂提示。
当前合同由[RTS 预约](../architecture/rts-command.md#前哨站目标格预约)维护。

实现按一米格中心与实际支撑层检查静态通行、单位占位和 actor 预约；失败保留原命令和原预约。
多选从点击格分配不同格子，点击格失败拒绝整组，编队偏移格失败保留对应成员原命令。
停止、到达、死亡、身份变化和导航重建释放预约；到达后以实际身体占格接替。
同时修复统一命令结果把多选接受人数覆盖成 1 的问题。

验证：

- `powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 test` 完整原生逻辑通过，退出码 0。
  日志 `tmp/grid-reservations-test.log`。覆盖负坐标吸附、同格互斥、上下层隔离、保留原目标、
  停止释放、到达占位、死亡占位清理、身份变化、导航重建及两人编队整组拒绝。
- `python tools/rts_floors_check.py --outpost --reservation-check --output tmp/outpost-grid-reservations-native`
  实机 29 项通过，游戏与脚本退出码均为 0。检查占格失败提示、目标保持、提示过期、
  B1→二层→屋顶→一层及楼梯入口移动，最后返回 FPS。
  报告与截图保存在上述目录；已查看 `occupied-cell-feedback.png`，提示清晰可读。
- 文档检查与 `git diff --check` 通过。

实机脚本最初在概览点击了遮挡地面的屋顶，随后一层单位位于 HUD 下方；改为一层剖切视图并
通过正常 RTS 镜头输入定位目标后复测成功，没有修改玩法状态。
本次预约仅覆盖终点，途中局部碰撞保持原规则；通路预约、排队和自动让路尚未实现。
