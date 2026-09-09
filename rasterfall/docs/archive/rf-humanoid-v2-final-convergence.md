# RF Humanoid V2 Final Convergence 现场记录

日期：2026-09-10。本记录是当前工作区验收现场，不替代 character-assets.md 的契约。

建议冻结本版 body 的比例、几何、五色块和附件布局，作为通用 AI/NPC 扩展基线。
这是 base body 的冻结，不代表所有 locomotion、reload、downed 动画或职业附件已经验收。
开发者区默认 body 已切到 V2；没有替换全部 gameplay actors。

## 源资产变化

- 骨盆最大半宽 0.310 → 0.275m，腰半宽 0.215 → 0.235m；骨盆/腰宽比约
  1.44 → 1.17。裤腰上端藏入 shirt 内，消除侧面/背面交叠闪烁。
- 胸侧半宽 0.315 → 0.290m；肩峰截面 0.155 → 0.120m，肩峰重心略抬高，
  更早混合到 upper arm，避免整个肩帽跟随 shoulder 形成刚性护肩。
- 大腿根最大半宽 0.160 → 0.138m；修正左右 knee 网格原先共同落在 X=0 的错误，
  归到 X=±0.15m，并让膝盖略向前。小腿峰值上移，脚踝进一步收窄；两鞋底严格落在地面。
- 头部原有 jaw/cheek/temple 体块保留；hair crown 高度 2.095 → 2.080m，侧锁收小，
  闭合发帽前部、让大块前缘与发帽衔接。没有增加发丝系统。
- 保留 hair/skin/shirt/pants/boots 五大材质色块和原有色值；避免造型与配色同时漂移。

## 最终资产统计

| 项目 | 值 |
| --- | ---: |
| Blender source vertices | 968 |
| Triangles | 1832 |
| Runtime vertices（flat normal 拆分后） | 4816 |
| Mesh nodes / materials | 26 / 5 |
| Bones / attachments | 29 / 8 |
| BDEF1 / BDEF2 vertices | 3706 / 1110 |

GLB/RMESH 位于 `rasterfall/private-assets/`，不提交；可复现生成器和 manifest 在 tools 中。
格式仍为 RFCHAR V1 / RFM2 v14 / SKN1 / CHR1。

## 挂枪修复与门禁

复查发现旧验收复用了 Maid 校准且未求解 RFCHAR 双手；旧 importer 还直接写 GLB parent-local
附件 TRS，与 SKN1 identity-rest 基底不一致。已烘焙附件 bind 基底，并增加源 GLB/runtime
附件变换交叉检查。角色仍使用标准 AK，CHEST 枪架、WEAPON_R 与 FOREGRIP 接触点。
双臂通过稳定 role 求解，独立 pole、迭代扣除掌部 socket offset。

最终 socket 到武器 anchor 的误差（源 RFU，512 RFU/m）：

| 姿态 | 右手 | 左手 |
| --- | ---: | ---: |
| rifle idle | 0.788 | 1.718 |
| rifle aim | 1.222 | 2.059 |

验收阈值为 4 RFU，失败返回非零。正面相机现在对应 canonical +Z，不再把背面标作 front。
idle 为低持枪，aim 平持枪；侧面/3/4 可以读出枪管、弹匣、双手与肘部。纯正面枪身自然缩短，
不能期待与侧面一样的枪长。枪托在本次观察中未再从背部露出。

## 可复核产物与结果

- `tmp/rf-v2-final/acceptance/`：bind/idle/aim × front/side/back/three-quarter，以及三档 A/B。
- `tmp/rf-v2-final/world/`：整条 strip 三档距离与 old/idle/aim/motion 各自三档距离。
- `tmp/rf-v2-final/lab-sheet.png`、`world-sheet.png`：用于审阅的缩略总览；原始 BMP 保留。
- `repeat-acceptance/`、`repeat-world/`：第二次独立运行。两组各 15 张 BMP 全部逐字节一致。
- validator：零错误、零警告；runtime load/CPU skinning、V2 attachment bind 交叉检查通过。
- `test_rfchar_pipeline.py --blender-python-path /home/lu/.local/lib/python3.13/site-packages`：通过，
  原有 fixture 四张 hash 基线保持一致。首次未指定路径因 Blender 缺 numpy 失败，指定已有路径后通过。
- GCC 构建、`--logic-test`、`git diff --check` 通过。未做 Windows 构建或交互窗口长时间运行。

实景使用真正地图与 world renderer，不隐藏 props；镜头固定在目标相对 (0.6d,0,0.8d)，
d=2000/4000/8000 RFU。旧直视镜头中工业组件或边界墙挡住角色，不能作为有效远景验收。

near：旧 procedural 的平板胸、细柱腿与符号脸明显；V2 腰臀更中性，头发、鞋、持枪有独立轮廓。
mid：头、上下装、靴子保持分层；idle/aim 的枪口高低可辨认。
far：idle/aim 主体和持枪方向仍可读，膝盖/手部细节无法可靠分辨；motion 的鞋部在该固定镜头中
仍受前景 ammo container 局部遮挡，不将该张作为完整脚部远景通过依据。idle/aim 的脚部可见。

后续优先事项：近景掌部仍是大块简化体；同色衣袖在纯正面弱光下仍有轮廓合并；完整运动/重装填
与 headgear 的变形、穿插必须各自验收。这些限制不要求重新探索当前 base body 的比例与风格。
