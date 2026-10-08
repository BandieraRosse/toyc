# 玩家移动配置

> 状态：当前
> 所有者：Rasterfall Game / session / 启动参数
> 事实入口：`rasterfall/config/player-movement.cfg`、`rasterfall/lib/game_player_movement.inc`

玩家运动在启动时读取 `rasterfall/config/player-movement.cfg`，修改后重启游戏即可生效，无需重新编译。
仓库配置的跑速为 17.812 m/s，是原配置的两倍；加减速和垂直跳跃参数保持原值。
未加载配置时的内置基准仍约为 8.91 m/s，逻辑 fixture 使用该基准。
在源码工作区修改仓库中的文件；Windows `NativeCodex.ps1 build` 及各暂存命令都会复制它。
配置同时位于 `build-windows/rasterfall/config/` 和 `build-windows/rasterfall-windows/rasterfall/config/`，
各自供同目录的 exe 读取。直接运行分发目录里的 exe 时可修改分发目录中同一路径的文件，
再次构建或暂存会以仓库版本覆盖同名配置。

个人调参文件可以放在暂存目录外，通过 `--movement-config <path>` 指定；Windows 原生程序将相对文件
路径定位到 exe 所在目录，其他平台以程序工作目录为准，所以个人文件建议使用绝对路径。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --movement-config C:\path\player-movement.cfg
```

配置为 UTF-8 文本，允许 BOM、LF/CRLF、空行、行首/行尾空白和 `#` 注释；每行一个 `key = value`。
没有列出的键保留内置默认值。米制值最多三位小数，时间必须为非负整数毫秒。
未知键、重复键、非法数字或越界会报告文件、行号和原因，并阻止启动；不会部分采用错误配置。
默认文件不可读取时，普通启动报告使用内置默认值；明确指定文件或运行配置检查时，不可读取即失败。

| 参数 | 单位 / 范围 | 作用 |
| --- | --- | --- |
| `move_speed_mps` | m/s，0.5～30 | 基础地面/空中速度上限，仍乘玩家移动技能倍率 |
| `ground_accel_mps2` | m/s²，1～300 | 地面起步与同向加速 |
| `ground_brake_mps2` | m/s²，1～300 | 松开方向键时减速 |
| `turn_accel_mps2` | m/s²，1～300 | 输入方向与当前速度反向时加速 |
| `air_accel_mps2` | m/s²，0～100 | 空中方向微调，0 禁用微调 |
| `jump_speed_mps` | m/s，1～20 | 垂直起跳速度 |
| `gravity_mps2` | m/s²，4～100 | 普通玩家跳跃/掉落重力 |
| `fall_terminal_mps` | m/s，1～100 | 普通玩家最大下落速度 |
| `coyote_ms` | ms，0～250 | 离边起跳宽限，0 禁用 |
| `jump_buffer_ms` | ms，0～250 | 落地前跳跃输入缓冲，0 禁用；普通地面按跳仍生效 |

例如，降低跑速、保持地面响应并增强跳跃：

```ini
move_speed_mps = 7.5
ground_accel_mps2 = 60
jump_speed_mps = 8.0
```

起跳速度和重力共同决定跳高与滞空，跑速和起跳时实际速度共同决定跳远；不要把其中一个值当作独立跳高开关。
现有确定性运动仍使用 60Hz 逻辑步、512 RFU/m；速度和重力四舍五入到整 RFU/步及 RFU/步²，
地面/空中加速度保留 1/1024 RFU/步²精度，容错时间向上取整到逻辑步。
因此很小的速度或重力修改可能落在同一个整数档位。用 `--movement-config-check` 查看实际解析结果，
它只检查配置并打印换算后的参数，不创建窗口。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --movement-config-check
```

配置归启动 policy 所有，session 在换图/重试后重新应用，运动真值由 Game 持有；没有运行中热加载。
RTS 本地玩家沿用同一速度。队友、感染者、特殊攻击击飞及 RTS 镜头速度保留各自规则。
当前可信客户端位置上报协议没有同步这份配置，联机各端应使用相同文件；本地玩家和客户端预测共用同一实现。
自动逻辑回归使用内置默认值，并另外验证自定义运动、零容错、配置拒绝、换图持久与客户端预测。
