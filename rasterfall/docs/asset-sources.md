# Rasterfall 资源来源与发布限制

> 文档更新：2026-09-03
> 源码核对基线：`75a10cd`（将项目协作说明转向 Rasterfall）

本文是资源来源、许可状态和本地样本身份的活动台账。文件存在或能够构建不代表允许公开分发；
发布前必须逐项确认原件、作者和许可。`private-assets/` 与 `.claude/` 是本地区域，不进入公开资源
或内嵌发布目标。

## 角色模型

| 运行时名称 | 已知原件 | 转换产物 | 当前发布结论 |
| --- | --- | --- | --- |
| Eula / 优菈 | 原始 PMX 当前无法核实 | `private-assets/models/eula.rmesh` | 未找回原包和许可前不得公开分发 |
| ST AR-15 | `.claude/AR15/GirlsFrontline AsteriaDefault.pmx` | `st_ar15.rmesh`、LOD 和纹理 | 本地 Readme 禁止二次配布、商业使用和拆取部件 |
| G11 | `.claude/G11/GirlsFrontline MishtyDefault.pmx` | `g11.rmesh`、LOD 和纹理 | 未见独立许可，按同包最严格限制处理 |
| Vector | `.claude/Vector/GirlsFrontline VectorDefault.pmx` | `vector.rmesh`、LOD 和纹理 | 未见独立许可，按同包最严格限制处理 |
| UMP45 | `.claude/UMP45/GirlsFrontline LevaDefault.pmx` | `ump45.rmesh`、LOD 和纹理 | 未见独立许可，按同包最严格限制处理 |
| Quaternius UAL1 Standard | `private-assets/models/UAL1_Standard.glb` | 运行时直接读取 | 发布前回到原下载页核对资源包和许可 |

角色显示高度是项目校准数据，不是版权方官方设定。来源等级和具体参数如仍有维护价值，应记录在
角色 presentation profile 或校准文档，不作为许可依据。

## 动画

| 运行时文件 | 已知来源 | 本项目处理 | 当前发布结论 |
| --- | --- | --- | --- |
| `walk04_loop5.vmd` | `walk04.vmd`，说明署名 `mototo=toto@note0928` | 拼接五次循环 | 说明要求二次分发和商业利用前联系作者，未获许可不得公开分发 |
| `曼珠沙華.vmd` | 作者和随附许可无法核实 | 原样本地使用 | 不得公开分发 |
| UAL1 GLB 内 Idle/Walk/Jog | Quaternius UAL1 Standard | 运行时直接读取 local TRS | 发布前重新核对原包许可 |

`walk04_loop5.vmd` 的确定性生成脚本尚未保存在仓库。需要重建时，应先补脚本并记录输入哈希、
帧区间和输出哈希。

## 公开模型与其他资源

`assets/models/*.rmesh` 中的武器、弹药箱和道具是运行时转换产物。原件曾位于 `.claude/glb/`，
但当前工作区无法从这些产物恢复作者、资源包名称或许可。发布前必须重新建立逐项来源台账，
文件名本身不是许可证明。

同样需要核实 `assets/audio/*.tsnd`、`model_diffuse.ttex` 和 `wall.ttex` 的源文件与生成方式；
除非能够确认是项目自行生成或许可明确，不应假定可以再分发。

## 已知样本指纹

以下 SHA-256 只用于确认找回的是否为同一输入或运行时产物，不表示许可或所有权：

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

## 发布前检查

1. 找到原始下载页、压缩包和随附许可，记录作者、版本与下载来源。
2. 确认许可覆盖转换产物、修改、再分发和计划中的商业/非商业用途。
3. 用哈希确认原件身份；重新生成派生产物并记录可复现命令。
4. 检查 Linux 文件资源、embedded 资源和 Windows ZIP，确保私有资源没有被意外包含。
5. 将确认结果更新到本台账；不确定项保持不发布。

原始的暂停开发现场和更详细的当时调查记录保存在
[`archive/project-handoff-2026-09.md`](archive/project-handoff-2026-09.md)，仅供历史追溯。
