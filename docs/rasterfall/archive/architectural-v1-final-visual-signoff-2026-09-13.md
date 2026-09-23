# Architectural V1 最终视觉签收现场

> 状态：历史
> 归档原因：阶段记录，当前设计与执行顺序已转入维护入口
> 当前入口：[Rasterfall 维护者入口](../README.md)

日期：2026-09-13。Visual design = **VISUALLY FROZEN**；Engineering checkpoint = **pending Sol**。
当前规范以 [Architectural Environment V1](../reference/architectural-environment-v1.md) 为准。

## 恢复与结果

接手时为 35 个未提交条目：13 个 tracked 修改、10 份 RMESH、10 份 manifest、设计文档和 round 脚本。
未 reset、删除或重写前任工作。实际现场比交接文字更新：最终导入之后已有对应截图，欠缺的是明确签收。
本轮重新构建、重新 capture 并审阅；没有修改 geometry、资产、placement 或 renderer。

- service_panel：俯斜单件视图及 family 正面分区完整，三道服务口可读，无明显黑色碎面或叠层竞争；原型中保持背景服务语义。最终导入日志为 156 triangles。
- floor_hatch：俯斜视图大面和边框完整，取消小把手后更简洁；原型可见处保留地面尺度，不依赖微细节，无明显破面或深度竞争。最终导入日志为 60 triangles。
- family / alley / hall：未见明显 regression；侧边服务带、入口及中央开放空间保留，角色和感染体轮廓可辨。inside / far / reverse 补充确认墙面、门洞与背向结构完整。
- scoped culling：所审阅建筑 runtime 视角未见必要面丢失；此结论不替代状态恢复、winding 和其他模型回归的工程审查。
- 原十件 CANONICAL 保持，两材质、零纹理；未新增或退役资产，未进入正式 Campaign integration。

固定视角截图不能证明移动镜头下完全无闪烁；未将 cold capture 的 raster_flush_us 解释为 Campaign FPS。

## 可复核证据

本轮命令均成功退出：

```sh
make app-rasterfall
build/rasterfall --help
python3 tools/architecture_round.py --capture --deterministic --output tmp/architecture-v1-final-review
```

构建日志：`/tmp/architecture-final-build.log`。
新目录保存 `prototypes.png`、`details.png`、各单件四视图、原始 BMP、运行日志与 `runtime-fingerprints.json`。
重点逐张审阅 `rf_arch_service_panel.png`、`rf_arch_floor_hatch.png`、family 及两个原型各四个视角。
九个 family/原型场景的重复 BMP 逐字节一致；十九张 runtime 单件/原型 BMP 与
`tmp/architecture-v1/captures/` 对应旧文件逐字节一致。十件 runtime fingerprints 与旧记录一致。
另确认已有 panel/hatch 的 `source` 与 `source-repeat` GLB 字节一致；本轮未重新生成或导入资产。
所有截图、日志和 fingerprints 为本地产物，不提交 tmp/build/private-assets。

## Sol 接力

第一件事：保留当前工作区，做 full diff review，优先检查 scoped backface-culling 的 state save/restore、
closed-solid assumption、winding correctness 及其他模型回归风险；以本轮 runtime fingerprints 和截图为视觉基线。

工程 checkpoint 尚需 Sol 完成：Linux 最终 build/test matrix、Windows build/package、asset pipeline full regression、
最终 logic test、`git diff --check`、docs/code consistency review，以及 commit/checkpoint。本轮没有执行这些完整工程门禁，
也没有提交；单次 Linux 构建成功不等于完整矩阵通过。

后续正式 Campaign integration、map wall/floor surface、collision policy、doorway/Tank/Charger 碰撞与通行、
navigation、Campaign stress profiling 均仍归 Sol。先完成工程 checkpoint，再开展正式集成。
