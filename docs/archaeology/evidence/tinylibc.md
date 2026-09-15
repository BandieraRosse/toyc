# Tinylibc 提交级证据表

## 使用说明

本表为 [Tinylibc 时期](../periods/tinylibc.md) 提供可复核依据，不重复连续叙事。提交号属于公开仓库
`WHU-SC7/Tinylibc`；调查缓存为 `../Tinylibc`，当前可见历史截至 `a566206`。

“代码记录”只确认改动进入 Git；提交说明、README 和 `tlibc_commit_log.md` 是同期陈述；
`C:\Users\15259\Desktop\draft` 中的文章是作者提供的同期外部材料；2026-09-15 的回答是事后
回忆。功能通过、贡献比例和因果关系不能由提交数或署名自动推出。

## 阶段锚点

| 阶段 | 代表对象 | 日期 | 可确认内容 | 限制 |
|---|---|---|---|---|
| SC7 支线起点 | SC7 `e2eef7f` | 2025-09-24 | 说明为 TinyLibc 第一次提交 | 仍位于团队仓库和内核运行环境 |
| 分离快照 | SC7 `d2363da` / `fce216c` | 2025-10-17 | 29 个路径对应，26 个 blob 相同，只改三个 Markdown | 无 Git 祖先关系 |
| x86_64 可运行 | `7586050` | 2025-10-17 | 加入 syscall、输入和 ABI 适配，说明可在 PC 运行 | 未复现当时环境 |
| 独立项目布局 | `34fcd9e` | 2025-11-12 | 形成 `app/include/lib/arch` 分层 | 仍链接成单个大程序 |
| 手写阶段末端 | `6c396aa` | 2025-11-16 | top 加入 CPU 显示和排序 | 空档动机来自作者回忆 |
| pthread 重启 | `4778563` | 2026-03-05 | 最小 pthread_create、clone | 不等于完整 pthread 语义 |
| 程序/库分离 | `d82fe5d` | 2026-03-14 | 单独编译程序与静态库 | 与 11 月目录重组不同 |
| 异步回收 | `3dae4be` | 2026-03-27 | 后台回收线程栈和项目 malloc 内存 | 锁、join 和兼容性未收敛 |
| 网络与 Shell | `2931f99`—`0747dfd` | 2026-05-02—06-01 | HTTP、server、文件传输、补全、PATH | 验证范围主要来自同期记录 |
| agent 转型 | `c985e5b` 起 | 2026-06-22 | Claude Code 接入 DeepSeek API 后密集重构，提交普遍署名 Claude | 署名不能证明使用 Claude 模型 |
| 编译器起点 | `4670d9f` | 2026-06-30 | 最小编译器 Phase 1 | 框架来源仍需会话材料 |
| 测试策略转折 | `2e03788`、`4437887`、`ff396cb` | 2026-07-02 | 测试支线合并、阶段式 runner | 决策来源含作者回忆 |
| 独立前锚点 | `87d61e0` | 2026-07-04 | 修复对齐和字符串初始化 | 未证明恰为新仓库源快照 |

## 课程交付与终端程序

根提交 README 把项目定位为学习用途，`项目计划.md` 把 x86_64 和网络列为第二阶段。`ae1d107`
加入终端游戏；`cdf60b3` 至 `95aa5f2` 推进简化版 vim；`79fd5fd` 至 `6c396aa` 让 top 从进程列表
推进到内存、时间和 CPU 排序。提交日志同时记录 vim 中文显示限制和 top 段错误等边界。

作者说明它也是 2025 年下半年软件工程小组作业，主要由本人推进；教师要求成果不能只实现库接口，
促使终端应用成为交付形式。课程材料尚未取得，所以不记录成员、课程名或评分，也不推断其他成员缺席。

## `34fcd9e` 的准确边界

