# Toyc 与 Tinylibc 维护入口

Toyc 是面向 Linux x86_64 的自托管 C 工具链，`lib/` 与 `include/` 同时提供 Tinylibc 和跨平台公共设施。Rasterfall 不受 Toyc 编译兼容限制；涉及共享平台层时，再从 Rasterfall 入口核对消费者。

## 当前入口

- 用户构建、测试和工具说明：[../../README.md](../../README.md)
- 语言特性：[../../toyc-c-features.md](../../toyc-c-features.md)
- 自举种子：[../../bootstrap/README.md](../../bootstrap/README.md)
- Windows 平台迁移计划：[../windows-platform-plan.md](../windows-platform-plan.md)
- 历史与考古：[../archaeology/README.md](../archaeology/README.md)
- 编译器时期协作说明：[../AGENTS-toyc-history.md](../AGENTS-toyc-history.md)

考证 SC7、Tinylibc、ToyCCompiler 或 Toyc 的传承时，先读考古入口，再从其问题索引进入证据；不要仅凭当前目录结构、单条提交说明或事后 README 推断历史结论。

