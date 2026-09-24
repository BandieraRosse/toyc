# Scene 动态特感身体现场（2026-09-24）

> 状态：历史现场归档；阶段 2 的离屏审计切片，不是完整 WORLD 或正常 Scene 呈现签收

当前来源合同见[角色表现](../architecture/character-presentation.md)，提交和资源边界见
[GPU 渲染架构](../architecture/gpu-rendering-architecture.md)，复现见[Scene fixture](../guides/gpu-scene-fixture.md)。

本切片从正常帧的 mixed producer 冻结 Smoker、Charger、Tank 的存活身体 finalized pose、
受击偏移、方向、地面/腾空 lift、反馈颜色和光照模式。旧绘制与新 Scene 共用刚性形体枚举。
Scene 预备只读冻结值，不再次访问 enemy 数组或推进 observer；每个来源槽的动态资源由诊断 owner
持有，同步提交完成后才能在下帧重建。帧内 source slot 不承诺跨帧身份。

Windows 原生构建、package 与完整逻辑回归通过；逻辑日志位于 `tmp/scene-enemy-test-final.log`。
新增逻辑用例覆盖三种形体的确定性、pose 变化、callback 失败、unsupported 类型和 begin/freeze 边界。
RTX 3050 Laptop GPU 的定向验证命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_scene_enemies.ps1 -OutputDirectory tmp/gpu-scene-enemies-final -ValidationLayerDirectory tmp/scene-validation-tools/mingw64/bin
python tools/gpu_scene_enemy_pixels.py tmp/gpu-scene-enemies-final
```

实机原始 stdout/stderr、BMP、Scene PPM、`hashes.json` 与 `pixels.json` 均在该目录。
被测 package executable SHA-256：`3EA782B0478637C7CC1BED0E5198D9D22C1C0F79227AB45CB61EF1BFB6E32D07`。
三个运行均退出 0；loader 证明 Khronos layer 实际插入，日志列出
`VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`，未出现 VUID 或 sync hazard。

| 固定输入 | 结果 |
| --- | --- |
| `enemy-special 0`，2 帧、capture 第 1 帧 | 每帧 3 个身体，157 个特感 draw，暂缓为 0 |
| `enemy-special 0`，12 帧、capture 第 12 帧 | 每帧 3 个身体，动作推进后 167 个特感 draw，暂缓为 0 |
| `near 30`，2 帧 | 特感项与 draw 均为 0，30 个普通感染体明确计入暂缓 |

两张特感截图各检查 7 个固定像素，mixed 与 Scene RGB 全部一致；其中动作推进后的局部颜色改变，
没有把首帧截图重复用作动态证据。它仅证明局部身体、姿态和光照，不是完整画面容差审批。
正常呈现仍为 mixed；Scene target 有显式诊断 readback。普通感染体、死亡表现、blob shadow、
Smoker 舌头与其余角色未接入。动态网格当前按同步审计帧重建，不据此宣称性能改善或正常帧资源策略完成。

早期一次验证与 package 重建重叠，资源目录被替换，该次样本作废；恢复 package 后串行重跑得到上述结果。
不要在游戏或 GPU 验证运行期间执行会替换 package 的 build wrapper 命令。
