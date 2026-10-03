# 武器模型与游戏尺度适配

> 状态：当前；所有者：角色表现与 calibration profile

`rasterfall_weapon_model_adapt` 是主动武器的文件坐标到游戏呈现边界。资产源网格保持原单位，
角色体型、动画骨架和镜头距离不决定枪的尺寸。姿态求值见[动画架构](../architecture/animation-architecture.md)。

## 坐标与尺度

1. 文件顶点减去源包围盒中心。
2. profile 的 `asset_basis` 转为统一武器坐标：+X 右、+Y 上、+Z 枪口。
3. 用纵向包围盒长度与 `length_mm` 求缩放，按 `rasterfall_units.h` 的 512 RFU/米换算。
4. 组合已求解的武器刚体帧和 actor 世界变换。身体的 `position_scale` 与展示缩放只用于换算握点；
   不再次缩放武器几何。CPU 顶点路径和 Scene 实例矩阵消费同一个 adapter。

adapter 返回中心、轴变换、整数 `scale_milli`、参考长度及量化后的实际长度。整数矩阵边界可能产生
毫米级误差，应检查实际长度，不能把字段中的参考数字当成实测结果。无物理参考的旧 profile 保留
`base_scale_milli` 兼容路径；新增正式武器应填入真实长度并校准握点。

当前 AK 家族网格按固定枪托 AKM 的 **880 mm 总长**标定。参考来源为
[Small Arms Survey 2009 第三章中的实物 AKM 尺寸](https://www.smallarmssurvey.org/sites/default/files/resources/Small-Arms-Survey-2009-Chapter-03-EN.pdf)。
这是现有游戏网格的尺寸依据，不表示该网格已满足某个制造年份的精确工程外形。

## 武器接触帧

`rasterfall_weapon_asset_profile` 保存规范 RFU 空间的数据：

| 字段 | 用途 |
| --- | --- |
| `PRIMARY_GRIP` | 扳机手位置和完整掌心朝向 |
| `FOREGRIP` | 支撑手位置和完整掌心朝向 |
| `STOCK` | 枪托贴肩参考；允许有限肩窝调整 |
| `SIGHT` | 机械瞄具参考，供后续精细贴腮标定 |
| `MUZZLE` | 枪口、火光与朝向诊断 |
| `MAGAZINE` | 弹匣参考 |
| `clearance` | 枪托前段、机匣、握把和弹匣的视觉避让胶囊 |

握点位置和四元数是物理尺寸空间中的接触帧，不是每个角色的手腕 Euler 角。替换源文件后须重新
检查纵轴、中心、长度、握把与护木的表面位置；不能只改一个缩放数后沿用不匹配的握点。
胸部避让椭球由骨架躯干比例生成，重装增加前方余量；这些体积仅约束动画，不参与玩法碰撞。

旧私有角色 pose editor 的缩放与胸挂点路径仍是兼容诊断，不属于此主动武器合同。
当前身体不满足自然臂展时应修订源骨架与蒙皮，不能拉伸手臂或缩小步枪掩盖问题。

## 接入验证

手枪、SMG、霰弹枪和 AWP 同样使用完整的接触帧与物理单位 adapter；各自的尺寸、轴向、
前握点和掌心朝向由 calibration profile 拥有。当前现实尺寸依据如下；总长约束使用规范枪轴，
不包含额外火光，也不按角色展示缩放二次放大。网格仍是现有美术模型，总长一致不表示外形达到工程精度。

| 枪械 | 总长标定 | 现实参考与配置 |
| --- | --- | --- |
| 手枪 | 204 mm | [GLOCK 17 官方规格](https://us.glock.com/en/products/law-enforcement/pistols/g17)，标准 G17 |
| SMG | 267 mm | [美国司法部收录的 Ingram M10 资料](https://www.ojp.gov/pdffiles1/Digitization/000606NCJRS.pdf)，无枪托 10.5 英寸，换算取整；对应当前无枪托网格 |
| 霰弹枪 | 1003 mm | [Mossberg 590S 官方规格](https://resources.mossberg.com/hubfs/press_releases/Mossberg%20Releases%20590S%20Shotgun%20Line%20PR%2010-25-2021%20FINAL.pdf)，18.5 英寸枪管、完整枪托版本总长 39.5 英寸；作为现有通用泵动模型的尺寸参考 |
| AK | 880 mm | 上述固定枪托 AKM 实测资料 |
| AWP | 1120 mm | [Accuracy International 原厂手册第 3 页](https://www.indaginibalistiche.it/utlities/manuali/accuracy_international_aw_sniper_EN.pdf)，AWP、两块枪托垫片；区别于更长的 AW |

手枪左掌托住扳机手，SMG 左掌握前方竖握把；短枪的动作支点是
`PRIMARY_GRIP`，不会把无枪托模型强贴肩窝。霰弹枪握泵柄后段，AWP 支撑长护木。
`hold_kind` 选择肩托、双手手枪或紧凑双握把，反冲角度与后移距离也由 profile 提供；
角色骨长、rest pose 与资源网格不随枪型改变。武器资源按目录路径加载，不依赖展示 gallery 的容量。

新增枪型或调整握点后，用[三角色五枪循环区](../guides/gpu-scene-fixture.md#三角色五枪循环区)
对照不同体型、仰俯角、移动持枪和反冲。短枪的左右掌朝向可以不同，以保持托握时腕部连续。

运行 `--gpu-scene-pose-test` 检查源坐标平移/倍率变化下的物理尺寸、握点闭合、关节可达、腕部方向、
胸部代理间隙及冻结重放，再用[角色保真工具](../guides/character-fidelity.md)检查左右手、肩袖与枪托。
代理体检查不代替网格近景检查；手指与握把接触处允许有限贴合，当前没有逐三角形自碰撞或手指接触求解器。
