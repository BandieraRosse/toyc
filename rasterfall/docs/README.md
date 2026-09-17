# Rasterfall 代码导航

> 文档更新：2026-09-17
> 源码核对基线：GPU-0 ～ GPU-8A 已完成；RenderFrame V1 已建立单调层 cursor，normal frame 按 sky/world/transparent/effects/viewmodel/overlay 契约提交；Core 在 viewmodel barrier 统一决定 GPU 提交或整帧 CPU replay。GPU-8B2 审计已区分每层 command、direct pixels 和 fallback reason，纯 Raster V1 effects command 可随 retained stream 消费；billboard、普通 particle 与屏幕线 ray 已迁入携带投影逆深度的 opaque raster commands，`effects_direct_pixels=0` 门禁已建立；transparent 与 viewmodel 仍待迁移。GPU-8B1、GPU-9A 仍待 Windows normal-frame 冻结。
> 当前 GPU 优先级：在 Windows 实机验收 sky 的 CPU/GPU 路径切换一致性，并继续迁移 transparent、effects、viewmodel；`--frame-audit` 已同时写入 `rasterfall.log`。详见 [GPU 与 Windows Native Platform 阶段计划](../../docs/Rasterfall%20GPU%20%E4%B8%8E%20Windows%20Native%20Platform%20%E6%80%BB%E4%BD%93%E8%AE%A1%E5%88%92.md)。
> 源码核对基线补充：Eula 正常 world/展示在 near/mid 使用 Gameplay Hybrid `eula_lod3.rmesh`，仅 FAR（4096 RFU 起）使用 compact LOD2；Hybrid 缺失时回退原模型。
> 源码核对基线补充：Eula Animation Acceptance V1 提供 legacy VMD carrier 的固定离屏姿态组图；统一 `--character-performance[-suite]` 保留 model/actor/world benchmark 的原职责并提供单角色、固定资产族及常见实例组合的同口径统计。
> 源码核对基线补充：Anime Gameplay Hybrid LOD V1 的 Eula pilot 使用离线 region descriptor、按骨长定义的关节邻域与 BDEF pair/weight 分区生成普通 compact RFM2；runtime、renderer 与 skinning 不增加角色特判。
> 源码核对基线补充：True Vertex-Reduced Character LOD 在离线索引简化后 compact 实际引用 vertex 与对应 SKN1 BDEF；不改变 skeleton、CHR1 attachment、动画或 renderer skinning 算法。
> 源码核对基线补充：2026-09-15 工作区；V2 Planar Raster Optimization 为 V2 无纹理细分平面增加专用 solid + interpolated vertex light + fog + depth command/worker 路径；`--render-performance` 保留 `generic-planar` A/B，输出 framebuffer/depth 差异、专用/回退 command 及路径耗时。
> 源码核对基线补充：Maid 正常 world/展示仍优先 LOD2；Eula 已改为 near/mid Hybrid、FAR compact LOD2。Campaign Maid 四人内容武器为 AK。
> 源码核对基线补充：Windows 启动地图加载的容量型 Map IR 改为临时堆分配，成功与失败均释放；不依赖扩大线程栈，详见 map-format.md 的 Runtime Bridge。
> 源码核对基线补充：Static World Lighting V2 — FROZEN；最终64×48世界尺度、参数、consumer、验收与平台覆盖边界见 [Phase D](static-world-lighting-phase-d.md)。
> 源码核对基线补充：Static World Lighting V2 Phase C3：V2 为唯一正常 runtime world-light source；V1 独立 diagnostic owner、显式 fixed override 与统一 scene factor，见 [Phase C3](static-world-lighting-phase-c3.md)。
> 源码核对基线补充：Static World Lighting V2 Phase C2：正常 actor/enemy root 单点采样，世界武器继承 owner，本地第三人称/viewmodel 共用 sample，保留 form/material policy；固定诊断例外见 [Phase C2](static-world-lighting-phase-c2.md)。
> 源码核对基线补充：Static World Lighting V2 Phase C1：正常 map static RMESH 在 `render_static_props()` 按实例世界原点采样一次 V2，通过已有 scene override 与原 form lighting 组合；诊断/gallery 不变。见 [Phase C1](static-world-lighting-phase-c1.md)。
> 源码核对基线补充：Static World Lighting V2 Phase B：64×48 ground-following field，Runtime Map 三维 AABB 太阳遮挡、独立弱 contact 与 Q8 bilinear；主地面/建筑平面/边界墙消费 V2，角色与 static RMESH 保留原策略。见 static-world-lighting-phase-b.md。
> 源码核对基线补充：Static World Lighting V2 Phase A：独立 world-light owner 与显式 position sample；V1 32×24 bake/nearest-cell 和 renderer bypass 保持原行为。
> 源码核对基线补充：Surface 以 `attr.collision_id` 绑定 Runtime collision 稳定 ID；加载检查引用与唯一绑定，Gameplay Projection 按 ID 合并几何，不再使用 surface legacy_index。
> 源码核对基线补充：Campaign Continuous Wall / Floor 与 Component Collision：`boundary_wall` 为长度参数化 RFU 墙体；`attr.collision=component|boundary|none` 在 Runtime Map 展开独立碰撞，保留 object owner ID；布局导出调用 C inspector 获取实际碰撞。
> 源码核对基线补充：Campaign `env_arch_*` 接入西侧维修巷、东侧开放设施与南侧动力区；V1 object.y → toy_map_prop.y → static prop 的地面相对高度，既有 collision/surface/region 不变。
> 源码核对基线补充：Return-to-WHU compatibility/readability 入口：`world attr.identity` → session world ID → `rasterfall_world_uses_authored_ground()`；WHU floor paint 复用单平面分区，出生方向由 Runtime region sy/cy 投影。四个眼高视角用 `--map ... --environment-capture ...`。
> 源码核对基线补充：Temporary Campus Kit V0 的库存audit、12件米制临时构件和隔离campus-*验收；见temporary-campus-kit-v0.md，未改WHU Reference JSON或正式地图。
> 源码核对基线补充：新增《重返武汉大学》信息学部核心区 V0 资料参考入口；仅核对米制/RFU契约，未改正式地图或运行时，高程与道路宽度仍未核实。
> 源码核对基线补充：Architectural V1 最终 panel/hatch 与 arch-* runtime 视觉签收完成，VISUALLY FROZEN；工程 checkpoint 待 Sol，边界及证据见 architectural-environment-v1.md。
> 源码核对基线补充：Architectural 闭合 prop scoped backface culling 位于 static prop renderer / frontend state，处理旧 static RFM2 v2 的双面薄墙穿透。
> 源码核对基线补充：Architectural Environment V1 的 Builder/registry、Wall/Floor Surface 与独立 arch-* 原型验收；设计冻结入口见 architectural-environment-v1.md。
> 源码核对基线补充：Campaign 环境设施组合、power_unit / gate_frame / control_cabinet；`--environment-capture` 与 `tools/environment_sheet.py` 复用正常 world render 验收。
> 源码核对基线补充：旧开发坡道/墙顶平台高端重叠衔接修复；未更改地图碰撞记录或 AI/波次/spawn 配置，边界见 gameplay.md。
> 源码核对基线补充：Enemy Procedural Rig V1 的 profile / truth adapter / pose / generic renderer；特感关键帧、轮廓与 world 验收；普通敌人新模型混合比例、Charger 新冲锋碰撞代理、Tank 击飞真实位置历史 ribbon；协议 43 命中 mask。
> 源码核对补充：RF Core lifecycle boundary 已覆盖 poll/exit、tick clock 与 frame begin/end；Runtime Environment V1 ownership audit 与 checkpoint 已完成。
> 源码核对补充：Outpost V0 已接入默认 Game landing、world identity/switch lifecycle 与三终端交互；F12/前哨站统一双应用桌面、可拖动窗口与 live PERSONNEL actor projection。
> 源码核对基线：工作区（Runtime Map V1 projection ownership cleanup；RF Application Projection Layer V0 query boundary；Phase 4.4 render IR/runtime/legacy draw adapter；Enemy Visual V2 六份公开 RFM2 / renderer-only family；Enemy Visual Family Mix V1 自动比例 resolver 与强制 family capture；MODEL_DISPLAY style 6--14 的三类型×三家族感染体展示；Character Material Lighting Policy V1；Enemy Presentation V1 1000ms ballistic fade / rotating irregular fragments / directional trailing emitter 与开发者死亡测试排；Humanoid Action Composition V1.1 additive recoil；modular RFANIM presentation clock；双手 RFANIM 持枪轨道；RFCHAR +Z forward basis；PRIMARY_GRIP weapon presentation；开发者 world strip 与战斗区共用 modular path；双正式四人小队 runtime；RF Core Runtime V0.2 `rf_game_runtime` facade 与 status query；Core/Game startup config split；renderer frame ownership cleanup；Core filesystem service V0；唯一 `rf_core` context 与 Core clock service；Runtime Facade Authority audit；Phase 3A `rf_game_update()` gameplay update authority；Phase 3B-1 world presentation migration；Phase 3B-2 steady-state Game UI presentation authority；Phase 4.3 surface IR/runtime/legacy primitive adapter）

