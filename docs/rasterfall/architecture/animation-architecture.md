# Rasterfall 模型与动画架构

> 状态：当前
> 所有者：Rasterfall 模型与动画求值
> 最近核对：2026-10-03

本文说明运行时模块边界、扩展入口和当前仍需控制的技术债。格式细节仍以各公共头文件和
转换工具为准。

新角色骨架、rest pose、attachment 和 skinning 的离线输入规范由
[`character-assets.md`](../reference/character-assets.md) 唯一拥有；本页拥有导入后的动画求值顺序。

## 数据流

Humanoid Action Composition V1 的正式边界为：

```text
toy_game_actor animation semantic + deterministic time
                    ↓
rasterfall_action_layer（LOWER_BODY / UPPER_BODY / recoil delta / evade delta）
                    ↓ rasterfall_action_compose
rasterfall_model_instance finalized pose
                    ↓
human/weapon socket query → attachment / weapon / rendering
```

`toy_game_actor` 不持有 layer、动作资源、track、骨骼索引或最终姿态。modular presentation adapter
保留逐 actor lower locomotion，使 gameplay semantic 临时切到 FIRE 时仍能组合 WALK；action evaluator
是 semantic role 到目标骨架 stable ID 的唯一 runtime 适配点。`model_instance` 仍拥有求值后的可变姿态。
V1.1 提供 lower IDLE/WALK、upper RIFLE_IDLE/AIM/FIRE，以及 ADDITIVE 的 RIFLE_RECOIL。
战斗避让追加一个有界 secondary additive 槽，加载公开的 EVADE_LEFT / EVADE_RIGHT RFANIM。
两个 additive 都是局部旋转 delta，只允许 spine/chest、双肩和双臂，禁止 root、hips、legs。

求值顺序固定为 reset bind/base pose → apply LOWER_BODY role mask → apply UPPER_BODY role mask →
apply recoil delta → apply secondary evade delta → final bone update → 左手 attachment IK →
socket/attachment/weapon/render。lower mask 是 root、hips 和双腿；
upper mask 是 spine 至双手，additive mask 是 spine/chest、双肩和双臂。RFANIM track 越界到错误层会失败，
不允许 upper action 偶然覆盖腿或 recoil 修改 lower ownership。weapon target debug 以
finalized character `WEAPON_R` 对齐 canonical weapon `PRIMARY_GRIP`，再变换 `FOREGRIP` 得到左腕目标；
正式 RFANIM 的 RIFLE_IDLE/AIM/FIRE 先提供双臂与双手基准姿态，modular presentation 再对左上臂/前臂
执行两骨骼 attachment IK，使左手跟随同一把枪的前握点。开发者 world strip 复用同一 composition 与
左臂解算。

`rasterfall_action_composition.secondary_additive_weight_milli` 明确限制为 0..1000；0 不应用额外层，
旧三层调用保持原行为，1000 直接应用采样 delta，中间值从 identity 到 delta 插值。只保留一个额外槽，
不引入动态 layer 列表。左右避让资源均为 500ms 非循环动作，首尾零旋转；缩肩和侧倾不产生 root motion，
也不改变 actor 的权威位置、朝向、碰撞或命中体。

`rasterfall_actor_evasion_sample` 属于 presentation adapter。它只读权威回避 sequence、generation、剩余
动画时间、来源与强度，在逐实例历史中锁定该次方向和权重；后续命中和转向不重启或翻转这次动作。
控制、腾空、死亡、倒地或复活会禁止当前事件的姿态；解除状态后同一 sequence 不重新播放。
新事件或 actor generation 更新才解除该表现抑制。计时仍来自固定步玩法，重复提取不推进时间。

```text
VMD / glTF / 程序生成动画
          │ 导入、单位换算、骨骼名解析
          ▼
rasterfall_animation_clip（格式无关）
          │ 时间采样
          ▼
模型局部旋转 + root motion
          │ IK → grant → 全局骨骼更新
          ▼
skinning / rendering
```

模块职责如下：

- `rasterfall_character.*`：正式 actor 的稳定角色目录。规则和网络只保存 `character_id`，
  目录负责角色名称、默认调色板、模型入口和动作能力；新增角色不应在渲染循环中增加名称
  特判。当前 Akari、Mio、Ren、Yuki 使用程序化低模，后续可逐项替换为 RFM2 模型。
