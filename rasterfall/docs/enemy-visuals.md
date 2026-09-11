# Enemy Visual V2：第一阶段

> 文档更新：2026-09-11
> 源码核对基线：工作区（六份公开 infected RFM2；renderer-only family/recipe；Enemy Visual Family Mix V1；正式 spawn API 离屏验收；既有 Enemy Death Presentation）

## 状态与接入入口

`toy_game_enemy.type` 仍由玩法拥有。COMMON、FAST、HEAVY 对应现有
`TOY_GAME_ENEMY_PURSUIT_*`；AI、spawn、伤害、波次、snapshot 与地图均不增加字段或分支。
`rasterfall_enemy_visual.h` 定义本地展示选择 AUTO / LEGACY / BLOCK_INFECTED / HUMANOID_INFECTED。
默认 AUTO 由 renderer 按 enemy type 混合家族，进程启动参数只改变本机 renderer；联机双方可以选择不同外观。

AUTO 的比例为 COMMON 70/20/10、FAST 40/40/20、HEAVY 30/40/30，顺序均为
LEGACY / BLOCK_INFECTED / HUMANOID_INFECTED。slot 只作为 renderer 稳定选择的种子，避免帧间
闪烁；结果不写入 gameplay enemy 或 network snapshot。特感继续使用旧 renderer。

`rasterfall_render.c::render_enemies()` 继续拥有可见性、地面/空中锚点、受击位移和死亡表现编排。
它在旧身体分支前调用 `render/rasterfall_enemy_visual.inc::render_infected_enemy()`。
六项 `enemy_visual_recipes` 是 family + enemy type → 公开 model path 注册表；特感没有 recipe，
沿用旧 renderer。资源加载失败只报告一次并回退旧身体，不回写 gameplay。

每项缓存一份 immutable resource 和一份串行 scratch instance。每次提交先 reset pose，再根据
stable humanoid roles 写入基础感染站姿/步姿，finalize bones 后使用现有 gallery vertex cache、
CPU skinning、材质和 Lighting V1。scratch 不跨敌人保留动作；逐槽位的轻量 presentation cache
仅保留上次位置、步幅相位和短暂移动保持时间。真实位移驱动步幅，重复渲染不会加快动画；
短暂不更新位置的帧保留移动姿态，停止后恢复 idle。没有新 clip 格式、ragdoll 或动作状态机。

新 RMESH 的纯色三角形经过既有 `draw_world_triangle()`，沿用 Enemy Death Presentation 的
旋转、位移与 alpha；旧死亡样式保留纵向压缩。资产自身已拥有 Heavy 体型，不重复套用旧
Heavy 的 1350 缩放。受击 tint 仅在当前 submission 生效。模型与附件在资产层变形，renderer
只按 header 的 position_scale 换算到 512 RFU/m，不用显示偏移修补 bind。

感染家族普通敌人的远裁剪为 56000 RFU，以便观察真实 100m 投影；旧家族、特感与队友保留
原有裁剪。远距离不放大模型。现实地图墙体仍会遮挡敌人，此范围不是射程或索敌距离。

## 资产来源、设计与预算

`tools/blender/generate_rasterfall_infected.py` 是可复现源。它复用 V2 的 armature、geometry
helpers、skin export 和 skeleton patch；Humanoid 身体复用 V2 `create_body()` 并减少环面侧数，
Block 使用独立方块身体。无外部模型、图片或纹理；感染伤口、肋骨、残甲与生长物均为源几何，
材质为 GLB 内的纯色色块，导入后进入 RFM2 material table。

| 类型 | Block-derived Infected V1 | Humanoid-derived Infected V1 |
| --- | --- | --- |
| Common | 方块人、破损下颌、裸露小腿、单侧肩部感染、破损衣摆 | V2 解剖轮廓、衣物色块、单侧肋部伤口、面部感染 |
| Fast | 窄躯干、长臂、钩状手、背鳍、前倾追击姿态 | 瘦化 V2、长肢与暴露生长物、同一追击姿态 |
| Heavy | 宽厚躯干、残甲、巨大单侧前臂与背部突起 | 宽厚 V2、肩甲/胸甲/背甲/膝甲、非对称结构变异 |

