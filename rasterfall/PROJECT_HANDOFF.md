# Rasterfall 暂停开发与恢复说明

> 最后更新：2026-09-03
> 依据提交：`2698c813cbd04ed2c197dc84101ae02f0b9b02a4`（增加 Rasterfall 代码导航文档）

本文是 Rasterfall 暂停开发时留下的现场记录。它不代表产品路线图，也不承诺继续维护；
目的是让未来的自己或接手者不必重新猜测当前边界、资源来源、转换流程和动画问题。
面向用户的构建方法仍以仓库根目录 `README.md` 和本目录 `README.md` 为准，模型与动画
内部边界以 `ANIMATION_ARCHITECTURE.md` 为准。

## 当前结论

Rasterfall 已经是一个可构建、可运行的 freestanding 软件光栅化游戏实验，同时包含地图、
战斗、AI、音频、网络、静态与骨骼模型、VMD/glTF 动画实验和开发者标定工具。暂停的主要
原因不是某一个已知崩溃，而是角色表现层已经超过个人项目适合继续迭代的复杂度：模型格式、
骨架语义、重定向、IK、动画混合、持枪约束、动作资产和逐帧视觉验证相互影响，自动逻辑测试
只能覆盖其中一部分。

恢复开发时不要先扩展玩法或再加角色。先建立快速、可重复的视觉回归基线，否则每次动画
调整仍会退化为长时间实机试错。

## 已知稳定边界

- Linux 默认构建为 `make app-rasterfall`，产物是 `build/rasterfall`。
- `build/rasterfall --logic-test` 是当前最便宜的整体逻辑检查，但通过不等于动画视觉正确。
- gameplay 使用整数 RFU，`512 RFU = 1 m`。PMX、GLB、VMD 的资产单位只允许在表现层边界
  换算，不能进入碰撞、AI 或网络规则。
- RFM2 是运行时模型格式；PMX 和多数 GLB 原件不是运行时依赖。
- gameplay actor 的动作语义由 `toy_game_animation_id` 表达。VMD、glTF 和程序化动作只是
  表现来源，不能反向改变规则状态。
- Pose Editor 的编辑、显示和导出路径可用。它解决的是一套确定角色/武器的静态标定，
  不等于已经解决 locomotion、开火、受击等动画叠加后的最终持枪表现。

## Eula + AK 持枪姿态快照

当前确认保留的姿态在 `src/rasterfall_calibration.c` 的 Eula/AK profile 中；编辑器导出文件
为 `tmp/eula_ak.rfpose`。`tmp/` 会被清理且不进版本控制，所以源码 profile 才是长期记录。

```text
weapon_scale 500
weapon_offset -2 -4 -256
weapon_rotation 117 -45 45
grip -18 -8 -65
foregrip 5 1 48
muzzle -5 28 225
body upper_body -5 0 0
body right_arm -64 3 55
body right_elbow 90 26 -92
body left_arm 21 66 2
body left_elbow 62 51 -18
left_ik 1
```

这套数据以 Rifle Frame（优先 `上半身2`，不存在时使用 `上半身`）为局部参考。枪械网格、
grip/foregrip/muzzle 和左手 IK 目标必须使用同一个 frame。右臂应由上述 body pose 决定，
不要再用一个 presentation-side 右臂 IK 覆盖它；那会让已经标定的右臂旋转和求解器互相争夺
最终姿态。左手 IK 只负责把手腕对准 foregrip。

若以后重新标定：在游戏控制台执行 `pose`，完成后导出，人工比较导出文件，再把确认值写回
profile。不要让运行时自动加载 `tmp/`，也不要在不保存旧快照的情况下连续修改参考系和参数。

## 为什么动画表现仍然困难

目前同时存在以下层次：

1. VMD 或 glTF clip 的局部骨骼旋转与 root translation。
2. PMX rest pose、不同角色的骨骼命名、轴向和父链差异。
3. PMX IK 与 grant/inherit 求值。
4. locomotion 与 rifle stance 的组合、上半身清理/锁定以及 fire/hit overlay。
5. 武器 Rifle Frame、左右手约束和角色朝向/显示比例。
6. 最终 CPU skinning、材质和软件光栅化输出。

