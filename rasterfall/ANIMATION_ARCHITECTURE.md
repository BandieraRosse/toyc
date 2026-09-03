# Rasterfall 模型与动画架构

> 最后更新：2026-09-03
> 依据提交：`2698c813cbd04ed2c197dc84101ae02f0b9b02a4`（增加 Rasterfall 代码导航文档）

本文说明运行时模块边界、扩展入口和当前仍需控制的技术债。格式细节仍以各公共头文件和
转换工具为准。

## 数据流

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
- `rasterfall_model.*`：RFM2 资源、模型姿态求值、IK、grant、骨骼更新和蒙皮。

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

## 姿态求值顺序

`rasterfall_model_sample_clip` 的顺序是稳定契约：

1. 清空上一帧局部动画平移并应用绑定后的 root motion。
2. 采样 clip 的局部旋转。
3. 求解腿部 IK；解析式解算失败时才进入 CCD。
4. 应用 PMX grant/inherit。
5. 重建全局骨骼变换，供蒙皮和渲染读取。

改变顺序会改变 IK 目标空间或 grant 结果，必须同时运行完整 walk 连续性扫描。

## 当前约束与后续方向

- `rasterfall_model_asset` 目前同时拥有不可变网格数据与可变姿态状态，适合当前单实例
  角色，但同一资源的大量角色实例仍会重复资源。下一阶段应拆成可共享
  `model_resource` 和每角色 `model_instance`，不能只复制现有大结构体。
- inspector 的详细 IK trace 仍存放在模型结构中。新增诊断应优先放入可选 observer/
  snapshot，避免继续扩大运行时热数据。
- glTF 动画库实现仍由 `app/glb_inspect.c` 以 library 模式编译。若继续扩展 glTF channel，
  应把解析与采样移入 `rasterfall/src/`，CLI 只保留输出和测试。
- 当前 runtime clip 以骨骼局部旋转为主；加入通用骨骼平移、缩放或动画混合时，应增加
  独立 pose buffer 和 channel mask，不要继续增加 VMD 专用旁路状态。

## 回归要求

涉及上述边界的修改至少验证：

```sh
make app-vmd-inspect app-glb-inspect app-rasterfall
build/vmd_inspect <walk.vmd> <model.rmesh> --vmd-leg-trace
build/vmd_inspect <walk.vmd> <model.rmesh> --vmd-walk-final-flips
```

如果修改 RFM2 格式、公共重定向数学或构建目标，还需扩大到相关转换工具、Windows 构建
以及自托管应用构建。
