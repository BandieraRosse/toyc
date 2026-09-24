# Scene 不变 bind 上传与密集组件压力场景

> 状态：历史测量现场；不代表正式 FPS 或生命周期签收
> 日期：2026-09-24
> 当前工作：[活动计划](../plans/gpu-scene-renderer.md)

正式模块化队员的模型 bind 数据随资产 generation 不变，旧路径仍每帧为 43 个 mesh 上传完整 bind。
新路径在同一资源 handle/generation、顶点数和 bind-normal policy 下保留 GPU bind，只上传当前
palette 并执行蒙皮；冷创建、增长和身份变化上传完整 bind。取消批次或准备失败使缓存失效。
`RF_GPU_SCENE_LEGACY_BIND_UPLOAD=1` 在同一可执行文件中恢复完整上传。

压力地图从 Campaign 原始 V1 地图生成：保留 134 个 object，其中 113 个组件的种类与数量不变，
只把组件排入 near 镜头周围。60 敌人档另含六个 Tank、六个 Charger；各轮实际组件 draw 为 128，
普通 near 镜头此前约为 57。报告逐帧校验来源、敌人和 draw 序列。

Windows native、RTX 3050 Laptop、1280×720、固定 tick，同包交替五轮，每轮 32 帧，
剔除前 8 帧。下表为五个单轮统计值的中位数；含逐帧日志，是归因样本。
可执行文件 SHA-256：`FA2BFE74E7D2DFBD7572EE02EE7081E9137EF5733F718E069067457257588ADB`。

| 敌人 | 循环 median 旧→新 | P95 旧→新 | 角色准备 median 旧→新 | 上传字节旧→新 |
| --- | ---: | ---: | ---: | ---: |
| 0 | 16.448→16.567 ms | 17.071→17.183 ms | 3.194→2.089 ms | 10,788,996→4,598,724 |
| 30 | 21.928→20.992 ms | 26.365→23.386 ms | 3.561→2.353 ms | 14,929,020→8,738,748 |
| 60，含 Tank/Charger | 29.399→27.940 ms | 31.778→32.774 ms | 3.518→2.398 ms | 19,066,524→12,876,252 |

60 敌人档循环中位轮改善约 1.46 ms，上传减少约 6.19 MB。P95 未改善，且个别轮有敌人准备和
提交共同变慢的系统性波动；不能把角色准备收益直接等同于稳定帧率收益。
原始证据位于 `tmp/scene-bind-heavy-dense-ab5/report.json` 和每轮日志。

GPU graphics 门禁验证了 palette-only 更新的设备顶点与完整上传一致，且只计入 palette 字节。
密集 near 60 的两侧第 4 帧 Scene PPM SHA-256 均为
`4CDFD22D43AF53A7CEBF7AA7496D173C2A87E06DD9857D03309589EB185299D3`；两侧 validation/sync
均通过，正常帧仍为零旧 producer、bridge 和 readback。对应 capture 为普通 near 60 的密集地图，
Tank/Charger 镜头另有原生运行与五轮 A/B 证据。批次取消后的缓存失效修复晚于测量包。
最终包 SHA-256 为 `12330777F8A8AD10F781D079AA86DBE50A251853C03D0B63F4B34E19B42BD2F6`。
最终包 `--logic-test` 退出 0，Tank/Charger 加密集组件镜头的两侧 validation/sync 均通过；
第 4 帧 Scene PPM SHA-256 同为
`7C21476657E321E49FAC1CF6D700A9DA8707D78528EBF80E708FD3F2F62BCBF1`。
最终包修复仅涉及失败/取消时使 bind 缓存失效，不改变正常热帧输入；测量数字对应上面的测量包。
最终包 `tools/gpu_scene_play.ps1 -Stage Faults` 的五类 present 故障注入也在 validation/sync 下通过，
记录于 `tmp/scene-bind-final-faults/`。
低扰动正式五轮 FPS、长时 soak 与完整生命周期尚未执行。
