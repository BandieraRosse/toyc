# RF Desktop V1

> 状态：历史
> 归档原因：Desktop normal runtime 已冻结，保留原型设计供追溯
> 当前入口：[Application Runtime](../../architecture/application-runtime.md)

> 文档更新：2026-09-18
> 源码核对基线：Desktop V1 normal runtime 已冻结；实现、逻辑测试和 `desktop-v1` 视觉 fixture 保留隔离，正常启动不初始化 GUI/app manager。

> 冻结边界：F12、反引号和前哨站 station terminal 不再打开 Desktop/Console，只显示 `TEMPORARILY UNAVAILABLE` HUD 提示。以下内容记录冻结前的 V1 设计，不属于当前 normal GPU frame 语义。

Desktop 是 Rasterfall Game presentation 层的屏幕空间工作站桌面。`rf_gui_context` 是
Desktop/window-manager 状态所有者：它维护 application window 的 running、minimized、
maximized、focused、z-order、当前 rect 和 restore rect；统一处理图标、窗口摆放、焦点、
拖动、最小化、最大化/恢复、关闭及底部 running strip。

V1 只有两个固定、单实例 application：`PERSONNEL` 与 `TERMINAL`。打开已运行的 application
只会恢复并置前已有窗口；关闭才结束 instance。PERSONNEL 通过
`rf_application_projection` 读取真实 session roster/actor 数据，不复制 gameplay 真值。

F12 与前哨站终端使用同一套双应用桌面；前哨站进入时默认打开 TERMINAL，PERSONNEL 仍可从
图标打开。没有专用角色身份的地图 actor 显示为 `NULL Operative`，并标注
`Unassigned` 与 `UNASSIGNED / MAP ACTOR`。

TERMINAL 是独立的 application identity 和 display-only client area；它不等同于 RF Core，
也不等同于 developer console。PTY、shell、ANSI/VT100、exec/backend 留给后续阶段。

Desktop 不修改 RF Core、Game/Core ownership 或 gameplay actor/session 状态。V1 非目标包括
真正 terminal backend、多窗口应用、多桌面、Start Menu、通知中心，以及启动/关机/Core
reconstruction UI。
