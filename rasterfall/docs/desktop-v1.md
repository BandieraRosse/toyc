# RF Desktop V1

> 文档更新：2026-09-12
> 源码核对基线：`rasterfall_gui` / `rasterfall_app` Desktop V1 window manager

Desktop 是 Rasterfall Game presentation 层的屏幕空间工作站桌面。`rf_gui_context` 是
Desktop/window-manager 状态所有者：它维护 application window 的 running、minimized、
maximized、focused、z-order、当前 rect 和 restore rect；统一处理图标、窗口摆放、焦点、
拖动、最小化、最大化/恢复、关闭及底部 running strip。

V1 只有两个固定、单实例 application：`PERSONNEL` 与 `TERMINAL`。打开已运行的 application
只会恢复并置前已有窗口；关闭才结束 instance。PERSONNEL 通过
`rf_application_projection` 读取真实 session roster/actor 数据，不复制 gameplay 真值。

TERMINAL 是独立的 application identity 和 display-only client area；它不等同于 RF Core，
也不等同于 developer console。PTY、shell、ANSI/VT100、exec/backend 留给后续阶段。

Desktop 不修改 RF Core、Game/Core ownership 或 gameplay actor/session 状态。V1 非目标包括
真正 terminal backend、多窗口应用、多桌面、Start Menu、通知中心，以及启动/关机/Core
reconstruction UI。