其中任一层单独“数学正确”都不能证明最终动作自然。尤其要注意：

- 清空上半身会同时清掉 locomotion 的躯干摆动；不清空又会使静态持枪姿态被 walk clip 污染。
- 固定上半身父链可以稳定瞄准，但容易产生腿在移动、骨盆和躯干完全脱节的表现。
- 左手 IK 目标空间必须和武器空间一致；右手若也交给 IK，会覆盖 Pose Editor 的右臂参数。
- VMD 的 MMD 骨架语义与任意 PMX 模型并非天然兼容。名称映射成功不代表 rest basis、腿 IK、
  grant 和关节极限一致。
- 逻辑测试可验证数据流和不变量，却很难判断肩膀扭曲、手腕翻转、脚滑、循环接缝或动作节奏。

如果恢复工作，建议先输出固定相机、固定时间点的离屏 BMP 和像素/关键骨骼清单，为 idle、
walk 四分之一周期、fire 峰值各留一组 golden；然后才修改组合器。不要继续在完整游戏循环里
靠肉眼寻找每一次回归。

## 外部资源清单与权利状态

下面只记录当前工作区能够核实的名称。`private-assets/` 和 `.claude/` 都是本地资源区域，
不应因为 Windows 打包或 embedded 构建方便就默认公开分发。

### 角色模型

| 运行时名称 | 当前可核实的原件名称 | 转换产物 | 备注与权利状态 |
|---|---|---|---|
| Eula / 优菈 | 原始 PMX 当前不在工作区，准确文件名和随附许可无法核实 | `private-assets/models/eula.rmesh` 与 `eula.textures/` | 角色来自《原神》；在找回原始压缩包和许可前，不应对外分发转换产物 |
| ST AR-15 | `.claude/AR15/GirlsFrontline AsteriaDefault.pmx` | `st_ar15.rmesh`、`st_ar15_lod1.rmesh`、`st_ar15.textures/` | 随附 Readme 写明模型由 Sunborn Network Technology 创建、DesmondChan 绑定/修整，禁止二次配布、商业使用和拆取部件，仅限 MMD 使用 |
| G11 | `.claude/G11/GirlsFrontline MishtyDefault.pmx` | `g11.rmesh`、`g11_lod1.rmesh`、`g11.textures/` | 同系列本地测试模型；当前目录未见独立 Readme，应按最严格的同包限制处理 |
| Vector | `.claude/Vector/GirlsFrontline VectorDefault.pmx` | `vector.rmesh`、`vector_lod1.rmesh`、`vector.textures/` | 同上，不应公开再分发 |
| UMP45 | `.claude/UMP45/GirlsFrontline LevaDefault.pmx` | `ump45.rmesh`、`ump45_lod1.rmesh`、`ump45.textures/` | 同上，不应公开再分发 |
| Quaternius UAL1 Standard | `private-assets/models/UAL1_Standard.glb` | 运行时直接读取 GLB，没有转换成 Eula 的 RFM2 | 用于原骨架 Idle/Walk/Jog 对照；当前工作区没有找到原资源包许可文件，重新发布前必须回到 Quaternius 原下载页核对具体包名和许可 |

角色显示高度是项目校准值，不应描述为版权方官方数据：Eula 为 1736 mm；ST AR-15、G11、
Vector、UMP45 分别为 1652、1429、1474、1516 mm。来源等级和参考链接见 `README.md`。

### 动画

| 运行时文件 | 原件/作者信息 | 本项目处理 | 权利状态 |
|---|---|---|---|
| `private-assets/animations/walk04_loop5.vmd` | `.claude/walk/walk04.vmd`；随附说明署名 `mototo=toto@note0928`，日期 2020-01-04；说明称动作按つみだんご的“静谧的哈桑”描摹，并给出 NicoNico `sm30734215` | 为便于循环实验，把 walk04 拼接为五次循环版本；运行时仍按 VMD 读取 | Readme 允许修改；二次分发与商业利用要求通过 Twitter 联系。未取得许可前不要公开分发派生的 loop5 文件 |
| `private-assets/animations/曼珠沙華.vmd` | `.claude/曼珠沙華.vmd`；当前没有随附说明或可核实作者信息 | 原样复制为私有 VMD 运行时样本 | 来源与许可未知，不应公开分发 |
| `UAL1_Standard.glb` 内 Idle/Walk/Jog | Quaternius UAL1 Standard GLB | 直接读取原始 local TRS，作为 glTF 动画对照 | 需重新核实原资源包许可 |