本目录面向接手 Rasterfall 任务的编码代理。目标不是介绍玩法，而是先把问题归到正确的
状态所有者和文件，再开始搜索。命令、资源导入方法和用户可见特性仍以
[`../README.md`](../README.md) 为准。

文档职责分层如下：根目录 `README.md` 是仓库总览，`rasterfall/README.md` 是稳定的用户入口，
本目录是维护者导航和模块细节。易变的参数完整清单以 `build/rasterfall --help` 为准；可复核的
玩法、资产、模型和地图事实优先通过本仓库提供的 CLI 入口获取，详见根目录 `AGENTS.md` 的
“文档层级与 Agent CLI 事实入口”。

> 源码核对补充：正式 Hurd 四人使用专用 character IDs；原 Maid 四人旗卫已在西侧原位恢复并使用 Maid character/profession；普通 player、Eula 和佣兵为 character NONE；北侧 HURD 旗帜及派生 control status 已接入 session。
> 源码核对补充：World Content V1 已将 Outpost/Campaign 的 actor、flag、formation 和正常 renderer fixture 策略集中到 Game-owned `rasterfall_world_content`。
> 源码核对补充：Standard Response Squad 与 Assault Squad 各四人由 `rasterfall_roster` 提供有序 identity；session reset 将其接入普通 AI actor，并分别绑定中部 `RESP` 与东部 `ASLT` 旗帜部署位，renderer 再按 character profile 解析 modular recipe；`--squad-acceptance` 输出八人三视角验收。

