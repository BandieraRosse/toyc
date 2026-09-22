# 应用与离线工具维护入口

`app/` 保存宿主应用源码：`app/linux/` 和 `app/windows/` 是平台入口，`app/portable/` 是双平台共享实现。`tools/` 保存资产转换、检查、布局导出和验收编排工具，其中许多服务于 Rasterfall。

- 应用构建目标和仓库布局：[../../README.md](../../README.md)
- 现有工具清单：[../../tools/README.md](../../tools/README.md)
- Rasterfall 资产工具链：[../rasterfall/asset-pipeline.md](../rasterfall/asset-pipeline.md)
- Rasterfall 地图格式与工具：[../rasterfall/map-format.md](../rasterfall/map-format.md)

修改 portable 应用时同时检查 Linux `app-*`、Windows `win-app-*` 和各自平台库；修改 Rasterfall 工具时以其 reference 契约和 guide 工作流为准。

