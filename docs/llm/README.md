# LLM 维护入口

`llm/` 包含 GPT-2、Qwen2 推理实现和共享数值基础设施。当前用户构建、模型准备和运行方式见 [根 README](../../README.md#测试)。

主要验证入口：

```sh
make test-llm
make test-llm-qwen2
```

修改 checkpoint、tokenizer、算子或 KV-cache 时，应同时检查对应加载路径、数值参考测试和端到端单 token 前向测试。LLM 工作不应默认读取 Rasterfall 文档。