## 先读哪一篇

> 源码核对补充：V2.1 Final Body 与 Profession Visual System V1 使用现有 RFCHAR carrier；
> `generate_rasterfall_humanoid_v2.py --profession` 拥有六职业装备与 palette，
> `--profession-lineup` 在同一画面验收，`tools/rf_profession_round.py` 编排生成与组图。

| 任务或症状 | 首先阅读 | 主要入口 |
| --- | --- | --- |
| 启动、参数、Core Host、runtime update/render 调度、Outpost landing | [runtime.md](runtime.md) | `src/rasterfall.c`、`src/rf_game_runtime.c`、`src/rf_game_lifecycle.c`、`include/rf_game_lifecycle.h`；world switch 入口为 `rf_game_request_world()` |
| Runtime Environment V1 总体边界与 checkpoint | [runtime-environment-v1.md](runtime-environment-v1.md) | Core、Game、Command、GUI、Application、Projection 与 Map Runtime 的 ownership relationship |
| Console command registry/context/status | [runtime.md](runtime.md)、[core-runtime-v0.2.md](core-runtime-v0.2.md) | `include/rasterfall_console.h`、`src/rasterfall_console.c`、`src/rf_game_runtime.c` |
| GUI desktop、icon/window presentation | [desktop-v1.md](desktop-v1.md)、[gui-runtime-v0.md](gui-runtime-v0.md)、[runtime.md](runtime.md) | `include/rasterfall_gui.h`、`src/rasterfall_gui.c`、`src/rf_game_runtime.c` |
| Application registration/open/close/update/render | [app-runtime-v0.md](app-runtime-v0.md)、[gui-runtime-v0.md](gui-runtime-v0.md) | `include/rasterfall_app.h`、`src/rasterfall_app.c`、`include/rasterfall_gui.h` |
| Application/Core/Game 查询边界、projection snapshot | [application-projection-v0.md](application-projection-v0.md)、[core-runtime-v0.2.md](core-runtime-v0.2.md) | `include/rf_application_projection.h`、`src/rf_application_projection.c` |
| Terminal frontend/session、Console modal 接入和查询命令 | [runtime.md](runtime.md)、[core-runtime-v0.2.md](core-runtime-v0.2.md) | `include/rasterfall_console.h`、`src/rasterfall_console.c`、`src/rf_game_runtime.c` |
| Core/Runtime 查询面与前哨站接口准备 | [core-runtime-v0.2.md](core-runtime-v0.2.md) | `include/rf_core_host.h`、`include/rf_game_lifecycle.h`、`include/rasterfall_session.h` |
| agent 固定视觉场景截图 / Visual CLI | [rendering.md](rendering.md)、[runtime.md](runtime.md) | options → `rasterfall_render_visual_capture()` → `src/dev-tests/rasterfall_visual_capture.inc` |
| Enemy Visual V2 六资产、家族切换、敌人截图与资产验收 | [enemy-visuals.md](enemy-visuals.md)、[rendering.md](rendering.md) | `rasterfall_enemy_visual.h` → `render/rasterfall_enemy_visual.inc`；`tools/enemy_visual_round.py` |
| 特感 rigid 模型、程序化步态、Charger/Tank attack 与新敌人扩展 | [enemy-visuals.md](enemy-visuals.md) | `rasterfall_enemy_rig.h` → `render/rasterfall_enemy_rig.inc`；`dev-tests/rasterfall_enemy_visual_capture.inc`；真实命中 VFX 在 `rasterfall_effects.c` |
| 武器、敌人、碰撞、寻路、波次、商店、AI | [gameplay.md](gameplay.md) | `lib/game.c`、`src/rasterfall_session.c` |
| Hurd 固定小队、北侧据点、旗帜控制真值 | [gameplay.md](gameplay.md)、[map-format.md](map-format.md) | `rasterfall_session.h` 的 Hurd config/status → `rasterfall_session_hurd_status()` |
| 地图格式、关卡实体、拾取物、静态 prop、出生点、render records、Outpost | [map-format.md](map-format.md) | `assets/maps/outpost.map`、`lib/rasterfall_map_parser.c`、`lib/rasterfall_map_runtime.c`、`src/rasterfall_map.c` |
| Map Compiler V1、Runtime Map ownership、Gameplay Projection Adapter、legacy fallback 边界 | [map-format.md](map-format.md) | `assets/maps/rasterfall.map`、`assets/maps/rasterfall_legacy.map`、`lib/rasterfall_map_parser.c`、`lib/rasterfall_map_runtime.c`、`src/rasterfall_map.c`、`src/rasterfall_session.c` |
| 连续外围墙、同色地面、组件碰撞模板与布局查询 | [map-format.md](map-format.md)、[rendering.md](rendering.md) | `include/rasterfall_map_components.h`、`lib/rasterfall_map_components.c` → Runtime Map collision expansion → gameplay projection；`map-inspect --collision-json` |
| 编写或扩展 `.map` 文本格式 | [map-format.md](map-format.md) | `lib/map.c`、`include/toy_map.h` |
| 修改地图排布、导出地图俯视图、agent 可读 JSON 和精确布局查询 | [map-format.md](map-format.md) | `tools/map_layout_export.py`、`tools/map_layout_query.py`、`make map-layout` |
| Return-to-WHU runtime compatibility、地面可读性、出生朝向与眼高验收 | [map-format.md](map-format.md)、[rendering.md](rendering.md)、[runtime.md](runtime.md) | `world attr.identity` → `rasterfall_session.c`；`rasterfall_world_content.c` 的单布尔 ground policy → `draw_partitioned_floor()`；`player_start` → Runtime region sy/cy → projection → session；`--map ... --environment-capture ...` |
| 《重返武汉大学》真实地点底图、坐标、尺寸来源与白盒前置调查 | [V0 计划](reference/return-to-whu-core/return-to-whu-core-v0-plan.md)、[调查报告](reference/return-to-whu-core/investigation-report.md)、[来源台账](reference/return-to-whu-core/sources.md)、[资料补充 V1](reference/return-to-whu-core/evidence-addendum-v1.md) | `reference/return-to-whu-core/whu-info-core-reference.json` 与同名 SVG/PNG；仅资料层，未知高程/宽度不得作为正式地图事实 |
| RenderFrame V1、sky/world/transparent/effects/viewmodel/overlay 层、场景、HUD、性能 | [rendering.md](rendering.md) | `include/rf_core_host.h`、`src/rf_game_runtime.c`、`src/rf_core_host.c`、`src/rasterfall_render.c`、`gpu/shaders/raster_v1.comp` |
| 角色 humanoid / 实景距离观察组图 | [asset-pipeline.md](asset-pipeline.md)、[rendering.md](rendering.md) | `tools/character_lab_sheet.py`、`tools/character_world_sheet.py` |
| RMESH 基础光照、角色 role 可读性策略、Lighting OFF/V1 回归 | [rendering.md](rendering.md) | `model_form_light_q8()` → `character_render_policy()` → `render_gallery_model_range()`；`lighting-props` / Character Acceptance `lighting-policy` |
| 世界位置光照查询、静态太阳遮挡、ground/architecture/static RMESH/dynamic entities 接入 | [rendering.md](rendering.md)、[Phase B 说明](static-world-lighting-phase-b.md)、[Phase A 开发记录](static-world-lighting-phase-a.md) | `include/rasterfall_world_light.h` / `src/rasterfall_world_light.c` 拥有 field/bake/bilinear/compose；Runtime Map collision/surface 只读进入 bake；renderer 持有缓存；正常 static RMESH 在 `render_static_props()` 按实例采样，详见 [Phase C1](static-world-lighting-phase-c1.md)；normal actor/enemy 与 owner weapons 的 frame scope、local viewmodel sample 见 [Phase C2](static-world-lighting-phase-c2.md)；form/material/fog 仍归原层 |
| Hurd 职业外观、低模 AI 人体、RF Humanoid V1/V2、基础外观、指定角色独立绘制入口 | [rendering.md](rendering.md) | `rasterfall_render.h` 的 `rasterfall_procedural_humanoid_state` / `rasterfall_render_procedural_humanoid()`；`rasterfall_character.h` 的基础/职业 profile；`dev-tests/rasterfall_visual_capture.inc` 的角色验收与 world strip |
| 中文 UI、UTF-8 文本和 GB2312 点阵字库 | [rendering.md](rendering.md)、[asset-sources.md](asset-sources.md) | `lib/graphics/fb_font.c`、`assets/fonts/` |
| world-space 静态 RMESH prop、实例变换和开发展示 | [rendering.md](rendering.md) | `include/rasterfall_render.h`、`src/rasterfall_render.c` |
| 战斗表现事件、muzzle/tracer/impact/camera shake 消费 | [rendering.md](rendering.md) | `include/rasterfall_effect_event.h`、`src/rasterfall_effects.c` |
| 敌人受击与死亡 raster presentation | [rendering.md](rendering.md)、[gameplay.md](gameplay.md) | `src/rasterfall_effects.c`、`src/rasterfall_render.c` |
| 模型、蒙皮、IK、VMD/GLB、动作重定向 | [assets-animation.md](assets-animation.md) | `src/rasterfall_model.c` |
| RFANIM 动作、lower/upper/additive 组合、关键帧检查、动作预览、pose/socket debug | [assets-animation.md](assets-animation.md)、[animation-architecture.md](animation-architecture.md) | `rasterfall_action_compose()` → `rasterfall_model_instance`；`build/rf_anim_info` / `--action-preview` / `--action-composition-capture` / `--pose-debug` |
| 出生点附近 V2 动作调试模型与切换按钮 | [rendering.md](rendering.md)、[map-format.md](map-format.md) | `button_humanoid_actions` → session presentation state → `render_humanoid_debug()` |
| 共享模型资源、独立 pose instance、instance socket/CPU skinning | [assets-animation.md](assets-animation.md)、[animation-architecture.md](animation-architecture.md) | `rasterfall_model_resource` → `rasterfall_model_instance`；`build/rfchar_runtime_test` |
| 独立 rigid RMESH、full rigid submission、HEAD/BACK assembly | [character-assets.md](character-assets.md)、[rendering.md](rendering.md) | `rasterfall_rigid_attachment_desc` → `rasterfall_render_rigid_attachment()`；`--rigid-attachment-acceptance` |
| Blender 人形角色、RF Humanoid、Character GLB、附件、导入与蒙皮门禁 | [character-assets.md](character-assets.md) | `tools/assets/rfchar_import.py`、`include/rasterfall_model.h`、`app/glb_inspect.c`、`dev-tests/rasterfall_visual_capture.inc` |
| V2 base body 收敛、AK 双手接触与冻结验收 | [character-assets.md](character-assets.md)、[rendering.md](rendering.md) | `generate_rasterfall_humanoid_v2.py` → importer bind 基底 → RFANIM `left_hand`/`right_hand` → `WEAPON_R` + `PRIMARY_GRIP`/`FOREGRIP` → `modular_solve_left_hand()` → `render_modular_preview_frame()` / `render_modular_ai_teammate()` |
| RF Humanoid clean face、Headgear / Face Coverage V1、覆盖率组图 | [character-assets.md](character-assets.md)、[asset-pipeline.md](asset-pipeline.md) | `generate_rasterfall_humanoid_v2.py --headgear` → `RF_HEAD` → `rf_humanoid_headgear_sheet.py` → Character Lab / world strip |
| V2.1 Final Body、六职业装备、Profession Lineup | [character-assets.md](character-assets.md)、[rendering.md](rendering.md) | `generate_rasterfall_humanoid_v2.py --profession` → 六份 `rf_profession_*` manifest → `--profession-lineup`；`tools/rf_profession_round.py` |
| 六职业 modular recipe / rigid gear、carrier A/B、普通队友迁移 | [character-assets.md](character-assets.md)、[rendering.md](rendering.md)、[gameplay.md](gameplay.md) | `rasterfall_character_visual_recipe()` → shared body instance → passive HEAD/CHEST/BACK/HIP assembly + active WEAPON_R/PRIMARY_GRIP presentation；`--profession-lineup` / `--visual-capture modular-teammate` |
| 正式 RF 小队 roster、角色 identity、八人验收 | [gameplay.md](gameplay.md)、[rendering.md](rendering.md)、[runtime.md](runtime.md) | `include/rasterfall_roster.h` / `src/rasterfall_roster.c` → `session_spawn_formal_rosters()` → `render_modular_ai_teammate()`；`--squad-acceptance` |
| 导入 PMX/GLB、manifest、纹理、LOD、模型诊断 | [asset-pipeline.md](asset-pipeline.md) | `tools/assets/import_asset.py`、现有转换器、模型加载器 |
| Eula Gameplay Hybrid LOD、区域/关节/skin-weight 保护 | [asset-pipeline.md](asset-pipeline.md)、[assets-animation.md](assets-animation.md) | `tools/rmesh_lod.py --region-profile`、`tools/assets/lod_profiles/eula_gameplay.json`、`make lod-eula-gameplay` |
| Eula legacy VMD 动画签收、角色模型统一性能预算 | [rendering.md](rendering.md)、[asset-pipeline.md](asset-pipeline.md) | `--eula-animation-acceptance`、`tools/eula_animation_acceptance_sheet.py`、`--character-performance-suite` |
| 程序化工业/军事环境组件、Blender 批量导出 | [industrial-props.md](industrial-props.md)、[asset-pipeline.md](asset-pipeline.md) | `tools/blender/generate_rasterfall_props.py` |
| 建筑模块、管线端口、墙地语言、服务巷/开放厂房原型 | [architectural-environment-v1.md](architectural-environment-v1.md) | 同一 Builder 的 `build_architecture()` → industrial manifests / prop registry → `architecture_capture()`；`tools/architecture_round.py` |
| WHU资产清点、临时校园墙窗/台阶/树代理、独立街角验收 | [temporary-campus-kit-v0.md](temporary-campus-kit-v0.md) | `tools/blender/generate_campus_kit.py` → campus manifests / prop registry → `campus_capture()`；`tools/campus_kit_round.py` |
| 正式 Campaign 建筑/环境组合与固定多区域实景验收 | [architectural-environment-v1.md](architectural-environment-v1.md)、[industrial-props.md](industrial-props.md)、[map-format.md](map-format.md)、[rendering.md](rendering.md) | `assets/maps/rasterfall.map` 的 `env_arch_*` → runtime object projection（保留 y）；`--environment-capture` → `tools/environment_sheet.py` |
| 环境组件 V2 风格、palette、几何/纹理预算与验收 | [environment-art.md](environment-art.md)、[industrial-props.md](industrial-props.md) | 十件 static prop 源资产与游戏内展示 |
| 整套 V2 Hybrid 生成、flat/局部 sign 分配 | [industrial-props.md](industrial-props.md)、[asset-pipeline.md](asset-pipeline.md) | 生成器 `PILOT`、`ACCENTS`、`Builder.box()`、`hybrid_prop()`；crate 沿用 `hybrid_crate()` |
| 静态 prop 资产 ID、路径、展示缩放和默认尺寸 | [asset-pipeline.md](asset-pipeline.md) | `include/rasterfall_prop.h`、`src/rasterfall_prop.c` |
| 联机协议、快照、预测、可靠事件、房间发现 | [networking.md](networking.md) | `src/rasterfall_net.c` |
| Linux/Windows 平台差异、构建、测试 | [build-platforms.md](build-platforms.md) | `Makefile`、`windows/Makefile` |
| GPU service/capability、normal world selected-stream 与 CPU↔GPU differential/replay | [build-platforms.md](build-platforms.md)、[rendering.md](rendering.md)、[`../../gpu/README.md`](../../gpu/README.md) | flush 前 command observer / Core consumer → Raster V1 + Texture V1 packer → hosted replay 或显式 normal gpu-compute；默认 renderer 仍为 CPU |
| 动画求值顺序、格式/角色扩展契约 | [animation-architecture.md](animation-architecture.md) | `src/rasterfall_model.c`、动画头文件 |
| 网络状态所有权、协议和房间生命周期 | [network-architecture.md](network-architecture.md) | `src/rasterfall_net.c`、公共协议头 |
| 资源来源、许可和发布检查 | [asset-sources.md](asset-sources.md) | 资源目录与导入工具 |

