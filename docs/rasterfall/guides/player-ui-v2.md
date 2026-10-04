# 玩家界面 V2 使用与验收

> 状态：当前开发版本
> 事实入口：`rf_input_bindings.c`、终端 `help` 与各命令组

从 Windows native 启动：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 run --skip-boot --renderer gpu-scene
```

默认玩家界面。默认 F3 循环玩家/终端/实验，反引号临时打开或关闭玩家终端，M 切 FPS/RTS，
G 按住展开地图。通讯默认 C 打开回答指针，方向键选择，Z 回答，H 收起/恢复；
数字武器槽仍保留。提示由当前物理动作绑定生成，运行中改绑后不依赖这些默认字母。

网格编织机附近按交互键打开窗口，Tab/上下键移动可见焦点，确认键执行；左右键旋转真实模型。
选择不制造，开始按钮明确执行。关闭或收起后制造继续；领取会说明被替换武器，确认时再次校验。
工程详情保留精确几何和消耗。实验模式保留原工程面板。

## GUI 与终端映射

| 图形操作 | 结构化接口 / 终端 | 只读查询 | 权限与校验 | 验证 |
| --- | --- | --- | --- | --- |
| 模式/缩放/透明度 | `RF_PLAYER_SET_*`；`ui mode player|terminal|experiment`、`ui scale N`、`ui opacity N` | `ui` | USER；范围校验、独立保存 | 重启偏好、布局检查 |
| 蓝图选择 | `RF_PLAYER_WEAVER_SELECT`；`weaver select pistol` 等真实 ID | `weaver list`、`weaver` | USER；真实设备、距离、代际 | GUI/终端交替 |
| 制造 | `RF_PLAYER_WEAVER_START`；`weaver start` | `weaver` | USER；资源、尺寸、忙碌、serial | 重复提交拒绝 |
| 电力/CPU/X1 | `RF_PLAYER_WEAVER_POWER/CPU/X1`；`weaver power|cpu|x1 0|1` | `weaver` | USER；提交时校验距离，不补充能源 | 断供/恢复保持进度 |
| 领取 | `RF_PLAYER_WEAVER_COLLECT`；`weaver collect [confirm]` | `weaver` | USER；成品身份、旧武器确认、距离 | 无重复领取收益 |
| RTS 选择/移动/停止 | `RF_PLAYER_RTS_*`；`rts select N`、`rts move X Z`、`rts stop` | `rts` | USER；离线、目标地面、有效选择 | 视角切换保持目标 |
| 跟随/视角 | `RF_PLAYER_RTS_FOLLOW/VIEW`；`rts follow 1`、`rts view fps|rts` | `rts` | 只变展示；RTS 离线 | 不清空会话/制造 |
| 通讯回答 | `RF_PLAYER_DIALOG_ANSWER`；`comms answer N SESSION NODE_REV` | `comms` | USER；双版本校验，N=0确认结语 | 旧回答拒绝 |
| 收起/恢复/结束 | `RF_PLAYER_DIALOG_COLLAPSE/CLOSE`；`comms hide|resume|close` | `comms`、`comms history` | USER；保留/释放按会话规则 | 停止视频、释放控制 |
| 任务/通知 | 共享只读状态 | `task`、`notices` | USER；不泄漏未知敌人 | 真实事件驱动 |
| 渲染开关 | `RF_DEVICE_RENDER_SET`；`render set outline 0|1` 等能力 ID | `render status` | USER；按当前后端支持状态校验 | 显式设置幂等 |
| 指挥桌选择/部署 | `RF_DEVICE_TABLE_SELECT/DEPLOY`；`table select|deploy outpost|campaign|whu` | `table maps`、`devices status` | 部署复核桌旁距离/高度、离线与世界代际 | 队列单次消费 |
| 重播/重置 | `RF_PLAYER_STORY_REPLAY/RESET`；`comms replay 101|102`、`comms reset 0|101|102` | `comms` | ADMIN；显式实验模式 | 两段与全部回答分支 |

## 验收入口

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File windows/NativeCodex.ps1 test
powershell -NoProfile -ExecutionPolicy Bypass -File tools/gpu_player_ui.ps1 -Stage All
```

脚本发送真实 Windows 按键、等待结构化只读审计并请求原生截图；不直接赋值制造阶段或对话节点。
每次新建证据目录，等待进程真实退出并保存 stdout/stderr/操作记录。
`RF_UI_SAVE_PATH` / `RF_STORY_SAVE_PATH` 可把测试存档隔离到临时文件；`RF_UI_NO_SAVE` 禁止持久化。
固定场景诊断默认关闭剧情，显式 `RF_UI_STORY=1` 才开启；正常游玩自动检测真实区域。

视频属于 GPU Scene 功能，CPU 界面显示明确不可用状态并保留文本会话。通讯仅一个可见镜头，
设备真实预览优先占用辅助画面；关闭设备后恢复通讯采样。新增性能/视觉结果进入本轮现场记录，
不得把独立逻辑通过写成真实窗口验收通过。
