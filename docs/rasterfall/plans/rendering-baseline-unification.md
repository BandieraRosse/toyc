# CPU 与 GPU Scene 默认渲染统一

> 状态：活动
> 所有者：Rasterfall renderer presentation
> 开始：2026-09-27

## 目标

为 CPU 与 GPU Scene 确定同一份默认功能描述，使同一 world、相机、游戏状态和资源在切换后端后保持相同内容、层序、角色动作及大致相同的整体观感。实现可以不同；只有具体功能存在语义差异时才提取共享求值。执行格式仍分别为 CPU RasterCmd 和 GPU Scene mesh/batch。共同集合和高级功能原则见[baseline 清点](../reference/rendering-baseline.md)。

## 工作顺序

1. 对照正常单人镜头清点 CPU 与 Scene 的内容及现有用户开关；区分默认功能、可选画质、内容策略、后端选择和诊断参数。
2. 明确默认功能的对外描述及各后端能力状态。优先处理会改变玩家所见功能的差异；独立实现只要符合合同便可保留。CPU 边线默认值与纹理开关作用范围是当前已知决策点。
3. 高级功能逐项提供显式选择与能力反馈：GPU 可以正式支持，CPU 可以实验支持或不支持。动漫高模/PMX/VMD 恢复单独设计，不能仅重新打开旧 CPU 宏。
4. 用少量 Windows native CPU/Scene 同输入场景检查内容、动作、遮挡、层序、透明、HUD 和整体观感。只追踪明显缺项或语义差异，不以逐像素相等或共用所有几何代码作为门槛。

## 当前进度与风险

- 已有共享 canvas 的 SKY/HUD、viewmodel 几何入口、程序角色/部分敌人几何及 map/world 冻结值；仍存在独立来源与提交逻辑。
- 正常 CPU 与 Scene 的 UI 状态决策已开始共用 `rf_game_shared_ui_layout`；这只覆盖一部分，尚无完整画面等价签收。
- Scene 只支持单人 Runtime Map；联机 CPU 画面不在本轮共同 baseline。
- 旧动漫渲染由 `RASTERFALL_LEGACY_ANIME_RENDERING_ENABLED=0` 关闭；恢复需要资产与双后端合同。

## 完成门槛

- reference 清点中的每一项有明确的功能描述和双后端支持状态；默认帧不依赖后端特有的隐式展示规则，不强制共用全部求值或几何代码。
- 代表性镜头覆盖 Outpost/Campaign、FPS/RTS、角色与敌人、交互物、透明和 UI 状态；内容和主要观感大致相同，明显缺项与语义差异已处理。光栅化边缘及采样造成的像素差异不要求消除。
- Windows native 构建、逻辑、GPU 提交、窗口/present 验证通过；性能和失败传播没有明显回退。