世界光照最终冻结、世界尺度、性能与验收边界见 [Phase D](static-world-lighting-phase-d.md)。
已有 Character world capture 的固定输入/headless 配置归 `rf_game_runtime.c`，不改变 renderer 测试带光照。
世界光照 consumer 清理、诊断边界与回归入口见 [Phase C3](static-world-lighting-phase-c3.md)：
`rasterfall_world_light.h/.c` 拥有 V2 field；`rasterfall_render.c` 的默认 world adapter、scene override
及 `src/dev-tests/` 的显式诊断 scope 共同定义消费边界。

## 地图 V1 输入链路

默认启动加载 Game 选择的 `RASTERFALL_WORLD_OUTPOST`（`outpost.map` + `outpost.content`）；C parser 生成 Map IR，Runtime Map 持有
authoritative world data，`src/rasterfall_map.c` 的 Gameplay Projection Adapter 再创建现有
gameplay、collision 和 renderer 接口所需的数据视图。`toy_map`、`session.level` 的 primitives、
safe_rooms、spawn_zones、props、interactables 都属于迁移期 compatibility/runtime view，不是地图真相。
World Content V1 由 Game-owned parser 单独加载，描述 actor、terminal、flag、formation 和 fixture；
它不进入 Map Runtime。对外统一称呼为“空间地图”和“内容地图”：`map-layout` 输出空间地图，
`world-layout` 将空间地图与 World Content 合并后输出内容地图。
稳定 ID 是查询边界，record 文本顺序不承载语义。`--legacy-map` 显式启用 `rasterfall_legacy.map`，
仅用于 legacy compatibility fallback/reference；它不参与默认加载。`map-inspect`、`map-runtime-test`
和 projection count check 均通过 C parser/Runtime Map 链路验证地图。
Normal world 的 actor 与 renderer-only fixture 还必须经过当前 World Content policy；`toy_game_init()`
不创建 Jesus 或其他命名队友，固定 Eula/developer strip 与 Humanoid debug 也不会在 Outpost
normal render 中进入。诊断 CLI 保留自己的独立 fixture 路径。

