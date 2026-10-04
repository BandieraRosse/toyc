# 玩家界面 V2 与 NULL 通讯

> 状态：活动
> 所有者：Rasterfall UI / runtime / presentation
> 授权：2026-10-04 用户任务书与参考图
> 开始时间：2026-10-04 11:39（Asia/Shanghai）

本轮依据仓库根目录 `Rasterfall_UI_v2_Codex_Development_Brief.md`，实施默认玩家界面、
终端与实验兼容模式、共享地图、设备窗口及真实 NULL 通讯。参考图只定义视觉方向；
布局、主题、业务查询、命令执行和剧情状态分别拥有自己的扩展入口。

## 执行顺序

1. 核对现有 native 基线、命令权限、输入、NULL 行为和 Scene 渲染边界。
2. 建立结构化玩家命令/查询、模式偏好与输入所有权；复用现有终端 registry。
3. 接入主题与共享布局组件、FPS/RTS HUD、地图缓存与任务投影。
4. 编织机玩家窗口、真实模型预览、配置/制造/领取与通知；列明其他设备覆盖。
5. 修复 NULL 待机根因，接入临时行为约束、实体镜头和低频真实世界采样。
6. 数据驱动两段通讯、回答与队列、任务事件和最小持久化。
7. Windows native 逻辑/输入/GPU/视觉检查、同场景性能采样与分阶段提交。

## 当前审计

- 当前分支为 main；开始时只有用户新增任务书和 references 未跟踪，没有已修改源码。
- 现有 HUD 的 canvas 已直接生成 Scene OVERLAY 几何，无需新全屏 CPU 图像上传。
- Developer Console 有 registry/session/权限，但正常运行受 Desktop feature gate 关闭；
  新玩家终端须独立接通 canvas，保留 Desktop 隔离。
- 编织机的制造真值在 Game，成品在 session；现有 UI 直接调用两者，需统一校验入口。
- NULL 永久 control_disabled 与自然 rifle idle 的条件冲突；修复驻地意图与临时通讯约束。
- Windows doctor 已确认 MSYS2、SDL2、Vulkan loader、运行目录；GPU 类型以 native 输出为准。

## 完成门槛

按任务书 V01–V16 逐项记录实际证据及缺口。GUI 与终端共享执行/查询，不能靠 GUI 禁用项
替代权限检查；会话/制造不随页面开关重置；地图不显示未感知敌人；真实通讯不能以静态图替代。
布局调整不应修改玩法或对话状态机。新增大功能同时更新架构、维护入口和使用指南。

## 暂停的原工作

原 [GPU 光照与天空升级](gpu-lighting.md) 后续性能与美术工作按反馈恢复；
本轮只处理新 UI 与通讯直接暴露的渲染问题。其余历史延期项不自动加入本轮。
