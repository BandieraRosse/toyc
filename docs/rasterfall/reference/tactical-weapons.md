# 实验武器与战士基准

> 状态：当前
> 所有者：Rasterfall Game 战斗
> 事实入口：`lib/game.c`、`lib/game_combat.inc`、`lib/game_hitscan.inc`、`lib/game_ballistics.inc`、`lib/rf_tactical_weapon.c`

靶场、对抗场和 CLI 使用正式 Game 武器表、actor 能力和命中结算。
权威规则由[共享战斗](../architecture/combat.md)维护；实验所有权由
[战术架构](../architecture/tactical-ai.md)维护。本页不另设第二张枪械数值表。

实验标签 rifle / smg 分别映射正式 AK / SMG。对抗双方与战士标靶使用 Lv2 standard 模板，
基准生命与回避由 `toy_game_actor_capabilities()` 读取；靶场射手使用 player 模板。
机械间隔、换弹、固定散布、飞行弹速、分段衰减、真实头部命中和简化回避全部遵守正式 actor 规则。
对抗使用正式备弹；靶场显式使用无限备弹，以便重复测量。

全身没有挡板；半身挡板遮住身体下半部分；探头挡板遮至身体高度 84%。
同一挡板 primitive 同时用于命中遮挡和 Scene 显示。横移实验目标使用正式移动与碰撞。
单发/点射的额外等待属于测试输入节奏，不能缩短正式武器机械/技能冷却。

报告输出实际发射、几何命中、弱点、确认生命伤害、回避消耗及完成/未完成试次。
规则版本 4 不输出旧模型解析命中期望，不将实际 HP 损失标为原始弹丸伤害。
规则版本 1 的独立浮点武器模型已经退役；旧训练参数和报告仅作历史证据。
