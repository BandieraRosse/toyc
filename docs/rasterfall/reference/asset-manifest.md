# 资产导入 manifest schema 1

> 状态：当前
> 所有者：离线资产导入 manifest

`tools/assets/manifest.example.json` 是 schema 1 示例。必填字段只有 `schema`、`id`、`type`、`source`；
`source` 相对 manifest 定位。可选 `lods` 保存该资产的简化策略，static prop 可记录自身
`dimensions_m`，weapon 可记录自身 `attachments`。输出根、输出路径、RMESH 的 232 units/m 等全局
可推导规则不写进 manifest，未知字段会被拒绝。

`type` 允许 `static_prop`、`character`、`weapon`、`rigid_attachment`。刚性附件必须提供
`attachment_space`，其 `origin`、`orientation`、`units` 分别为 `mount_origin`、
`canonical_character`、`meters`；其他类型不得写此字段。

manifest 仅供离线导入、完整性验证，以及后续生成/校验 `rasterfall_prop` 或 asset registry；游戏
runtime 不解析 JSON。当前 importer 不生成 runtime registry，避免在契约稳定前制造第二套主数据。

asset ID 只允许小写 ASCII 字母开头以及小写字母、数字、下划线，且发布后不复用。输出名称由
asset ID 推导；纹理表索引决定三位十进制文件名，LOD 使用正整数层级。