`.claude/walk/first01.vmd`、`walk04.vmd`、`end02.vmd` 是原始步行动作片段；loop5 只用中间
walk 段生成，不包含自然起步和停步逻辑。

### 本地样本指纹

以下 SHA-256 用于以后确认找回的是同一份输入或运行时产物，不表示许可或所有权：

```text
412bd037301ebe18709eeac9cd3ec3d7f2ec4121cc83edd331843075b08b6a92  GirlsFrontline AsteriaDefault.pmx
4cc23c77ded8ee9b60fba1e4281fb766e8f1e341c05db866f34e3f9a0541eed4  GirlsFrontline MishtyDefault.pmx
4f5d642f97fc5a9d83c80ce50e73aa69a1cb3b4156d32ef97e74cbc93824cfdc  GirlsFrontline VectorDefault.pmx
abe3eaaaa9d042640f08e4f53f7d55a05d1e3c377798854b1f4c3ffee81a099f  GirlsFrontline LevaDefault.pmx
971f244764afadff0658846870cc0e085671fd76b783eae3fd25ce559274da16  walk04.vmd
79b0f33f3a6d36d72313545c267c7c4120e20a157d50c6e9f72cf60c5cd8c97a  walk04_loop5.vmd
ad879b45a7ebd9f19cf68b5afde5062e133e9ff613876c562f901aa7aea41f18  曼珠沙華.vmd
69591853d817488edaa8fd9bf8fc1d821eaeaf789f8627b3cd23b41c4ed67997  UAL1_Standard.glb
09fa038621aca7599669f8527d292c4197fc831398f20d77a7ce2553c34b5ab8  eula.rmesh
a0121befa37ac27588ef8089828d5d0d587b9058ed2d4dbf78672787bf04440f  st_ar15.rmesh
658223f5daf67a1423bbbb4366af7cd45f024fbf10715db934359840bdc3e828  g11.rmesh
ee7321e137cde8267faaf1d36f704f36d51446e76c1cbb7afdeb51697eee7b9c  vector.rmesh
7982d311c4356539f176378285d0a613bbaab238100bf17a3f22f69beb4fd6f6  ump45.rmesh
```

### 武器、弹药箱和道具模型

版本控制中的 `assets/models/*.rmesh` 是运行时转换产物。现有 README 记载其原件曾位于
`.claude/glb/`，但该目录当前已不存在，因此只能可靠记录产物名，不能从当前工作区恢复作者、
资源包名称或许可：

```text
ar_ak47, ar_ammo, ar_aug, assault_rifle_ammo_box,
axe, bomb, molotov,
pg_desert_eagle, pg_glock1, pg_glock2, pistol_ammo,
revolver, revolver_snub_nose,
rf_AWP, rf_hunting,
sg_double_barrel, sg_pump_action, shotgun_ammo,
smg_ammo_box, smg_bizon, smg_mac10, smg_mp5, smg_p90, smg_skorpion,
sniper_ammo_box
```

这些文件在对外发布前也必须重新建立逐项来源和许可台账。文件名不是许可证明。音效 `.tsnd`、
`model_diffuse.ttex` 和 `wall.ttex` 的源文件/生成方式也没有在当前工作区形成完整权利记录；若非
确认由项目自行生成，同样不要假定可再分发。

## 已使用的转换流程

### PMX 角色到 RFM2/TTEX

当前自动入口：

```sh
tools/import-pmx-model.sh path/to/character-folder output_name
# 覆盖已有本地产物时：
tools/import-pmx-model.sh --force path/to/character-folder output_name
```

流程如下：

