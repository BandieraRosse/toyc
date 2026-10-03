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

运行 `--gpu-scene-pose-test` 检查源坐标平移/倍率变化下的物理尺寸、握点闭合、关节可达、腕部方向、
胸部代理间隙及冻结重放，再用[角色保真工具](../guides/character-fidelity.md)检查左右手、肩袖与枪托。
代理体检查不代替网格近景检查；手指与握把接触处允许有限贴合，当前没有逐三角形自碰撞或手指接触求解器。
