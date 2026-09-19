# HG-2B 后续：GPU 帧诊断与 mixed 帧优化记录

> 文档更新：2026-09-19
> 源码核对基线：2026-09-19；核对 Windows CLI、mixed executor、graphics bridge 与 Intel Iris Xe 测量记录。实现与验证结果以当前 CLI 和下文证据为准。

Windows strict native 已支持 `--gpu-frame-capture <output.bmp> [--gpu-capture-frame <N>]` 导出最终 GPU 帧；普通 strict 帧保持零读回。冻结 mixed stream 在 Raster 分段复用 binning/上传，连续 Draw span 合并 graphics 提交，static prop 的视锥检查先于 world-light 查询。剩余跨段提交和 bridge 成本见下文。

原始任务草案已经实施，不再作为待办；HG-3A 尚未开始。
## 2026-09-19 实施与验证记录

后续帧末优化：显式预检后的冻结 mixed stream 在各 Raster 分段间复用 binning、命令、tile 与纹理上传；普通可变 stream 保持逐段验证与上传。每个连续 Draw span 将 Raster 导入、indexed Draw 和 Raster 导出录入同一 graphics command buffer，保留双向全屏 bridge，但只提交并等待一次。normal map static prop 将同一模型 AABB 视锥检查提前至 world-light 查询前。Intel `--mixed-gate`、`--executor-gate`、`--native-window` 与 Windows `--logic-test` 均通过；四 extent 的 140 帧 resize 脚本通过。

当前整帧仍由前段 Raster、graphics Draw span 和尾段 Raster 分别提交，且每个 Draw span 保留双向全屏 color/depth bridge。将三段真正合为一次队列提交或消除剩余 bridge，需要让 Raster 与 graphics 共用一帧的命令缓冲区/目标状态；现有单个 Raster command buffer 在分段间重录，不能只删除 fence 而继续复用它。这两项不计入本次已完成收益。

同机 Intel Iris Xe、1280×720、Fog、固定 near/0 和 mid/0 各运行 140 帧，取第 17–140 帧：near `render_ms` 中位数/P95 为 12.781/14.282，`present_wall_ms` 为 60.976/71.756，`whole_loop_ms` 为 75.206/85.860；mid 分别为 15.452/18.197、68.015/81.923、85.203/98.648。两组均为 140/140 GPU 帧、零回退；热帧分别有 57/67 Draw、1 Draw span、2 次 bridge、29,491,200 bridge bytes、1 次 graphics submit/等待，GPU 资源上传 0 bytes，普通读回和 CPU framebuffer copy 均为 0。near 第 3 帧最终 GPU BMP 已目视检查，含 WORLD、角色、VIEWMODEL、Fog 与 HUD。与上面的旧工作区测量相比，场景参数相同，但 exe、资源状态及运行顺序不同，墙钟差值只作观察，不作为严格 A/B 加速比。

- `--gpu-frame-capture` 从最终 GPU Post/overlay 缓冲区单次读回并写 BMP；`--gpu-capture-frame` 默认 30，诊断读回独立计数。输出路径须可写；不可写路径返回 3 且不生成空白文件。相对路径按运行时工作目录解析，自动化建议传绝对路径。固定场景的本地 actor 与 camera 同步初始位置，near/mid 在固定步长后不会退回出生点。
- 连续 Draw span 使用每资源固定 descriptor set，在一个 render pass 中按冻结顺序切换资源并发出 indexed draw；每个 span 一次 Raster 导入和一次导出。fixture 增加 3 Draw/2 span/4 bridge/6 稳态 graphics submit 断言；`-ExecutorGate`、`-MixedGate`、`-NativeGate`、`--logic-test` 均通过。
- Windows Intel Iris Xe、1280×720、Fog、固定 near/0 与 mid/0，各 140 帧中取第 17–140 帧：near `whole_loop_ms` 中位数/P95 为 81.073/96.285，`present_wall_ms` 为 66.963/80.511；mid 分别为 87.396/109.551 和 68.938/91.354。两组 140/140 GPU 帧、零 fallback；热帧分别为 57/67 Draw、1 Draw span、2 次 bridge、29,491,200 bridge bytes、3 次 graphics submit、GPU 上传 0 bytes。普通 strict 帧读回和 CPU framebuffer copy 均为 0。最终 package 的 140 帧四 extent resize 由 `hardware_graphics_resize.ps1 -NoRedirect` 通过。
- 最终 exe SHA-256：`27C4FD6A6F1C3873FC12AF0446A35B9998F2A3E81A65F3032A1CEA7928E0D119`。第 3 帧 near/mid Fog BMP 均为 1280×720、3,686,454 bytes，诊断读回各 3,686,400 bytes；SHA-256 分别为 `B8EB3659E865AE8227C109F3C154098F8DDD45686B22EFA50AC77142B8DE9D0E`、`40121883A00D86E054AC83652EC52A74E793B8C557478FB63C01D3932C0597AB`。无 Fog 图也已目视核对，四图均有天空、WORLD、VIEWMODEL 和 overlay，无整屏黑帧或明显遮挡翻转。
- 交接前的 720/701 ms 数据来自旧固定场景在首个逻辑步重置 camera 后的出生点画面（147 Draw）；本次近/中景分别为 57/67 Draw。因此这些墙钟数字不能当作同一场景的严格加速比。bridge 从按 Draw 往返降为按 span 往返、稳态提交为 3 次，是直接可比较的结构性改进。
