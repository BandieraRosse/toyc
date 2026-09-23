# RF Humanoid 美术验收与职业外观

> 状态：当前操作指南；各节的阶段观察只描述对应验收现场
> 资产合同：[Character Asset Contract V1](../reference/character-assets.md)

## RF Humanoid Art Acceptance Character V1.1

第一份非 fixture 的正式候选由 `tools/blender/generate_rasterfall_humanoid.py` 参数化生成，
manifest 为 `tools/assets/manifests/characters/rf_humanoid_acceptance.asset.json`。它沿用[角色资产合同](../reference/character-assets.md)
冻结的 21-role skeleton 与 8 stable attachments，不修改 RFCHAR contract；几何按 pelvis/waist/chest、
独立 neck/skull/hair shell、tapered upper/lower limbs、放大的 hands/feet 拆分，使用 5 个大色块
材质。源资产与导入产物在 `rasterfall/private-assets/`（可选本地资产）中生成，不由 runtime 直接读取 GLB。

可重复生成和验收：

```sh
mkdir -p rasterfall/private-assets/source/characters
blender --background --factory-startup --python tools/blender/generate_rasterfall_humanoid.py -- \
  --output rasterfall/private-assets/source/characters/rf_humanoid_acceptance.glb
build/glb-inspect rasterfall/private-assets/source/characters/rf_humanoid_acceptance.glb contract
tools/assets/import_asset.py --force tools/assets/manifests/characters/rf_humanoid_acceptance.asset.json
build/rfchar_runtime_test rasterfall/private-assets/models/rf_humanoid_acceptance.rmesh
build/rasterfall --model-pose-views rasterfall/private-assets/models/rf_humanoid_acceptance.rmesh tmp/rf-humanoid-acceptance/bind bind
build/rasterfall --model-pose-views rasterfall/private-assets/models/rf_humanoid_acceptance.rmesh tmp/rf-humanoid-acceptance/posed rfchar-test
```

当前 acceptance 基线为 1,619 vertices / 1,011 triangles / 18 mesh nodes / 5 materials；
导入后为 29 bones（21 humanoid + 8 attachments），其中 1,499 个顶点为 BDEF1、120 个为
BDEF2。`--character-acceptance` 使用稳定 CHEST/WEAPON_R/WEAPON_L/FOREGRIP 接入现有 AK，
输出 bind、rifle-idle、rifle-aim 的 front/side/back/three-quarter，并输出 near/mid/far 的
固定 front A/B sheet。它是离屏验收入口，不是新的 runtime character path：

```sh
build/rasterfall --character-acceptance rasterfall/private-assets/models/rf_humanoid_acceptance.rmesh tmp/rf-humanoid-v11
```

步枪动作通过稳定 humanoid role composition 旋转肩、大臂、前臂和双手，再由现有 rifle hand
solver 求解握持；capture 根据姿态 bounds 使用稳定 margin framing。当前已验证 rifle motion、
RFM2 v14 runtime load 与 CPU skinning，仍需在真实 gameplay 镜头中继续观察远距离附件空间。

## RF Humanoid Art Acceptance Character V2

V2 是利用同一 RFCHAR V1 contract 对 V1.1 原型做的大尺度 body rebuild。V1.1 的 GLB/RMESH
继续保留作并排对照；V2 不新增骨骼角色抽象，也不改变 `RFCHAR`、RFM2 v14、stable role 或
attachment ID。新的源生成器为 `tools/blender/generate_rasterfall_humanoid_v2.py`，manifest
为 `tools/assets/manifests/characters/rf_humanoid_v2.asset.json`。

造型策略是长腿、较短的视觉躯干、明确的 pelvis second volume、连续 ribcage → waist → pelvis
收缩、独立 deltoid/elbow/knee/calf 轮廓，以及 jaw/cheek/temple/crown 分层的头部和独立 hair
silhouette mass。基础外观只使用 skin、hair、shirt、pants、boots 五个大色块；没有用战术附件
掩盖人体比例。所有 deform 顶点保持最多两项影响，按现有 BDEF1/BDEF2 输入门导出。

可重复生成、导入和验收：

```sh
mkdir -p rasterfall/private-assets/source/characters
blender --background --factory-startup --python tools/blender/generate_rasterfall_humanoid_v2.py -- \
  --output rasterfall/private-assets/source/characters/rf_humanoid_v2.glb
build/glb-inspect rasterfall/private-assets/source/characters/rf_humanoid_v2.glb contract
python3 tools/assets/import_asset.py --force tools/assets/manifests/characters/rf_humanoid_v2.asset.json
build/rfchar_runtime_test rasterfall/private-assets/models/rf_humanoid_v2.rmesh
build/rasterfall --character-acceptance \
  rasterfall/private-assets/models/rf_humanoid_v2.rmesh tmp/rf-humanoid-v2/acceptance
build/rasterfall --character-world-capture tmp/rf-world-v2 \
  --character-world-model rasterfall/private-assets/models/rf_humanoid_v2.rmesh
```

