# 2026-09 开发地图与展示 fixture 记录

> 状态：历史
> 归档原因：开发地图的坐标、陈列布局与墙体迁移属于阶段现场
> 当前入口：[地图格式](../reference/map-format.md)、[地图架构](../architecture/maps-and-world-content.md)、[地图指南](../guides/map-authoring.md)

## 开发按钮与陈列带

`button_west_corridor_no_tank -23940 2100 200` 位于西侧走廊出口墙面，一次生成 16 个随机敌人但排除 Tank。`button_humanoid_actions x z y` 是 RF Humanoid V2 动作调试按钮，地图按钮位于 `(-12600,-12800)`，对应 Rifleman 固定展示在 `(-11800,-10200)`；动作按 `IDLE → WALK → RIFLE AIM → AIM + RECOIL` 循环。

`button_enemy_death_test 14000 -10500 -250` 位于开发者区东南空地，在前方 `z=-13500` 生成 Common/Fast/Heavy 各两名真实敌人，再经 `toy_game_apply_reported_hit()` 施加当前生命值伤害；击杀统计、死亡状态、effects、网络状态和清槽沿正式链路。它不维护独立假人或动画时钟。

工业组件陈列带在 `z=-17000`，十件组件沿 X 轴每 1500 RFU 排列，renderer 与 gameplay primitive 共用 profile 尺寸。Character Test Strip 固定在后方 `z=-20000`；地图只声明可见 label，角色、姿态和 AK attachment 由 renderer 固定配置。
模型陈列台的感染体家族沿 `z=-8700` 展示线排列，供原型视觉对比。

## Hurd 与墙体现场

Hurd 北门和外侧防区为 `x=-6000..6000, z=24000..33000`，墙体与 `blocks_airborne` 碰撞同步扩宽；控制区内缩到 `x=-5000..5000, z=25500..31500`。原北侧中央刷怪区拆成 `x=-20000..-9000` 与 `x=9000..20000` 两翼，均为 `z=14000..22000`。Hurd 旗帜及四名固定 actor 由 session 生成；Maid 四人及位于 `(-12000, 0)` 的 flag 1 也由 session 生成，Hurd 使用 flag 2。

当时原 12.3 m 可见长墙已替换为 component boundary wall；开发坡道和可站立 air gate 保留。旧南侧背景墙从世界外 `z=-45000` 移到 `z=-33000`，world bounds 留出墙厚，外墙显式阻止 airborne 越界。