- `rasterfall_animation.h`：格式无关的 clip、track、player 和四元数采样。不得依赖游戏
  状态、某个角色名称或文件格式。
- `rasterfall_action.*`：RFANIM 的动作资源与 LOWER_BODY、UPPER_BODY、ADDITIVE 组合入口。
  它把 stable humanoid role track 写入逐实例局部姿态；最终 pose 仍归 `rasterfall_model_instance`。
- `rasterfall_actor_animation.h`：玩法动作的程序化表现采样。Block 由
  `render/rasterfall_block_character.inc` 将特殊动作采样适配为标准骨骼旋转；下身和 AK 持枪
  复用 RFANIM 与共享 rifle solver。该适配不进入通用骨骼动画层。
- `rasterfall_vmd.*`：VMD 解码和 VMD 语义分类。通过骨骼 resolver 映射目标模型，
  不直接绑定某个角色。
- `rasterfall_glb_animation.h`：glTF 动画输入和 humanoid 源姿态。
- `rasterfall_humanoid*`：格式无关的解剖角色、静止基向量和重定向。
- `rasterfall_model.*`：RFM2 resource、逐角色 model instance、姿态求值、IK、grant、骨骼更新和蒙皮。

正式 RFCHAR 的 resource 独占 backing、纹理、mesh、静态 skeleton/IK/grant 与 CHR1 定义；
instance 独占局部姿态、全局变换、root motion、solver history/cache 和 attachment IK pole 状态。
不得对 resource definition 调用 pose/IK API；resource 必须晚于全部 instance 释放。
CPU skinning 与 stable socket 都从 instance API 查询。

正式 RFCHAR 数据流为：

```text
one rasterfall_model_resource (immutable after load)
                 ↓ shared read-only by
many rasterfall_model_instance (isolated pose/solver state)
                 ↓ finalized instance pose
CPU skinning / stable attachment query / rendering submission
```

world position、actor yaw 和 presentation scale 不属于 instance。当前 evaluator 继续使用
`rasterfall_model_asset` 布局兼容 pose view，以避免 V1 重写 pose representation；每个 instance
复制 mutable bone records、bone transforms、animation/root-motion 及 inline solver state，网格、纹理、
bone order 和 IK definition 指针共享 resource。compatibility shadow 会重复静态 bone 字段，静态事实的
canonical owner 仍是 resource，后续 Pose Buffer V2 再消除此布局债。

被动 rigid follower 在上述 finalized pose 之后求值：renderer 从 instance 读取 HEAD/BACK socket，
组合 attachment mount correction 与 actor/world transform，再提交无骨架 RMESH resource。它不回写
pose，不共享 instance mutable storage，也不进入武器 placement/双手 IK 的约束求值阶段。

主动步枪由 `rasterfall_rifle_pose` 统一求值，CPU actor、目录台位和 Scene extraction 共用。
输入是只读展示值：肩托瞄准与低位移动权重、玩法 pitch、相对 yaw、目标距离、反冲和护甲余量。
`rasterfall_rifle_sample` 以 combat simulation 毫秒推进持枪历史；AK 移动时采用低位持枪，移动 FIRE
保留低位并叠加反冲；停止移动后，目标获取或 FIRE 进入肩托瞄准。真实 AK AI 静止且无目标时，
在肩托警戒、右手竖持／左臂下垂、胸前斜上持枪、放松低持之间随机轮换；每次保持数秒，
缓慢过渡且不连续重复。actor/generation 派生的独立随机序列只属于表现历史，不消耗玩法 RNG。
发现目标、移动、重装填、切枪、近战、投掷、腾空、失能和非存活状态中断待机；战斗持枪更快接管。
展示台通过 owner 显式提供普通 AI 的只读输入、独立历史和展示时钟，复用同一随机待机逻辑；
玩法内标记为 developer-only 或 animation-demo 的角色仍由其专用动作控制。
重复冻结同一时间不推进，actor/generation 变化或时间回退重置历史。
历史属于 renderer/local source；冻结帧只保存求值输入，重放不查询 Game 或更新采样器。

手枪、SMG、霰弹枪、AK 和 AWP 都经过同一 sampler 与双臂 solver。移动持枪保持目标跟踪；
枪型差异由 calibration profile 的接触帧、持枪支点和反冲参数决定。短枪瞄准时抬高并前伸，左掌分别
托握或握竖把；短枪随机待机映射为双手低位警戒，长枪保留单手竖持、胸前斜持和低持。
求解短枪肘部时用掌骨方向引导弯曲平面，避免把步枪的外展配置直接用于双手相邻的手枪握点。
RFCHAR 目录采样显式携带 `weapon`，CPU actor、Block 和 Scene 使用同一套枪型配置。