Final convergence 保留参数化生成源和五大材质色块。骨盆最大半宽 0.275m、腰半宽
0.235m；肩峰采用更小且偏向上臂权重的渐缩体，收窄胸侧以留出腋下空间。大腿根收窄，
左右 knee 网格位于各自的 X=±0.15m，calf 峰值上移、脚踝收窄、鞋底共面于地面；hair crown
闭合并与前缘相接，避免原先开口露出头皮。手部 socket 位于实际掌部，保持现有 stable IDs。
源空间从鞋底到 crown 约 2.080m；bind pose 中
hip 约在总高 42%、shoulder 约在 72%，视觉上把更多高度交给腿和小腿，而不是 V1.1 的短直筒腿。

`--character-acceptance` 继续输出 bind、rifle-idle、rifle-aim 的 front/side/back/three-quarter
及 near/mid/far A/B；`--character-world-model` 只替换开发者区 Character Test Strip 的 body，
仍使用正式地图、真实 world renderer、标准 AK 和深度路径。开发者区默认加载 V2；相机从展示带
斜正面观察，避开工业 prop 与边界墙对中远景的遮挡，并补充每个角色独立的三档距离截图。

观察模型时应优先采用组图方式，减少逐张打开和切换截图的开销。使用
`tools/character_lab_sheet.py` 将当前 Humanoid V2 的三种姿态和四个角度拼成一张 lab sheet；
需要检查真实地图中的距离、场景遮挡或角色姿态时，使用 `tools/character_world_sheet.py`
生成类似 `real.png` 的 near/mid/far × old/idle/aim/motion 实景合集。两个脚本都会先调用当前
离屏 CLI，再拼接正常光照结果；单张 BMP 仅作为原始证据保留，不作为主要观察交付物。

附件 importer 必须将 GLB 的 parent-local TRS 烘焙到 SKN1 的 identity-rest 基底：position 为
socket 与 parent 的全局 bind 位置差，rotation 为 socket 的全局 bind rotation。不能直接复制
GLB local TRS；`test_rfchar_pipeline.py` 对导入附件与 GLB bind 变换做交叉检查。

RFCHAR 的正式 modular 持枪使用 finalized `WEAPON_R` 对齐武器 `PRIMARY_GRIP`，并从同一
pose 派生 `FOREGRIP`；`rifle_idle`、`rifle_aim`、`rifle_fire` 都必须同时写入左右手和
双臂轨道，随后由 modular runtime 的左臂 attachment IK 让左手随前握点保持一致。开发者区 Character Test Strip 通过
`render_modular_preview_frame()` 复用战斗区的 RFANIM 组合与 active weapon presentation。
`rifle_solve_hands()`、CHEST 枪架和 `visual_rf_calibration()` 只保留给 legacy carrier/旧 PMX
验收诊断；`visual_rf_check_grips()` 超过 4 RFU 误差即返回失败。
Character Lab 的 front/three-quarter 对应 canonical +Z 正面，所有方向共享相同姿态与 framing。

这套 V2 是后续 AI/NPC 身体与 headgear/职业附件扩展的基线候选。完整运行时资产仍为本地可选
资源，生成器与 manifest 才是可复现源；本轮不自动替换全部 gameplay actor。最终冻结判断和
具体截图观察记录见 [final convergence record](../archive/rf-humanoid-v2-final-convergence.md)，导航不记录阶段测试数量。

## RF Humanoid Headgear / Face Coverage V1

Headgear V1 先验证“clean simplified face + HEAD-mounted coverage”这条身份路线，不新增
humanoid bone、face bone、网络字段或 gameplay identity。裸脸清理将 HairCap 的下缘抬到稳定
hairline，移除横跨前额的独立 HairFringe，只保留 crown 与两侧 HairLock；因此 face 仍只依赖
skull / jaw / cheek / chin 的大形，不加入眼鼻口、T 字、十字或符号化脸标。

生成器仍是 `tools/blender/generate_rasterfall_humanoid_v2.py`。`--headgear` 的几何全部使用
单骨骼 `RF_HEAD` 权重，并沿用已有 `HEAD` attachment；V1 为便于现有 RFCHAR importer 和
Character Acceptance 验收，每个样本暂时输出为“同一 base body + 一个模块”的完整 RFCHAR
变体。它是稳定的验证载体，不是最终要求 runtime 复制整个人体；后续拆成独立 head-mounted
assembly 时不需要改变骨架或 attachment contract。

覆盖率样本如下：

| 变体 | 覆盖率 | 识别语言 |
| --- | --- | --- |
| `bare` | 低 | clean face + hair mass 基线 |
| `headset` | 低 | 侧向耳罩、头顶弧和 mic boom |
| `patrol-cap` | 低 | crown volume + forward brim |
| `goggles` | 中 | 双镜片与 strap，保留 jaw/cheek |
| `respirator` | 中 | 下半脸 shell、双 filter、侧带 |
| `tactical-helmet` | 高 | dome、brow、ear rail、visor |
| `engineering-helmet` | 高 | 宽耳罩、前檐与顶部 lamp |

