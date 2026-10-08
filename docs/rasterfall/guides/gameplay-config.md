# 玩法配置

> 状态：当前
> 所有者：Rasterfall Game / session / 启动参数
> 事实入口：`rasterfall/config/gameplay.cfg`、`rasterfall/include/toy_gameplay_fields.inc`、`rasterfall/lib/game_gameplay_config.inc`

通用玩法参数在启动时从 `rasterfall/config/gameplay.cfg` 读取，修改后重启生效，无需重新编译。
玩家跑速与跳跃继续使用独立的[玩家移动配置](player-movement-config.md)。

| 配置组 | 可调内容 |
| --- | --- |
| `wave_*` | 首波等待、波间休息、预告时长、总波数、规模百分比、每波刷怪持续时间 |
| `base_*` | 基地核心出生/最大生命、每恢复 1 HP 的间隔 |
| `shove_*`、`charger_shove_stun_ms` | 推搡距离、推开距离、普通感染者与 Charger 僵直时长 |
| `pill_heal_range_rfu` | 面向友军使用药丸的治疗距离，独立于推搡距离 |
| `melee_*` | 斧头的伤害及有效距离 |
| `throw_*` | 投掷初速度、重力、反弹速度保留比例、反弹次数和碰撞半径 |
| `bomb_*` | 炸弹伤害、爆炸半径和落地后的引信时间 |
| `molotov_*` | 燃烧区域半径、持续时间、伤害间隔及每次伤害 |
| `evasion_*` | 回避优惠窗口、再触发间隔、受击后恢复等待、窗口内枪弹/近战成本、基础近战成本 |

文件内每项都有注释。所有值为整数：`_ms` 是毫秒，`_rfu` 是世界单位，512 RFU = 1 米；
`_rfu_per_second` 和 `_rfu_per_second2` 分别为 RFU/s 和 RFU/s²；`_percent` 为百分比，
`_per_mille` 为千分比。例如反弹保留比例 700 表示保留 70% 速度。
所有玩法计时仍在固定逻辑步推进，实际事件时间受逻辑步量化。

缩短波间休息、提高斧头伤害、缩短炸弹引信的示例：

```ini
wave_pause_ms = 30000
melee_damage = 40
bomb_fuse_ms = 2000
```

通过检查命令取得当前完整参数、解析值与允许范围，不创建窗口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --gameplay-config-check
```

配置为 UTF-8 文本，允许 BOM、LF/CRLF、空行、空白及 `#` 注释，每行一个 `key = value`。
未列出的键使用内置默认值。文件最大 8192 字节，每行最多 255 字节。
未知键、重复键、非整数、越界、内嵌 NUL 或超长文件/行会报告文件、行号及原因并阻止启动；
非法文件不部分生效。回血和燃烧伤害间隔必须大于零，避免计时循环无法推进。
投掷重力下限保证在正常固定步中不会舍入为零；具体范围以检查命令输出为准。
`bomb_fuse_ms = 0` 表示落地立即爆炸；零推开距离和零僵直时长允许单独关闭相应效果。

Windows `NativeCodex.ps1 build` 及各暂存命令都复制整个 `rasterfall/config/` 目录，
同步到 `build-windows/rasterfall/config/` 和 `build-windows/rasterfall-windows/rasterfall/config/`。
在源码工作区调参应修改仓库文件；直接运行分发目录的 exe 时修改分发目录同一路径的文件。
再次构建或暂存会用仓库版本覆盖同名配置。Windows 原生程序将相对文件路径定位到 exe 所在目录。
个人文件可放在暂存目录外并使用绝对路径明确选择：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --gameplay-config C:\path\gameplay.cfg
```

默认文件不可读取时普通启动报告使用内置默认值；显式指定文件或检查该配置时，不可读取即失败。
启动会验证移动与玩法两份配置，任意一份非法均阻止进入 runtime。
随包配置将旧地图的波次威胁规模设为 400%；独立 Game 的内置默认值仍为 100%，没有运行中热加载。波次配置只作用于 Game 波次导演，不覆盖边缘站点等
session 局部任务导演的配额与流程。

启动层负责磁盘 I/O，Game 负责纯解析与实例内规则；session 保留启动规则，换图和重试重新应用，
包括首波与回血初始计时。HUD 从 Game 配置读取总波数。每个 Game 可使用独立配置，
自动逻辑测试沿用内置默认值并另验证自定义配置。
当前协议未同步配置，联机各端应使用相同文件；伤害与波次仍由主机结算。

枪械内容表、感染者模板、技能能力表和动作表现时长仍由各自模块定义；已提取参数的旧宏仅提供内置默认值。
数组容量、实体 ID、协议字段和碰撞/导航结构常量继续保留编译期定义。