该提交把 `app.c` 改为 `app/shell.c`，`core.c/test.c` 移入 `lib/`，`internal/` 与 `external/`
下的头文件移入 `include/`。Makefile 最终仍链接到 `build/tlibc_x64`。对象支持“摆脱 SC7 目录
习惯、建立 Tinylibc 独立布局”的作者解释；各命令独立链接首次明确见 `d82fe5d`。

## pthread、mempool 与论文

| 对象 | 同期记录 | 限制或反例 |
|---|---|---|
| `4778563`、`a324f05` | 最小 create 与 join | 初期 join 仍简单 |
| `a4fc67a` | mempool 按线程管理内存 | 同时仍在修 clone 参数 |
| `973b761`、`6da8668` | 预分配并记录线程栈 | 只覆盖项目接口管理的资源 |
| `3dae4be` | 工作线程异步回收资源 | 日志留下锁问题，join 暂不清理 |
| `2907eb3` README | 宣称创新性修改，提供 glibc 对比入口 | 不是优势已复现的证据 |
| `26b003e` | 修复旧程序退出兼容，memtest 原因不明 | 显示全局 mempool 的副作用 |
| 支线 `3d5b976` | 完整 pthread 需要大改，分支不合并 | 同期失败记录 |
| `9274c1c`、`5d98067` | agent 阶段重写 pthread，默认关闭自动回收 | 后续完善不抹去早期实验 |

`app/paper/` 保存 pthread、mempool、memtest 和 glibc 对比入口。作者说明毕业论文研究线程资源
异步回收机制，并认为是否优于 glibc 有待商榷、需要继续优化。论文原文和原始输出尚未纳入。

## 网络、Shell 与构建工具

| 主题 | 提交 | 对象可确认内容 |
|---|---|---|
| HTTP | `2931f99` | HTTP 程序；日志写明 `getaddrinfo` 暂缓 |
| server/client | `5a58201`、`e697a41` | 本机收发与多线程服务端 |
| 文件传输 | `7281774`、`f83544e` | 交互下载与分片发送 |
| Shell 补全 | `8a185ed`、`291bef7` | 补全、PATH 配置与 fd 修复 |
| PATH 执行 | `0747dfd` | 去掉补全状态 workaround，执行时独立搜索 |
| tmake | `33afdb3`、`45adc6b`、`8924b42` | 编译、链接和安装各程序；仍调用外部工具 |

作者回忆这些网络程序主要由自己手写。2026 年 3 月前还尝试手写音频但失败且未提交；7 月
`78ba482`、`067efc3` 的 ALSA/PulseAudio 成功属于 agent 阶段另一轮实现。

## 同期文章与旧对象号

仓库外 `draft` 的文件系统时间集中在 2026-07-11，不能直接作为写作或发布日期。

| 文件 | 内容与边界 |
|---|---|
| `000_draft_shell_v0.md`、v1.0 | 解释补全与终端；声明主体代码手写，`cal_absolute_path` 来自 SC7 队友 |
| `001_what_is_rm_doing.md` | 用递归删除说明 syscall 组合；作者写作，AI 只建议润色 |
| `002_use_tmake_toreplace_make.md` | 解释 tmake，并在实现前提出未来简化 C 编译器 |
| `003_CLAUDE_shell_path_search.md` | 保存原始 prompt；从本篇起文章由 Claude Code 客户端接入的 DeepSeek 模型生成，对应代码仍主要手写 |

`000` 引用的 `820b680...`、`003` 引用的 `dfc7a9f...` 当前均不可解析；日期和主题分别与现历史
`291bef7`、`0747dfd` 匹配。这只支持可能存在历史改写后的映射，找回旧对象前不能宣称 blob 相同。

## AI 工作方式与提交日志

手写时期主要使用 DeepSeek 网页端讲解 musl、syscall、终端 flag 和调试输出。同期日志在
2025-11-07 记录 `termios` 字段类型曾被 AI 误导，最终改查 Linux UAPI，说明网页回答并非权威入口。
作者另将 GLM-5.2 首次使用回忆为 2026 年 7 月的“ToyCCompiler 迁移幽默声明”会话，将 ChatGPT
使用放在 7 月底及 8 月订阅；当前没有会话或 Git 文本交叉验证。

