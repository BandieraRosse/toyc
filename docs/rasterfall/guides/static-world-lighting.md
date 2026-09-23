# Static World Lighting V2 验证

> 状态：当前操作指南

所有权、固定参数、采样与诊断例外见[世界光照架构](../architecture/static-world-lighting.md)。相关修改至少验证：

```sh
make app-rasterfall
build/rasterfall --logic-test
make test-map-parser test-map-runtime test-map-components
make win-rasterfall
```

视觉检查使用正常 world render 的 `--environment-capture`、Character world capture、enemy capture 和 lighting-props fixture。world-light logic 应覆盖 bilinear、bounds clamp、三维遮挡、高位梁、surface 高度、scene×form/material policy 以及 V2-default/V1-diagnostic source ownership。

离屏截图与 MinGW 构建不能替代 Windows native present、真实窗口移动中的 flicker/grid snapping 或实机 GPU 结论；这些验收遵循 [Windows Native 指南](windows-native.md)。历史冻结证据见[阶段记录](../archive/static-world-lighting-v2-freeze.md)。