RFANIM lower/upper 与 recoil/evade additive 完成后，求解顺序为：

1. 将有界仰俯和左右瞄准分配给脊柱、胸、颈和头，保留基础站姿、行走与回避。
2. 根据右肩和骨架比例构建枪托接触，待机、低位移动与瞄准连续过渡；护甲增加前方余量。
   AK 平视瞄准接触下移到肩窝，低位移动在肋侧持枪；大幅仰俯时逐渐抬高低位支点以保持臂展和腕部约束。
   低位移动的枪托进一步下沉并向身体内侧收拢，胸部减少侧转、颈部抵消基础侧身，让平视时的头朝向回正。
   低位移动与肩托瞄准都跟踪射击方向，不把低位动作当作向地面压枪。
3. 沿既有玩法眼高、pitch 与目标距离构造展示瞄准点，迭代枪口收敛、胸部代理避让与有限肩窝调整。
   肩窝调整最多 40 RFU；闲置枪托不贴肩，允许有界扩大到 240 RFU 的握持位置调整，不缩放肢体。
   小于两米的目标采用两米视觉收敛下限，玩法射线保持原值。
4. 在肩部支点叠加反冲，求扳机臂的完整 attachment IK，然后从最终 `WEAPON_R` 和
   `inverse(PRIMARY_GRIP)` 取得枪的最终刚体帧，再闭合支撑臂 `FOREGRIP`。
   单手竖持沿枪轴旋转以保持腕部自然；左臂与左手指节向放松下垂姿态混合，不再强制锁定前握点。
5. 生成最终 palette、被动装备和武器矩阵；绘制阶段不再移动枪或手。

两个握点都约束位置和掌心朝向。求腕目标时先旋转 authored socket offset，再求肩肘链和手腕；
肘部 pole 由枪下方向、掌骨方向及身体外侧方向引导；抱枪和单手举枪适量增加外展，
极端仰俯减少外展，保留自然腕部约束。所有武器 socket 使用 RFU，身体 socket 按
`position_scale` 换算；512 与 65536 authored units 使用同一路径。
武器物理长度、文件轴向与握点配置属于[武器适配合同](../reference/weapon-model-adapter.md)，不属于动作 clip。

可选指节链由 action composition 在持枪上身层后求值，随后跟随 HAND 参与最终 palette；
它不移动挂点、不修改 rest。扳机食指与其余包握手指分别弯曲，退出持枪 composition 时复位。
指节命名见[角色资产合同](../reference/character-assets.md)。当前使用原创基础动作和有界程序瞄准，
没有导入商业动作包，也没有新增通用动画图或逐手指接触求解器。

`--gpu-scene-pose-test` 覆盖低持枪、瞄准、移动持枪/射击及其过渡、反冲、仰俯/左右角度、近距离目标与护甲，
并验证握点闭合、可达性、腕部方向、胸部代理间隙、指节复位、源模型尺度变化及 CPU/Scene 冻结一致性。
闲置回归覆盖停留、随机轮换、战斗中断、冻结幂等、时间回退，以及三种持枪与肩托之间的双向过渡。
骨骼 Euler 的整数精度使最终枪口存在小角度误差；测试与近景检查分别验证数值边界和视觉效果。
表现求值不回写命中、伤害、actor 朝向或网络真值。

## 扩展新动画格式

新的导入器应输出 `rasterfall_animation_clip`，并完成以下工作：

1. 把源骨骼名或源节点映射为模型骨骼索引。
2. 设置 `translation_scale`，把源格式平移单位转换为 RFM2/world 单位。
3. 保证每条 track 的 keyframe 按时间递增，并声明循环语义。
4. 在导入层识别格式专有通道；模型解算器不应出现新的格式名称或单位常量。

同骨架动画可以使用精确名称 resolver；异骨架动画应先映射到 humanoid roles，再使用
rest basis 重定向。不要在 VMD、glTF 解析器里添加目标角色专用分支。

## 扩展新模型

新模型应首先通过 RFM2 层验证层级、权重和 IK metadata。运行时需要：

1. 调用 `rasterfall_model_map_humanoid` 检查通用中英文/MMD 名称映射；特殊命名应在独立
   profile/resolver 中补充，而不是修改动画格式解析器。
