# 初步时间线

本时间线只列已经由本地仓库提交记录核对的边界事件。提交日期使用提交对象记录的 `+08:00`
时间；历史叙事补充前应继续核对作者日期、提交者日期、分支和文件内容。

| 日期 | 仓库 | 提交 | 事件 | 证据性质 |
|---|---|---|---|---|
| 2025-10-17 | Tinylibc | `fce216c` | 从 SC7 项目分离并尝试支持 x86_64 | Tinylibc 根提交说明 |
| 2026-06-30 | Tinylibc | `4670d9f` | 加入最小可行的自举编译器 Phase 1 | 提交内容与说明 |
| 2026-07-01 | Tinylibc | `9921705` | 提交说明记录 `tcc.c` 自编译通过 | 提交说明，能力仍待复现 |
| 2026-07-02 | Tinylibc | `2213ae5` | `tmake -T` 开始以 tcc/tas 替代 gcc | 提交内容与说明 |
| 2026-07-04 | ToyCCompiler / Toyc | `22ffcc8` | 从 Tinylibc 提取 tcc，建立独立仓库 | 两仓共享根提交；提交说明 |
| 2026-07-08 | ToyCCompiler / Toyc | `961fbf6` | 提交说明记录自举到 stage 10 | 提交说明，脚本与产物待复现 |
| 2026-07-08 | ToyCCompiler / Toyc | `8b2ea1f` | 发布“自举成功宣言” | 提交说明与 README 历史 |
| 2026-07-09 | ToyCCompiler / Toyc | `7adcf8f` | 提交说明记录种子、gcc-free 构建和 tas 闭环 | 提交内容与说明 |
| 2026-07-10 | ToyCCompiler / Toyc | `9948ea0` | tld 接管链接，形成工具链自举闭环 | 提交内容与说明 |
| 2026-07-11 | ToyCCompiler | `58ac389` | README 以 HTTP 301 形式宣告迁往 Toyc | 提交内容 |
| 2026-07-11 | Toyc | `643287b` | README 将项目描述为来自 ToyCCompiler 与 Tinylibc 的 “two parents” | 提交内容 |
| 2026-07-11 | Toyc | `a26e7a6` | 编译器源码由 `app/` 移到 `compiler/`，为 Tinylibc 整合留出目录 | 提交内容与说明 |
| 2026-07-23 | Toyc | `5bffca4` | 工具链由 tcc/tas/tld 等统一改名为 toyc/toyas/toyld | 提交内容 |
| 2026-07-24 | Toyc | `af6bc30` | 引入 Tinylibc 的完整库与应用树及 GCC 构建规则 | 提交内容与说明 |

## 尚未定论的边界

- Tinylibc 的“手写时期”应从 SC7 前史算起，还是从独立仓库根提交算起。
- ToyCCompiler 根提交对应 Tinylibc 的准确源提交；目前只能确定提取发生在 2026-07-04，且
  Tinylibc 在 2026-07-03 已有接近的编译器树。
- Toyc 在 2026-07-24 引入的 Tinylibc 文件对应上游哪个快照，之后两仓是否发生双向同步。
- Rasterfall 应作为 Toyc 第四时期，还是作为 Toyc 时期内部的第二条项目主线。