1. 要求输入目录递归范围内恰好一个 `.pmx`。
2. 构建 `build/pmx2rmesh` 和 `build/toyasset`。
3. `pmx2rmesh` 读取 PMX 网格、材质、纹理引用、骨骼、蒙皮和已支持的 IK/grant metadata，
   写出 RFM2，并把 PMX 引用纹理复制到临时纹理目录。
4. `toyasset convert` 把 PNG/BMP/SPA/SPH/JPG 转为 TTEX，再执行 `toyasset validate`。
5. 所有步骤成功后才把临时结果移动到 `private-assets/models/`。

四个 GFL 模型曾等价地使用以下输入和输出名称：

```sh
tools/import-pmx-model.sh .claude/AR15 st_ar15
tools/import-pmx-model.sh .claude/G11 g11
tools/import-pmx-model.sh .claude/Vector vector
tools/import-pmx-model.sh .claude/UMP45 ump45
make lod-characters
```

`make lod-characters` 调用 `tools/rmesh_lod.py --ratio 0.4`。LOD 只简化索引，保留顶点、骨骼、
蒙皮和材质布局，并与 full mesh 共用纹理目录。

### 静态 GLB 到 RFM2

```sh
make app-glb2rmesh
build/glb2rmesh input.glb rasterfall/assets/models/output.rmesh
```

该路径面向静态 mesh，按 232 倍换算坐标，保留已支持的 primitive、法线、UV 和基础 PBR
因子，但不把 GLB 骨骼或动画转换进 RFM2。`UAL1_Standard.glb` 走的是另一条直接 glTF
骨架/动画预览路径，不应使用静态转换器替代。

### VMD

VMD 没有转换为另一种磁盘格式。`rasterfall_vmd.c` 在运行时读取 VMD，把骨骼旋转映射到
格式无关 `rasterfall_animation_clip`；Center/Groove 平移按 232 缩放为 presentation-only
offset。检查入口：

```sh
make app-vmd-inspect
build/vmd_inspect rasterfall/private-assets/animations/walk04_loop5.vmd \
    rasterfall/private-assets/models/eula.rmesh
```

`walk04_loop5.vmd` 的实际拼接工具/命令未保存在仓库中。未来若需要重建，先补一个确定性脚本，
并记录输入文件哈希、帧区间和输出哈希，不要再次手工生成后只保留结果。

## 暂停前的最低验证方式

```sh
make app-rasterfall
build/rasterfall --logic-test
```

涉及模型或动画时，再按修改范围选择：

```sh
make app-vmd-inspect app-glb-inspect
build/rasterfall --model-humanoid rasterfall/private-assets/models/eula.rmesh
build/rasterfall --model-humanoid-basis rasterfall/private-assets/models/eula.rmesh
build/rasterfall --model-material-regression \
    rasterfall/private-assets/models/eula.rmesh tmp/eula-material-regression
build/rasterfall --actor-performance 30 5 8
```

这些检查仍不能代替视觉验收。恢复开发前应把固定帧截图、骨骼变换摘要和性能阈值组成一个
短路径回归目标，目标运行时间应以秒计，而不是每次启动完整游戏人工巡查。

## 建议的恢复顺序

1. 先确认外部资源是否仍允许本地使用，并补齐来源、许可文本和原件哈希；不先做发布。
2. 从干净工作树构建并运行 logic test，记录编译器、平台和资源是否齐全。
3. 只选择 Eula + AK + 一个 walk clip，冻结其他角色、网络和新玩法范围。
4. 建立 idle/walk/fire 固定帧 visual golden 和骨骼摘要。
5. 把模型不可变资源与每 actor 的可变 pose instance 分离，避免多个角色共享并修改同一模型
   状态；这是继续扩展骨骼角色前最重要的结构债。
6. 明确定义 animation composition 的输入 pose、channel mask 和应用顺序，再处理上下半身混合。
7. 只有上述基线稳定后，才考虑 reload、hit、death 等动作和第二角色。

若只是想保留项目成果，不必执行这份恢复计划。当前代码和文档本身已经可以作为完整的个人
工具链、freestanding 运行时与软件渲染实验记录。