2. 用 `rasterfall_model_bind_root_motion` 一次性绑定主、次 root motion 骨骼；逐帧仅
   更新平移值。
3. 缺少 PMX 腿部 IK metadata 的模型走 humanoid 重定向路径，不应假设存在
   `左足ＩＫ`/`右足ＩＫ`。
4. 用 inspector 检查骨骼覆盖率、父子链、腿部连续性和最终全局旋转。

## 扩展正式角色与关键动作

新增角色时先在 `rasterfall_character` 注册稳定 ID、动作能力与资源入口，再由 actor 的
`character_id` 选择它。不要用 actor 名字、AI 等级或模型路径充当身份。网络协议必须同步
这个 ID，缺失资源时应回退到程序化 actor，保证规则模拟不依赖私有美术资源。

游戏动作以 `toy_game_animation_id` 为稳定语义层。idle/move/fire/reload/hit、近战、投掷、
倒地、死亡和复活都先进入同一动作采样入口；角色专属 clip 在目录或后续 animation set
中覆盖这些语义。导入器仍只负责生成格式无关 clip，不能反向依赖某个游戏动作。

玩法动作时钟与 authored clip 时钟是两个时间域：玩法 `MOVE` 的 400ms 循环服务确定性状态
和网络同步，动漫角色的 VMD walk 则按自身 clip 时长采样。角色渲染在组合 locomotion 时
使用独立展示时钟，并跨越 MOVE 计时回卷累计真实经过的时间；不能把 `actor->animation.time_ms`
映射为整个 clip 的时间，否则多秒 walk 会在一个玩法 MOVE 周期内快速播完。
RF Humanoid modular actor 同样遵守该规则：其 RFANIM WALK 使用逐 actor presentation accumulator，
400ms gameplay 回卷只贡献 elapsed delta，不直接成为 800ms authored clip 的采样时间；upper-body
射击覆盖不会重置已经累计的 lower-body 相位。

## 姿态求值顺序

`rasterfall_model_sample_clip` 的顺序是稳定契约：

1. 清空上一帧局部动画平移并应用绑定后的 root motion。
2. 采样 clip 的局部旋转。
3. 求解腿部 IK；解析式解算失败时才进入 CCD。
4. 应用 PMX grant/inherit。
5. 重建全局骨骼变换，供蒙皮和渲染读取。

改变顺序会改变 IK 目标空间或 grant 结果，必须同时运行完整 walk 连续性扫描。

## 当前约束与后续方向

- RFCHAR 的 resource/instance ownership 已建立并迁入 Character Acceptance；gameplay actor、Eula
  高模与 PMX/VMD inspector 仍走 legacy asset API，等待按风险逐条迁移。
- instance 当前保留完整 inline solver diagnostics/cache，确保所有可变 solver 历史隔离；这比热路径
  理想布局更大。冷 diagnostics observer 与独立 Pose Buffer 属于后续优化，不在 V1 ownership 中完成。
- inspector 的详细 IK trace 仍存放在模型结构中。新增诊断应优先放入可选 observer/
  snapshot，避免继续扩大运行时热数据。
- glTF 动画库实现仍由 `app/linux/glb_inspect.c` 以 library 模式编译。若继续扩展 glTF channel，
  应把解析与采样移入 `rasterfall/src/`，CLI 只保留输出和测试。
- 当前 runtime clip 以骨骼局部旋转为主；加入通用骨骼平移、缩放或动画混合时，应增加
  独立 pose buffer 和 channel mask，不要继续增加 VMD 专用旁路状态。
- RFANIM V1 固定容量、纯旋转、step/linear 插值；Composition 仍只有固定 role mask，两个有界
  additive 仅进行局部四元数 delta 叠加；secondary 支持 identity 权重，没有通用 pose 混合、
  animator graph、IK target channel 或 root motion。
  `weapon_target` 等非骨骼 semantic channel 应在扩展格式时增加显式 channel kind，不能伪装成骨名。

回归命令和矩阵见[资产导入与诊断指南](../guides/asset-pipeline.md)。`rasterfall` 的 `--vmd-*` 参数属于旧 PMX/VMD 兼容诊断，不是新 RFCHAR 角色的默认开发者预览路径；正常启动不显示 Eula/VMD 私有预览，正式 Eula gameplay actor 存在时仍按 profile 懒加载 walk clip。
