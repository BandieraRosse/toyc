# 角色形体与模块化装备精修现场

> 状态：历史记录；2026-10-03 用户授权的角色与装备升级
> 当前入口：[角色表现](../architecture/character-presentation.md)、[角色资产合同](../reference/character-assets.md)、[美术验收指南](../guides/character-art-acceptance.md)、[实验区合同](../reference/experiment-labs.md)

本轮针对 RF-C01 肩部、头脸和发型，Humanoid 身体细节，以及贴体装备缺少外轮廓的问题进行修订。
资产使用原创网格；人体结构和装备的一手参考见对应创作记录与[来源台账](../reference/asset-sources.md)。

## 实现

- Humanoid 身体生成拆到 `rf_humanoid_body.py`，增加肩颈坡面、衣领、眉眼鼻唇与耳廓、短发、
  手掌拇指、裤腿与靴子细节。鼻梁并入头壳，五材质顺序与职业调色保持；骨架、inverse bind 和
  CHR1 附件记录逐字节保持。新身体为 5,880 顶点、4,286 三角形、312,276 字节。
- 装备生成拆到 `rf_humanoid_equipment.py`，升级八套职业/敌方头、胸、背组件和已有侧挂件，
  新增独立护目镜与左右大腿外裤层。头盔外壳、导轨、耳罩、板甲包边、肩带、腰封、弹匣袋与
  背包压缩带具有真实体积。配方容量扩到八件，允许同一 socket 上保留多个独立附件。
  最终共 30 件公开装备，合计 24,856 三角形、1,586,272 字节；单件最多 2,082 三角形与八种材质。
- AI 动作区保留步枪手的三种动作，在原空台位加入其余职业和敌方套装，使用同一开关与时钟。
  `equipment-lab` 近景和 `gpu_equipment_review.ps1` 用正常原生 Scene 检查完整装配；不创建玩法 actor。
- RF-C01 沿私有锁定部件创建新候选，肩袖去除独立圆帽感，修订头型、脸部与主次发束。
  最终 V25d 安装为 `private-assets/models/rf_c01_v025d.rmesh`，目录与保真工具读取同一版本。
  旧源与各轮候选保留；私有源和运行资源不加入 Git。

## 验证口径

本机证据根为 `tmp/character-upgrade/`、`tmp/humanoid-refinement/`、`tmp/character-quality/equipment/`，
RF-C01 部件审阅与隔离记录在私有 `authoring/build/` 下。原生捕获记录实际 exe、地图与资产哈希；
过程图和修订前图不能替代最后版本的结果。

Windows native doctor、最终构建和聚合逻辑回归通过。Humanoid 的 RFCHAR 与双实例检查、固定多视图、
CPU 行走/射击/回避捕获通过；公开装备完成 rigid 导入。原生图检检出了镜框离脸过远、呼吸器过方和
鼻梁接缝，随后在源几何中修订并复验。

最终 executable SHA-256 为 `40AC6BAD2FDCA4B08B16DBFE61D098B064848D5093F20FA4200E40B1D04D5450`。
RF-C01 安装文件 SHA-256 为 `41B19F19B4DBD1AAA0668A3EA3E5D17C0934BB94ED16414B107E9656D407737E`；
源导出、本地 models、运行目录一致，31 件公开身体/装备资源也逐字节一致。

- `--gpu-scene-pose-test` 通过容量、重复 socket、腿部跟随、只读、CPU/Scene 一致及展示时钟回归，
  同时通过网络、感染体、武器、资源替换与世界切换的既有测试。
- `--squad-acceptance` 通过共享身体、30 件共享装备、八个独立实例及 palette 隔离。
- `--combat-character-capture` 完成 15 个近中远视图、八帧动作与 12 帧回避，验证资源只读及 CPU/Scene 回避一致。
- RF 模型区与重装侧面各运行 120 帧原生 Scene，来源独立，无 bridge/mixed；仅指定最后截图帧读回。
  RF-C01 最终正面/三分之四/侧面保真图、突击/重装/精英的九张装配图已检查。
- 文档检查与 `git diff --check` 通过；没有运行与本轮无关的编译器自举更新。

最终日志位于 `final-checks/`，三视图位于 `anime-native-final/`，装备图位于 `native-equipment-final/`，
连续重装检查位于 `equipment-motion-final/`。九张装备图生成于仅测试及 RF-C01 目录切换前的构建，
各自 manifest 保留真实哈希；最终构建另完成连续原生重装与完整 CPU 装配验证。

测试还暴露了 presentation game 指针独立后旧 fixture 未切换指针，以及实验区迁移后残留的绝对坐标
断言。修复测试上下文的保存/恢复，用源到冻结值的一致性检查替代旧布局值；不削弱姿态、几何与
资源只读断言。网络 fixture 也改为在真实 client 本地槽放置待排除对象，避免仅改 player ID 后把它映射成远端。
另修复展示步态从短周期 gameplay 时间累计造成的 CPU/Scene 相位差，保留真实角色时钟。

## 边界

本轮提升的是形体、分层与装配轮廓，仍使用既有不透明材质，没有增加角色贴图、布料模拟、
通用可换蒙皮服装或完整新动作。大腿外裤层截止膝上，跨膝表面仍由身体蒙皮拥有。
Humanoid 的版型不能自动套到 RF-C01；两类角色保留各自创作与资源边界。
原生固定视图和连续提交属于技术/图检证据，不等同于用户自由游玩的美术签收或稳定帧率承诺。
