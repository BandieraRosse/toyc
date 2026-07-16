# llm-agent — 命令行 LLM API 客户端

通过命令行调用大模型 API（兼容 OpenAI / Anthropic 格式）的工具。

> **⚠ 定位说明**
>
> llm-agent 是 **tcc 项目的附赠工具**，与编译器的自举链无关。
> 它依赖 libcurl、glibc 等系统库，用 gcc 编译，
> **不参与 tcc 自举测试，不验证编译器的正确性。**

## 依赖

```bash
# Debian/Ubuntu
sudo apt install libcurl4-openssl-dev

# Fedora
sudo dnf install libcurl-devel
```

若 libcurl 不可用，编译会生成一个提示安装的 stub。

## 编译

```bash
# 从项目根目录
make external

# 或直接
cd external/llm-agent && make
```

产物输出到 `build/llm-agent/`。

## 使用

```bash
# 设置 API 密钥（必填）
export LLM_API_KEY="sk-..."

# 命令行传 prompt
./llm-agent -p "用 Rust 写一个快速排序"

# 从 stdin 读 prompt
echo "解释一下零拷贝" | ./llm-agent

# 指定模型
./llm-agent -m claude-sonnet-4-20250514 -p "C 语言 self-hosting compiler 的原理"

# 指定 API 端点（默认 OpenAI）
export LLM_API_URL="https://api.anthropic.com/v1/messages"

# 设置 system prompt
export LLM_SYSTEM="你是一个精通 C 编译器的专家"
```

### 环境变量

| 变量 | 必填 | 说明 |
|------|------|------|
| `LLM_API_KEY` | 是 | API 密钥 |
| `LLM_API_URL` | 否 | 端点 URL（默认 OpenAI） |
| `LLM_MODEL` | 否 | 模型名（默认 gpt-4o-mini） |
| `LLM_SYSTEM` | 否 | system prompt |

## 项目结构

```
external/llm-agent/
├── main.c               # CLI 入口
├── llm_client.c/.h      # libcurl HTTP 客户端
├── json_util.c/.h       # JSON 序列化/反序列化（封装 cJSON）
├── buffer.c/.h          # 动态字符串构造器
├── third_party/cJSON/   # 嵌入式 cJSON（MIT 许可证）
├── Makefile             # gcc 构建，自动检测 libcurl
└── README.md
```

## 许可证

llm-agent 本身采用与 tcc 相同的许可证。
cJSON 是 MIT 许可证，见 `third_party/cJSON/` 中的版权声明。