## 架构主线

```text
平台事件/网络包
      ↓
src/rasterfall.c                 进程入口（Core Host / Game facade 调度）
src/rf_game_runtime.c            Game runtime、菜单、输入、固定步长帧循环
      ↓
src/rasterfall_session.c         单机/主机/客户端会话编排与 controller
      ↓
actor API → toy_game_actor       各类角色共享的状态与动作入口
      ↓
lib/game.c                       world simulation 与确定性玩法规则
      ↓
src/rasterfall_render.c + HUD    只读玩法状态并生成画面
      ↓
toy_renderer / window / audio    仓库公共平台层
```

战斗表现事件是 presentation-only 数据，统一经 `rasterfall_effects` 消费为短生命周期表现状态，
并登记到固定容量的 `rasterfall_effect_instance` runtime 池；instance 将底层组件类型与效果语义
分离，tracer、命中火花、分层 muzzle flash 和 Molotov 火焰已由 runtime `RAY`/`PARTICLE`/`BILLBOARD` 组件绘制；屏幕空间反馈已有 `OVERLAY` 原语入口，主循环通过统一 `rasterfall_render_effects()` facade 消费这些 instance。FIRE 与 EXPLOSION 通过统一 emitter preset table 描述生命周期、生成间隔和固定 child descriptor 列表。它不进入
受击时由展示态生命值下降触发本地 `CAMERA_SHAKE` preset；同一展示同步入口生成浅色四角边缘
闪红，并根据最近存活敌人与相机朝向生成八方向中心受击箭头。其参数和最短间隔位于
`include/rasterfall_effects.h`。它不进入 `toy_game` 的权威状态同步。