六个模块和基线的源资产/manifest 分别为：
`rf_humanoid_v2_headset`、`rf_humanoid_v2_patrol_cap`、`rf_humanoid_v2_goggles`、
`rf_humanoid_v2_respirator`、`rf_humanoid_v2_tactical_helmet`、
`rf_humanoid_v2_engineering_helmet`，以及原有 `rf_humanoid_v2`。可重复生成单个变体：

```sh
blender --background --factory-startup --python tools/blender/generate_rasterfall_humanoid_v2.py -- \
  --output rasterfall/private-assets/source/characters/rf_humanoid_v2_tactical-helmet.glb \
  --headgear tactical-helmet
python3 tools/assets/import_asset.py --no-build --force \
  tools/assets/manifests/characters/rf_humanoid_v2_tactical_helmet.asset.json
build/rfchar_runtime_test rasterfall/private-assets/models/rf_humanoid_v2_tactical_helmet.rmesh
```

统一生成 Character Lab 对比和代表性 world strip：

```sh
python3 tools/rf_humanoid_headgear_sheet.py \
  --output tmp/rf-headgear-v1/headgear-lab.png --world
```

该脚本复用 `--character-acceptance` 的 bind / rifle-idle / rifle-aim、front / side / back /
three-quarter 和 `--character-world-capture` 的 near / mid / far；主要交付图是
`headgear-lab.png` 与 `headgear-lab/headgear-world-idle.png`，原始 BMP 留在同目录 captures
下。当前观察结论是：clean face 已足够稳定；低覆盖模块主要在侧面和三分之四角度提供职业
提示，中覆盖在近中景最有效，高覆盖的 tactical / engineering helmet 在中远景仍保持强轮廓。
因此 RF Humanoid 头部扩展 V1 建议正式沿用“clean face + modular headgear”主线，五官系统暂不
作为下一阶段前置条件。

## RF Humanoid V2.1 Final Body / Profession Visual System V1

当前 `rf_humanoid_v2` 资产 ID 保持不变，生成内容为 V2.1 Final Body。局部收敛只调整
胸背深度、肩峰到上臂的前后过渡、骨盆后侧与大腿根深度；骨盆最大半宽仍为 0.275m，
头顶仍为 2.080m，canonical skeleton、八个 attachments 和五色块基础身体保持冻结。
patrol cap 的下部壳体略扩，以包住原有 hair crown；脸与头发没有重新设计。

六职业通过同一 `create_body()`、现有 Headgear 和少量中大型装备构成；`--profession`
拥有 palette 与 headgear 组合，不能再指定额外 `--headgear`。资产仍为完整 RFCHAR
carrier，装备采用单骨骼权重，HEAD 属于 RF_HEAD，CHEST/BACK 属于 RF_CHEST，
侧面 HIP 装备属于对应上腿；Breacher 中央护腹属于 RF_HIPS。attachment bone 不参与蒙皮。
导出前合并 mesh，按材质生成 primitive，以遵守现有 RFM2 32 primitive 上限。

| 职业 | 大色块与主要识别体积 |
| --- | --- |
| Rifleman | 橄榄绿、战术头盔、标准胸挂和短背包，中等负载 |
| Breacher | 蓝灰深色、头盔与呼吸器、厚胸甲/护领/护腹、贴身背板 |
| Recon | 浅卡其绿、帽檐与 headset、轻胸挂和窄高小背包 |
| Medic | 青灰衣裤、浅色医疗胸背装备、橙色大面板、goggles 与 respirator |
| Engineer | 赭黄、工程头盔、非对称工具背箱与左髋工具箱 |
| Heavy | 棕色、最宽弹药背架、厚胸甲与双髋弹药箱 |

源资产为 `rf_profession_<name>.glb`，manifest 为
`tools/assets/manifests/characters/rf_profession_<name>.asset.json`，运行时产物在
`rasterfall/private-assets/models/`。六职业名称不复用旧四人 Hurd gameplay profession，
也不进入 actor、网络或碰撞数据。没有新增 rigid attachment runtime 或模型实例架构。

```sh
make rasterfall app-glb-inspect build/rfchar_runtime_test
python3 tools/rf_profession_round.py --generate --capture --world --deterministic
```

该命令重建基础身体、六个 headgear 样本和六职业；每个生成物经过 contract、统一 importer
与 runtime 检查。Character Lab 覆盖 bind/idle/aim 四方向；world 覆盖 body 和六职业的
near/mid/far × old/idle/aim/motion。lineup 在同一深度缓冲绘制六人，无职业标签；front 与
three-quarter 各三档距离，加 side mid。重复 capture 逐字节核对七张 BMP，并记录 SHA-256。
生成资产、日志、组图和原始 BMP 均为本地产物，不提交。

正式 Standard Response / Assault roster 复用这里冻结的同一 body、六职业 gear resource 和
attachment contract；roster 的 character identity 与 ordered squad content 位于
`include/rasterfall_roster.h` / `src/rasterfall_roster.c`，不新增资产载体，也不把资源字段复制到 actor。
