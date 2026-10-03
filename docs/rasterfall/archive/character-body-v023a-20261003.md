# RF-C01 V23a 身体修订现场

> 状态：历史记录；2026-10-03 的内容制作与局部验证，不定义运行时能力
> 当前入口：[角色设计](../reference/rf-c01-design-study.md)、[分部件工作流](../guides/character-parts.md)、[实验场预览](../guides/gpu-scene-fixture.md#rf_model_lab-角色预览)

用户授权本轮视觉升级，并明确优先改善动漫角色身体。对象为私有 RF-C01，起点是 V22h 完整清单；
公开 RF Humanoid 与敌人身体不在这次内容修订范围内。

## 已完成内容

V23a 只替换 `body` 部件。连续夹克表面调整袖山、腰部织物体积和袖口，增加肩缝、胸袋开口、裁片线、
腰带环。工装裤细化膝/裤脚局部褶皱，侧袋加翻盖和扣片；厚矩形膝片替换为贴合腿部的曲面护膝。
手套增加腕口、指节层次并收敛指尖，靴子修整鞋头并增加靴口和鞋面分区。细节复用原有五类身体材质，
完整角色仍为 19 个材质，无纹理、透明 pass 或新着色器。

身体原有 10 个无面顶点被清理，旧手套面板的 24 个重合倒角面通过局部焊接移除；旧膝片退化面
随替换消除。头脸、眼部、口腔、头发、表情、49 骨骼、8 个 attachment、动作、V20c 接口和固定审阅
场景保持原语义记录，原文件哈希核对通过。

| 预算 | V22h | V23a |
| --- | ---: | ---: |
| 身体源三角形 | 26,192 | 24,664 |
| 完整 GLB/RFM2 顶点 | 51,984 | 47,759 |
| 完整 GLB/RFM2 三角形 | 74,596 | 73,068 |
| 导出材质 | 19 | 19 |

新增细节后身体三角形减少约 5.8%，完整角色顶点减少约 8.1%。这是资产几何预算，不能作为帧率
提升的实测结论。

## 源与复现

私有根为 `rasterfall/private-assets/source/characters/rf_c01/authoring/`。

| 入口 | 用途 |
| --- | --- |
| `assembly-body-v023a.json`、`parts/body/v023a.blend` | 最终部件锁定与冻结源 |
| `build/v023a/rf_c01-v023a.blend`、`.glb`、`.rmesh` | 完整组装、交换格式和运行资源 |
| `build/v023a/review.html` | V22h/V23a 固定视图、近景和动作审阅 |
| `scripts/build_body_v023.py` | 从锁定 V22h 网格创作身体；不执行历史生成器 |
| `scripts/finalize_body_v023a.py` | 仅清理遗留手套面板重合倒角 |
| `scripts/validate_body_v023.py` | 部件隔离、拓扑及 51 个动作采样 |
| `scripts/review_body_v023.py -- v023a` | 固定 CPU Blender 全身、近景、步行和抬臂渲染 |
| `scripts/native_body_v023.py` | Windows importer、实例合同与独立版本安装 |

生成器和发布工具均拒绝覆盖已有源 revision/组装结果。重建应在保留清单相对结构的独立副本中运行；
不得为了重跑删除既有候选。生成顺序为 build → finalize → validate → native；各步确认退出码后再继续。
Blender 使用 `E:/Blender 5.2/blender.exe --background --python-exit-code 1 --python <脚本>`。
原生 importer 使用 `--position-scale 65536`；实例工具传入绝对 RFM2 路径。

运行资源安装到 `rasterfall/private-assets/models/rf_c01_v023a.rmesh`，角色目录和保真诊断的默认路径同步。
旧 V22h、V23 和其产物保留；私有几何和预览不加入 Git，Windows 本地 package 按已有规则复制运行模型。

## 验证与边界

已通过保存重开与仅 body 部件隔离；头部、骨架、动作、接口和审阅场景保持。身体零孤点、无三面共边、
无零面积面；已有 idle/walk/aim 各 17 次采样无非有限坐标/法线或退化三角形。固定前侧后、三分之四、
身体近景、步行和抬臂视图已检查。细节沿用既有权重，最多两项有效 influence。

Windows GLB contract 为 0 error / 0 warning；RFM2 v14 导入、双实例姿态与 socket 隔离通过。
详细结果分别保存在 `validation.json`、`deformation-validation.json`、`native-validation.json`。
这些检查不证明所有动作无部件交叠，也不代表正式持枪、RFANIM 全套动作、纹理、LOD 或头部改善。
正常 GPU 帧的联动检查由本轮整体视觉验证记录拥有。
