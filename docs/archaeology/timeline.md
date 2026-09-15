# 初步时间线

本时间线只列已经由本地仓库提交记录核对的边界事件。提交日期使用提交对象记录的 `+08:00`
时间；历史叙事补充前应继续核对作者日期、提交者日期、分支和文件内容。

| 日期 | 仓库 | 提交 | 事件 | 证据性质 |
|---|---|---|---|---|
| 2025-03-23 | SC7 | `ec37618` | 团队竞赛内核首次提交，说明已支持双架构 UART 输出 | SC7 根提交说明 |
| 2025-06-30 | SC7 | `bcfab95` | `preliminary_contest` 初赛快照终点 | 提交对象；由合并第二父恢复 |
| 2025-08-17 | SC7 | `b927bc1` | `pre_final1` 决赛阶段一快照终点 | 提交对象；由合并第二父恢复 |
| 2025-08-20 | SC7 | `3c6e528` | 决赛现场赛重新适配物理开发板的实际起点 | 提交对象；现场文档误写为 `28aab57` |
| 2025-08-20 | SC7 | `524e2ef` | 上板线清零 BSS、补显式初始化并接入文件系统 | 提交内容与说明 |
| 2025-08-29 | SC7 | `1a668a3` | 当前主分支最后提交 | 提交对象；不代表项目所有分支终点 |
| 2025-09-24 | SC7 | `e2eef7f` | 在 `origin/tlibc` 支线开始独立发展 Tinylibc | 作者提交记录；支线后续仍有提交 |
| 2025-10-17 | SC7 | `d2363da` | `tlibc` 末端；独立仓库根树的精确来源快照 | 跨仓库逐路径 blob 对照 |
| 2025-10-17 | Tinylibc | `fce216c` | 从 SC7 分离；只改三个 Markdown，源码仍为 RISC-V | 根提交、树对照 |
| 2025-10-17 | Tinylibc | `7586050` | 首次加入 x86_64 适配并声明在作者 PC 可运行 | 提交内容与说明 |
| 2025-11-12 | Tinylibc | `34fcd9e` | 重组为 `app/include/lib/arch`，摆脱 SC7 遗留目录习惯 | 提交内容；动机来自作者回忆 |
| 2025-11-16 | Tinylibc | `6c396aa` | top 加入 CPU 占用与排序；首次开发中断前的主线末端 | 提交对象；中断解释来自作者回忆 |
| 2026-03-05 | Tinylibc | `4778563` | 以最小 pthread_create 和 clone 重新启动开发 | 提交内容与说明 |
| 2026-03-27 | Tinylibc | `3dae4be` | 加入线程栈与内存的异步回收实验 | 提交内容、同期日志 |
| 2026-05-29 | Tinylibc | `45adc6b` | tmake 已能调用外部工具编译并链接所有程序 | 提交内容与说明 |
| 2026-06-22 | Tinylibc | `c985e5b` | 开始以 Claude Code 客户端接入 DeepSeek API 的密集 agent 重构 | 提交内容、元数据与作者回忆 |
| 2026-06-30 | Tinylibc | `4670d9f` | 加入最小可行的自举编译器 Phase 1 | 提交内容与说明 |
| 2026-07-01 | Tinylibc | `9921705` | 提交说明记录 `tcc.c` 自编译通过 | 提交说明，能力仍待复现 |
| 2026-07-02 | Tinylibc | `4437887` | 合并 compiler-test-suite 工作线，测试开始成为自举推进单位 | 提交 DAG、内容与作者回忆 |
| 2026-07-02 | Tinylibc | `2213ae5` | `tmake -T` 开始以 tcc/tas 替代 gcc | 提交内容与说明 |
| 2026-07-04 | ToyCCompiler / Toyc | `22ffcc8` | 以 Tinylibc `87d61e0` 编译器子树为基底选择性提取，建立独立仓库 | 跨仓库 blob 与逐文件 diff |
| 2026-07-07 | ToyCCompiler / Toyc | `0252321` | 首个可运行 stage-2 脚本：stage 1 编译自身，GCC 汇编、GNU ld 链接 | 脚本实际调用链 |
| 2026-07-08 | ToyCCompiler / Toyc | `961fbf6` | 提交说明记录自举到 stage 10 | 提交说明，脚本与产物待复现 |
| 2026-07-08 | ToyCCompiler / Toyc | `8b2ea1f` | 发布“自举成功宣言” | 提交说明与 README 历史 |
| 2026-07-09 | ToyCCompiler / Toyc | `7adcf8f` | 提交说明记录种子、gcc-free 构建和 tas 闭环 | 提交内容与说明 |
| 2026-07-10 | ToyCCompiler / Toyc | `b3c5145` | tld 改由 tcc 编译并完成两代自链接字节一致 | Makefile、源码与说明 |
| 2026-07-10 | ToyCCompiler / Toyc | `9948ea0` | tld 接管链接，形成工具链自举闭环 | 提交内容与说明 |
| 2026-07-10 | ToyCCompiler / Toyc | `46119c1` | 更新三个收敛版种子，说明记录 stage 9/10 一致 | 二进制 blob、说明与隔离复查 |
| 2026-07-11 | ToyCCompiler | `58ac389` | README 以 HTTP 301 形式宣告迁往 Toyc | 提交内容 |
| 2026-07-11 | Toyc | `643287b` | README 将项目描述为来自 ToyCCompiler 与 Tinylibc 的 “two parents” | 提交内容 |
| 2026-07-11 | Toyc | `a26e7a6` | 编译器源码由 `app/` 移到 `compiler/`，为 Tinylibc 整合留出目录 | 提交内容与说明 |
| 2026-07-23 | Toyc | `5bffca4` | 工具链由 tcc/tas/tld 等统一改名为 toyc/toyas/toyld | 提交内容 |
| 2026-07-24 | Toyc | `af6bc30` | 引入 Tinylibc 的完整库与应用树及 GCC 构建规则 | 提交内容与说明 |

## 尚未定论的边界

- Toyc 在 2026-07-24 引入的 Tinylibc 文件对应上游哪个快照，之后两仓是否发生双向同步。
- Rasterfall 应作为 Toyc 第四时期，还是作为 Toyc 时期内部的第二条项目主线。
