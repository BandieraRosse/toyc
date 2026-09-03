# Rasterfall 资产转换与诊断

> 文档更新：2026-09-03
> 源码核对基线：`75a10cd`（将项目协作说明转向 Rasterfall）

本文记录可执行的模型、纹理和动画工具链。运行时模块边界见 `assets-animation.md`，动画求值契约
见 `animation-architecture.md`，资源是否允许发布见 `asset-sources.md`。

## 静态 GLB 转 RMESH

```sh
make app-glb2rmesh
build/glb2rmesh input.glb rasterfall/assets/models/output.rmesh
```

`app/glb2rmesh.c` 读取 GLB mesh primitive 的 POSITION、可选 NORMAL/TEXCOORD_0 和常见三角形
索引，合并 primitive 并修正索引基址。坐标按资产边界的换算进入 Rasterfall 单位；运行时不解析
glTF JSON。静态转换路径不导入 GLB 骨架和动画，不能替代 GLB 动画预览路径。

## PMX 转 RFM2/TTEX

```sh
tools/import-pmx-model.sh path/to/character-folder character
tools/import-pmx-model.sh --force path/to/character-folder character
```

脚本要求输入范围内只有一个 PMX，构建 `pmx2rmesh` 与 `toyasset`，在临时目录生成模型和纹理，
全部验证成功后才移动到目标目录。`app/pmx2rmesh.c` 负责 PMX 网格、材质、骨骼、BDEF1/BDEF2
蒙皮及已支持 IK/grant metadata；`toyasset convert` 把受支持图片转换为 TTEX。

RFM2 是演进中的运行时格式，加载器保留多个旧版本兼容分支。修改格式时必须同步：

- 转换器写入与 `rasterfall_model.c` 读取；
- 版本号、记录宽度、边界检查和旧版本兼容；
- 材质、纹理、蒙皮、IK/grant 诊断；
- 公开模型、私有模型、LOD 工具和 Windows 构建。

当前精确版本与字段布局以转换器和加载器为准，不在本导航复制逐版本字节表。

## LOD

```sh
make lod-characters
# 或使用 Makefile 中的 lod-eula、lod-ar15、lod-ump45、lod-vector、lod-g11 等目标
```

`tools/rmesh_lod.py` 简化索引并保留顶点、骨骼、蒙皮与材质布局。LOD 与完整模型共享纹理；缺少
LOD 文件时运行时应回退完整模型。修改选择阈值或布局假设时同时检查 `rasterfall_render.c`。

## GLB 与 VMD 检查

```sh
make app-glb-inspect app-vmd-inspect
build/glb-inspect animation.glb
build/glb-inspect animation.glb humanoid
build/glb-inspect animation.glb basis
build/glb-inspect --self-test
build/vmd_inspect motion.vmd model.rmesh
```

`glb_inspect` 检查 node、skin、accessor、animation 和 humanoid/rest basis，不生成运行时模型。
`vmd_inspect` 检查骨骼名、映射、关键帧、IK 和运动诊断。游戏运行时的 VMD/GLB 诊断参数以
`build/rasterfall --help` 为准。

## 离屏与性能回归

```sh
build/rasterfall --model-views model.rmesh tmp/model-views
build/rasterfall --model-material-regression model.rmesh tmp/material-regression
build/rasterfall --model-performance model.rmesh 5 8
build/rasterfall --actor-performance 30 5 8
```

模型视图和材质回归走游戏内相同的材质与光栅路径。并行度、优化或渲染路径修改后，应比较确定性
帧缓冲哈希和各阶段统计；诊断消融模式不代表默认画质。生成物放在 `tmp/` 或 `build/`，不提交。

## 单位与边界

玩法世界使用 RFU，`512 RFU = 1 m`。PMX、GLB、VMD 的局部单位只在 presentation/导入边界
换算，不能直接进入碰撞、AI 或网络规则。坐标系或 bind pose 异常应从转换器输出一路定位到加载、
姿态求值和渲染，不要用角色专属末端偏移掩盖通用资产错误。

