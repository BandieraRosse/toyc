# 前哨站网格辅助视图与 RTS 命令卡

> 状态：历史
> 日期：2026-10-09
> 当前合同：[命令面板与网格视图](../architecture/rts-command.md#命令面板与网格辅助视图)

用户要求可开启的画面格线与状态颜色，以及类似 RTS 游戏的方形命令阵列和面板上方悬停解释卡。
实现归玩家 UI，网格和单位占位只读 Game。开关不进入玩法真值，默认关闭；FPS/RTS 切换保留。
静态颜色按世界、导航代际及楼层高度缓存，动态单位和预约每帧更新。图例消费点击，格线不消费世界指令。
屏幕格线沿主轴合并矩形，避免竖线逐像素产生 HUD 几何。Canvas 由 CPU/GPU 共用。

右下角使用三列方形命令槽；停止、镜头跟随、FPS、网格有图标与标签，其余槽位留空。
所有功能按钮共用绘制和命中矩形；悬停显示功能、快捷键、开关状态和不可用原因。
没有选择时停止与跟随按钮禁用，跟随按钮再次点击关闭跟随。解释卡片提高不透明度以保证文字清晰。

验证记录：

- Windows native 构建与完整逻辑回归通过；日志 `tmp/grid-view-build.log`、`tmp/grid-view-test.log`。
  覆盖只读开关不修改 Game、当前单位占格和预约颜色，以及多分辨率/缩放下方形按钮的共享命中。
- `python tools/rts_floors_check.py --outpost --grid-view-check --ui-only --output tmp/outpost-grid-view-native`
  实机 21 项通过，脚本和游戏退出码均为 0，实际设备为 NVIDIA GeForce RTX 3050 Laptop GPU。
  通过正常窗口输入验证默认关闭、悬停不执行、打开/关闭、楼层颜色更新、接受命令后的紫色预约格、
  停止释放、四种解释卡、跟随开关、FPS/RTS 保留开关、空选禁用提示与选择恢复。
- 已查看 `grid-tooltip-on.png`、`grid-first-floor.png`、`grid-reserved-target.png`、
  `grid-fps-view.png` 和 `command-stop-disabled.png`，格线、图例、颜色和说明可读。

实机截图与报告保存在上述 `tmp/` 目录，不进入提交。辅助格线不做世界遮挡；
不足三像素和屏幕外的格子跳过绘制。未对密集部队负载或 CPU 实机性能作结论。