生成与验证工具约束每个 Block 不超过 600 三角形、Humanoid 不超过 1800 三角形，材质不超过
9 个，无纹理采样。实际数量、文件字节数及门禁结果写入生成目录 `asset-report.json` 和日志，
不把阶段性测量值作为稳定文档状态。

六份 `tools/assets/manifests/enemies/rf_infected_<family>_<type>.asset.json` 沿用 character schema；
GLB 中间源在本地 `private-assets/source/enemies/`，公开交付物为
`rasterfall/assets/models/enemies/rf_infected_<family>_<type>.rmesh`。
全部保持 RFCHAR V1 的 21 stable roles、29 bones（含八附件）和 RFM2 v14；腿、手、头部、附件
同源导出。Fast/Heavy 的比例变形同时作用于源顶点、rest joints 与 socket anchors，保留 inverse bind
一致性。Block 的衣物腰部保留 spine/chest 双权重，四肢与感染附件大多使用单骨权重。

运行时不解析 manifest，不要求本机存在 Blender、GLB 或 private-assets。根 Makefile 的递归公开
资产扫描自动覆盖六份 RFM2；Windows package 递归复制公开 assets。新增的是 renderer include，
不是独立编译单元；Linux/self 的显式依赖和 Windows 的自动头依赖均覆盖该入口。

## 使用与验收

```sh
make app-rasterfall app-glb-inspect build/rfchar_runtime_test
build/rasterfall --enemy-visual-family block-infected
build/rasterfall --enemy-visual-family humanoid-infected
build/rasterfall --enemy-visual-family legacy
build/rasterfall --enemy-visual-capture tmp/enemy-visual-mix-v1/mixed
build/rasterfall --enemy-visual-family legacy --enemy-visual-capture tmp/enemy-visual-mix-v1/legacy

# 重新生成六个源模型、统一导入、验证及截图
python3 tools/enemy_visual_round.py --generate --capture --deterministic
# 已有公开 RFM2 时仅验证与截图，不要求 Blender
python3 tools/enemy_visual_round.py --capture --deterministic
build/rasterfall --logic-test
```

独立 capture 入口为 `--enemy-visual-capture <output-dir>`，默认捕获 AUTO mixed；也可与三个
family 参数组合进行强制家族验收。捕获标签会显示 `COMMON / BLOCK_INFECTED` 这类实际选择。
实现位于 `dev-tests/rasterfall_enemy_visual_capture.inc`：加载正式地图，用
`toy_game_spawn_horde_type()` 生成三种真实 enemy slot，调用 `toy_game_update_world()` 验证移动，
随后冻结 fixture 以便比较 bind/idle/move × front/side/three-quarter。
截图始终通过正式 `render_enemies()` 分支，逐帧比较完整 gameplay 结构的字节副本，并检查模型
submission 数、资源失败和 command overflow；缺少资产时验收失败，不允许回退图冒充新资产。

默认输出 `tmp/enemy-visual-v2/`：六份 `<family>-<type>.png`、`six-infected.png`，以及每家族的
world/death 组图、原始 BMP、asset report、contract/import/runtime 日志。`--deterministic` 重拍并
逐字节比较。已有源 GLB 时执行 manifest 和 GLB contract 门禁；只有公开 RFM2 时仍执行 runtime
隔离、蒙皮和骨架/附件门禁以及三角形/材质预算。

实景 10m、30m 使用正式地图南侧空地与 900 RFU 眼高；100m 因真实地图外围墙遮挡，单独输出
`distance-100m.bmp`，只保留天空/地面并明确标记 UNOBSTRUCTED DISTANCE。模型、相机焦距和
分辨率不随距离缩放。100m 的类型可读性应以原尺寸投影审阅，不能把放大截图当作 FPS 实际视图。
死亡组图观察既有 ballistic/fade 与 legacy collapse，不新增死亡动画。