网络主机运行权威会话；客户端通过 `rasterfall_net.c` 的快照、预测与校正形成展示状态。炸弹和
Molotov 的世界实体显式携带 `owner_actor_id`，本地预测 body 位置再派生 camera 展示位置。
`struct camera` 已显式区分 `body` 与 `view` 命名空间；扁平字段仅作为迁移期布局兼容别名。
主机和客户端的开火展示都经 `sync_network_fire_effects()` 适配到同一 runtime，并按 fire sequence
抑制重复事件。不要把纯视觉状态塞进 `toy_game`，也不要让渲染器修改权威玩法结果。

player/actor 和敌人的 airborne forced/knockback movement 均由玩法核心按各自碰撞半径分段扫掠；每个子步
保留 X/Z 分轴滑动，并在受阻时清除对应 knockback 分量。不可高抛越过的地图边界使用 primitive
的显式 `BLOCKS_AIRBORNE` 属性，不从 `role` 推导。

## 目录边界

- `rasterfall/include/`：模块公开结构、枚举和函数契约；定位状态所有权时先看对应头文件。
- `rasterfall/lib/`：可脱离窗口和渲染验证的玩法、地图解析和声音合成核心。
- `rasterfall/src/`：应用编排、网络、渲染、资产运行时和界面。
- `rasterfall/assets/`：公开运行时地图、纹理、音效和模型。
- `rasterfall/private-assets/`：可选本地资产；代码不能假设每个工作区都有它。
- `app/`、`lib/`、`include/`、`windows/`、`tools/` 中也有 Rasterfall 使用的转换器与平台层，
  详见各模块文档。