6 月 22 日后，大多数代表性提交显式署名 Claude，包括 tmake 并行构建 `423fbed`、测试框架
`c424382`、远程 Shell `ef82e39`、HTTP 服务 `3941b9d`、嗅探器 `f5162d3`、pthread 重写
`9274c1c`、库拆分 `ef11c99`。作者逐渐改为写 prompt、定方向、判断异常和让 agent 总结代码，
偶尔亲自测试；具体目标仍常来自作者兴趣或旧计划。

作者确认 Claude Code 只是客户端，模型请求全部接入 DeepSeek API，没有同时使用 Claude 模型。
因此 trailer 记录的是客户端工作流产生的身份文本，不能作为模型供应方证据。作者推测客户端内嵌
提示词使 DeepSeek 自我识别为 Claude，但配置、system prompt 和会话尚未取得，该解释仍属推测。

`tlibc_commit_log.md` 延续 SC7 的线性日志习惯。6 月 22—23 日连续尝试用多种 Git hook 自动维护
`CLAUDE_COMMITS.md`，`b082c74` 放弃自动写入，`d6997df` 删除文件。作者解释，线性文件便于人读，
agent 则可直接查询 Git；继续维护汇总反而低效。

## 编译器与测试策略

`002_use_tmake_toreplace_make.md` 已在 5 月留下未来编译器设想。作者说明自己给出“依赖全部位于
Tinylibc、不得使用标准库”的限制，初始框架由 Claude Code 客户端接入的 DeepSeek API 模型设计。

`4670d9f` 至 `a85c789` 一天内推进 Phase 1—4；`e457e6b` 说明编译 22/27 个库文件；`d996c17`
和 `9921705` 分别声明 9/9 与 `tcc.c` 自编译。`2e03788` 加入测试套件，`4437887` 合并
`compiler-test-suite`，`fddf85c`、`ff396cb` 形成阶段式 runner，随后出现密集语义和代码生成修复。

作者回忆，直接要求 agent 自举会反复陷入难定位的段错误，因此由他提出先覆盖自举所需常见与边缘
情况，再逐项通过。提交顺序与策略落地一致，但不能单独证明思想来源。

## 来源与许可待核

- 作者确认 `7586050:arch/x86_64/syscall.h.in` 的系统调用号直接借用 musl。
- `syscall_arch.h` 保存 `__syscall0` 至 `__syscall6`；作者称理解后加入，尚未固定 musl 对象逐行比较。
- pthread/clone 大量参考 musl 和 glibc，当前没有文件级来源表。
- 同期文章明确称 `cal_absolute_path` 来自 SC7 队友，仍需定位 SC7 blob 与迁移提交。
- 早期 printf 由网页 AI 辅助形成，相关会话和原型未保存。

在上游快照固定前，不把调用号文件的直接借用扩张成整个 syscall 层来自 musl，也不把 Linux ABI
约束下的相似代码自动视为复制。

## 复查入口与待取材料

```sh
git -C ../Tinylibc show --stat --summary 34fcd9e
git -C ../Tinylibc diff 34fcd9e^ 34fcd9e -- README.md Makefile
git -C ../Tinylibc log --all --follow -- app/paper/pthread.c
git -C ../Tinylibc show 6c396aa:tlibc_commit_log.md
git -C ../Tinylibc log --reverse --since=2026-06-22 --until=2026-06-30 \
  --format='%H %aI %s%n%(trailers:key=Co-Authored-By,valueonly)'
git -C ../Tinylibc show --stat 2e03788 4437887 ff396cb
```

仍需取得：课程材料；论文原文和输出；当时的 musl 快照；未提交音频实验（若仍存在）；agent 会话；
文章旧 Git 对象；以及 Tinylibc 编译器树到 ToyCCompiler 根提交的逐文件对照。
