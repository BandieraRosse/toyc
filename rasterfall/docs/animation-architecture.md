# Rasterfall 模型与动画架构

> 文档更新：2026-09-10
> 源码核对基线：工作区（Humanoid Action Composition V1.1 additive recoil；legacy PMX/VMD compatibility；PRIMARY_GRIP weapon presentation）

本文说明运行时模块边界、扩展入口和当前仍需控制的技术债。格式细节仍以各公共头文件和
转换工具为准。

新角色骨架、rest pose、attachment 和 skinning 的离线输入规范由
[`character-assets.md`](character-assets.md) 唯一拥有；本页拥有导入后的动画求值顺序。

## 数据流

Humanoid Action Composition V1 的正式边界为：

```text
toy_game_actor animation semantic + deterministic time
                    ↓
rasterfall_action_layer（LOWER_BODY / UPPER_BODY / ADDITIVE delta）
                    ↓ rasterfall_action_compose
rasterfall_model_instance finalized pose
                    ↓
human/weapon socket query → attachment / weapon / rendering
```

`toy_game_actor` 不持有 layer、动作资源、track、骨骼索引或最终姿态。modular presentation adapter
保留逐 actor lower locomotion，使 gameplay semantic 临时切到 FIRE 时仍能组合 WALK；action evaluator
是 semantic role 到目标骨架 stable ID 的唯一 runtime 适配点。`model_instance` 仍拥有求值后的可变姿态。
V1.1 提供 lower IDLE/WALK、upper RIFLE_IDLE/AIM/FIRE，以及 ADDITIVE 的 RIFLE_RECOIL。
recoil 是局部旋转 delta，不是完整 pose；当前只允许 spine/chest、双肩和双臂，禁止 root、hips、legs。

求值顺序固定为 reset bind/base pose → apply LOWER_BODY role mask → apply UPPER_BODY role mask →
apply ADDITIVE delta → final bone update → socket/attachment/weapon/render。lower mask 是 root、hips 和双腿；
upper mask 是 spine 至双手，additive mask 是 spine/chest、双肩和双臂。RFANIM track 越界到错误层会失败，
不允许 upper action 偶然覆盖腿或 recoil 修改 lower ownership。weapon target debug 以
finalized character `WEAPON_R` 对齐 canonical weapon `PRIMARY_GRIP`，再变换 `FOREGRIP` 得到左手目标；
它建立未来 IK 数据流但不在本版求解 IK。

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
- `rasterfall_actor_animation.h`：当前游戏角色的程序化表现层。它可以逐步被正式动作
  clip 替换，但不应进入通用骨骼动画层。
- `rasterfall_vmd.*`：VMD 解码和 VMD 语义分类。通过骨骼 resolver 映射目标模型，
  不直接绑定某个角色。
- `rasterfall_glb_animation.h`：glTF 动画输入和 humanoid 源姿态。
- `rasterfall_humanoid*`：格式无关的解剖角色、静止基向量和重定向。
- `rasterfall_model.*`：RFM2 resource、逐角色 model instance、姿态求值、IK、grant、骨骼更新和蒙皮。

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

active weapon presentation 与被动 rigid follower 分离：modular renderer 从同一个 finalized instance
读取 `WEAPON_R`，用 authored weapon-local `PRIMARY_GRIP` 求出 weapon origin，再派生
`FOREGRIP`、`MUZZLE` 和 `MAGAZINE` socket 供 renderer/debug 使用。此路径不使用 CHEST attachment、
`pose_calibration_local`、FOREGRIP 自动求解或 IK；CHEST/校准仍只属于 legacy acceptance/兼容诊断入口。

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
- glTF 动画库实现仍由 `app/glb_inspect.c` 以 library 模式编译。若继续扩展 glTF channel，
  应把解析与采样移入 `rasterfall/src/`，CLI 只保留输出和测试。
- 当前 runtime clip 以骨骼局部旋转为主；加入通用骨骼平移、缩放或动画混合时，应增加
  独立 pose buffer 和 channel mask，不要继续增加 VMD 专用旁路状态。
- RFANIM V1 固定容量、纯旋转、step/linear 插值；Composition V1.1 仍只有固定 role mask，新增
  additive 仅进行局部四元数 delta 叠加，没有权重混合、animator graph、IK target 求解或 root motion。
  `weapon_target` 等非骨骼 semantic channel 应在扩展格式时增加显式 channel kind，不能伪装成骨名。

## 回归要求

涉及上述边界的修改至少验证：

```sh
build/rfchar_runtime_test <rfchar.rmesh> # 含一 resource / 两 instance isolation
build/rasterfall --character-acceptance <rfchar.rmesh> <output>
build/rasterfall --action-composition-capture <model.rmesh> <lower.rfanim> <lower-ms> <upper.rfanim> <upper-ms> <additive.rfanim> <additive-ms> <output.bmp>
make app-vmd-inspect app-glb-inspect app-rasterfall
build/vmd_inspect <walk.vmd> <model.rmesh> --vmd-leg-trace
build/vmd_inspect <walk.vmd> <model.rmesh> --vmd-walk-final-flips
```

`rasterfall` 内置的 `--vmd-*` 参数属于旧 PMX/VMD 兼容诊断，不是新 RFCHAR
角色的默认开发者预览路径；正常启动不会显示 Eula/VMD 私有预览，但正式 Eula
gameplay actor 存在时仍会按 profile 懒加载 walk clip，保证其 MOVE 展示有动作来源。

如果修改 RFM2 格式、公共重定向数学或构建目标，还需扩大到相关转换工具、Windows 构建
以及自托管应用构建。