专题设计和活动台账：

- [Rasterfall GPU 与 Windows Native Platform 阶段计划](../../docs/Rasterfall%20GPU%20%E4%B8%8E%20Windows%20Native%20Platform%20%E6%80%BB%E4%BD%93%E8%AE%A1%E5%88%92.md)：GPU checkpoint、已冻结契约、normal-frame 阻塞、剩余问题与最终目标。
- [industrial-props.md](industrial-props.md)：十件 V2 Hybrid 规格、flat/decal 分工、米制轴向、碰撞建议与生成/统一导入流程。
- [environment-art.md](environment-art.md)：十件工业 / 军事组件的 V2 light upgrade 风格、逐件要点、预算、验收与试点顺序。

- [animation-architecture.md](animation-architecture.md)：动画数据流、不变量和扩展边界。
- [character-assets.md](character-assets.md)：RF Humanoid V1、Character GLB、attachment 与 skinning 的冻结契约和 validator 门禁。
- [network-architecture.md](network-architecture.md)：联机状态分类和房间生命周期。
- [asset-sources.md](asset-sources.md)：资源身份、许可状态和发布前检查。
- [asset-pipeline.md](asset-pipeline.md)：资产转换、检查器、LOD 和离屏回归命令。
- [map-format.md](map-format.md)：地图文本语义、示例和跨层修改要求。

历史资料放在 `archive/`。其中的结论只描述当时现场，不是当前设计依据；除非任务明确要求追溯
历史，不要先阅读归档文档。

## 修改时的定位原则

先找“谁拥有状态”，再找“谁展示状态”。改公共结构时同时搜索其序列化、测试和所有调用者。
尤其注意以下跨层联动：

