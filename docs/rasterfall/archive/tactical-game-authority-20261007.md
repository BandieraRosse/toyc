# 靶场与策略实验统一 Game：原生验证记录

> 状态：历史
> 日期：2026-10-07
> 归档原因：本轮实现与限定验证完成；当前契约见[战术架构](../architecture/tactical-ai.md)。

## 实现范围

靶场、策略对抗、无图形 CLI 和预测使用正式 Game actor。策略提交控制意图，
Game 执行导航、武器、碰撞和伤害；渲染读取真实角色。旧独立武器推进和伪角色展示已退役。
靶场读取正式射击结算事件，包括确认生命伤害与击杀；同帧多人攻击的靶子在事件消费完后重置，
混合射击不计作单个射手的独立 TTK。预测深拷贝 Game，隔离现场状态和随机流。

## 本机证据

- Windows native 构建与完整逻辑回归通过；`tmp/tactical-native-test.log` 包含
  `TACTICAL-SESSION Game authority / headless parity / player fire / reset passed`。
- `NativeCodex.ps1 tactical-test`：564 checks、0 failures，覆盖共享推进、预测隔离、
  旧计划失效、暂停、真实遮挡、射击事件及同帧混合击杀统计。
- CLI 短程 Beam/simple、双武器、攻防交换 batch：4 局退出 0。
- CLI 双武器靶场冒烟：216 行配置，HTML/CSV 报告转换成功。
  每项仅使用 2 个请求样本，此结果只验证执行和数据链路，不用于评估武器强弱。
- 两张实验地图以原生 `gpu-scene` 各执行 40 帧、退出 0，截图已检查；
  最终帧均报告 bridges/readback/mixed_execute 为 0。日志和截图位于
  `tmp/tactical-game-gpu/`。靶场画面可见正式角色，场景默认处于暂停状态。
- 文档检查通过；`git diff --check` 无空白错误。

## 未签收范围

终端输入自动化因窗口未成为前台而停止，没有继续注入输入。
本轮 GPU 冒烟只验证加载、静止画面和退出，不代表终端操作、RTS 观战及运动战斗观感已签收。
未进行完整 GPU 回归或性能签收，也未重新训练策略。旧独立模型的训练权重和收益结论不适用于规则 V2。
同场景状态一致性由加载后的 Game 克隆验证；仅相同种子不保证 CLI 生成场景与游戏内场景完全相同。