- 修改 `toy_game.h` 的武器、敌人、事件或结构布局：检查 `lib/game.c`、session、HUD、渲染和网络编码。
- 修改地图语义：检查 `toy_map.h`/`lib/map.c` 的解析、`rasterfall_map.c` 的绑定、玩法碰撞和渲染。
- 修改角色动画：检查角色选择、会话动画状态、模型求值、渲染以及网络动画字段。
- 新增 humanoid gameplay action：动作数据归 `rasterfall_action`，track 只使用 stable humanoid role；
  actor 提供语义动作和确定性时间，presentation adapter 选择固定 lower/upper layer，composition evaluator
  写 instance pose，socket/附件/renderer 只读 finalized pose。actor 不保存 layer、clip 或骨骼状态。
- 修改模型 runtime ownership：资源定义只由 `rasterfall_model_resource` 拥有；逐实例调用通过
  `rasterfall_model_instance`。同步验证 RFCHAR isolation 门禁与 Character Acceptance 双实例截图。
- 修改 rigid attachment：资产保持无 skin/无宿主骨架的 metric RMESH；assembly 只能从 finalized
  instance 查询 stable socket，并在 renderer 内组合 mount correction 与 actor/world transform。
- 修改离线资产导入契约：从 `tools/assets/import_asset.py` 和 manifest 开始，分别检查
  `glb2rmesh`/`pmx2rmesh`、`toyasset`、`rmesh_lod.py`；不要让 runtime 读取 manifest。
- 修改敌人模型家族：从 `rasterfall_enemy_visual.h`、`render/rasterfall_enemy_visual.inc` 的 recipe 与 cache 开始；同步六份 manifest、公开资源、options、capture 与 embedded/package 边界，禁止把 family 写入 enemy/snapshot。详见 [enemy-visuals.md](enemy-visuals.md)。
- 修改敌人外观组件：特感先查 `rasterfall_enemy_rig.h` 与 `render/rasterfall_enemy_rig.inc` 的 profile/adapter/pose；旧普通敌人查 `src/rasterfall_render.c` 的 `enemy_body_part`。动态舌头从 posed HEAD 取起点；命中粒子消费权威 mask，地面锚点仍由 `ground_y` 与 `airborne_y` 提供。
- 修改命令行或诊断模式：从 `rasterfall_options.c` 到 `rasterfall.c` 的早退分支一起核对。
- 修改职业外观：四个 Hurd profile 与 Maid profile 保存 profession identity；普通 player、Eula、佣兵的 `character_id` 为 NONE。Maid 旗卫仍由 `anime_character_id` 选择各自骨骼模型，同时由 `character_id` 标识共同 Maid 职业。静态 presentation profile 保存附件配置；actor 展示适配器从 `character_id` 解析职业。actor 和网络不携带重复的职业或附件字段；Visual CLI 提供固定 Hurd 小队。
- 六职业 Profession Modularization V1 由 presentation recipe 解析共享 RF Humanoid V2 body、per-profile shirt/pants palette 与 rigid gear ID；旧 `rf_profession_*` 仅作 legacy acceptance carrier。普通队友 Jesus 使用稳定 RF Rifleman identity 与逐 actor presentation instance；资源失败回退 procedural，actor 不持有资源、路径或附件数组。
- 修改角色状态：从 `toy_game_actor`、actor API 和
  `rasterfall_session.c` 的本地主循环开始；不要把 camera 的位置字段写回为 gameplay 源。
- 修改正式小队内容：从 `rasterfall_roster.h/.c` 的 ordered character list 开始，再检查
  `rasterfall_session.c` 的 reset adapter、character profile/recipe 映射、网络 character byte 和
  `--squad-acceptance`；不要把 squad 或 visual resource 字段塞进 actor。
- 修改 Hurd 据点：固定角色索引和 RFU control region 属于 session；assignment 只读
  `actor.flag_index`，capable 只认 ALIVE 且 HP 大于零；旗帜、actor 和 revive 仍是底层玩法真值。

## 修改后更新哪些文档

| 改动 | 必须同步检查 |
| --- | --- |
| 模块拆分、状态所有权或主数据流变化 | 本页及所有受影响模块导航 |
| 动画格式、姿态顺序、角色扩展机制 | `assets-animation.md`、`animation-architecture.md` |
| 资产格式、转换器、LOD 或诊断入口 | `asset-pipeline.md`、`assets-animation.md` |
| 地图记录、属性或绑定语义 | `map-format.md`、`gameplay.md` |
| 协议字段、权威规则、房间生命周期 | `networking.md`、`network-architecture.md` |
| 构建目标、平台源文件或资源打包 | `build-platforms.md`、用户 `../README.md` |
| 资源来源、许可或发布范围 | `asset-sources.md` |

更新正文时同时刷新顶部“文档更新”和“源码核对基线”。重大重构、重要文件迁移或重大功能变动
必须在同一改动中更新本索引，确保下一位维护者在阅读源码前得到正确入口。
