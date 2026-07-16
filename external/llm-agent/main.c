/*
 * llm-agent — 通过命令行调用 LLM API 的 CLI 工具
 *
 * 用法：
 *   ./llm-agent -p "你的提示词"
 *   echo "你的提示词" | ./llm-agent
 *   ./llm-agent --model claude-sonnet-4-20250514 -p "聊聊 C 语言"
 *
 * 环境变量：
 *   LLM_API_KEY    — API 密钥（必填）
 *   LLM_API_URL    — API 端点 URL（可选，默认 OpenAI）
 *   LLM_MODEL      — 模型名称（可选，默认 gpt-4o-mini）
 *   LLM_SYSTEM     — system prompt（可选）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "llm_client.h"
#include "json_util.h"
#include "buffer.h"

#define DEFAULT_API_URL "https://api.openai.com/v1/chat/completions"
#define DEFAULT_MODEL   "gpt-4o-mini"

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "用法: %s [选项]\n"
        "\n"
        "选项:\n"
        "  -p, --prompt TEXTO    用户消息\n"
        "  -m, --model  NOME     模型名称（默认: $LLM_MODEL 或 gpt-4o-mini）\n"
        "  -s, --system TEXTO    System prompt（默认: $LLM_SYSTEM）\n"
        "  -h, --help            显示此帮助\n"
        "\n"
        "未提供 -p 时从 stdin 读取 prompt。\n"
        "\n"
        "环境变量:\n"
        "  LLM_API_KEY   API 密钥（必填）\n"
        "  LLM_API_URL   端点 URL\n"
        "  LLM_MODEL     模型名称\n"
        "  LLM_SYSTEM    System prompt\n",
        prog);
}

int main(int argc, char **argv)
{
    const char *api_key  = getenv("LLM_API_KEY");
    const char *api_url  = getenv("LLM_API_URL");
    const char *model    = getenv("LLM_MODEL");
    const char *system_p = getenv("LLM_SYSTEM");
    const char *prompt   = NULL;

    /* 解析参数 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--prompt") == 0) {
            if (i + 1 < argc) prompt = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--model") == 0) {
            if (i + 1 < argc) model = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--system") == 0) {
            if (i + 1 < argc) system_p = argv[++i];
        } else {
            fprintf(stderr, "错误: 未知参数: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* 验证 API key */
    if (!api_key || !api_key[0]) {
        fprintf(stderr, "错误: 未设置 LLM_API_KEY\n");
        return 1;
    }

    /* 未提供 -p 时从 stdin 读取 prompt */
    if (!prompt) {
        Buffer buf;
        buf_init(&buf);
        char line[1024];
        int first = 1;
        while (fgets(line, sizeof(line), stdin)) {
            if (!first) buf_putc(&buf, '\n');
            buf_puts(&buf, line);
            first = 0;
        }
        prompt = buf_cstr(&buf);
        /* 不释放 buf — 字符串存活到程序结束 */
    }

    /* Defaults */
    if (!api_url) api_url = DEFAULT_API_URL;
    if (!model)   model   = DEFAULT_MODEL;

    /* 构建 JSON payload */
    char *json_body = json_build_chat_request(model, system_p, prompt);
    if (!json_body) {
        fprintf(stderr, "错误: 构建 JSON 请求失败\n");
        return 1;
    }

    /* 配置客户端 */
    LLMConfig cfg = {
        .api_key = api_key,
        .api_url = api_url,
        .model   = model,
    };

    /* 发送请求 */
    fprintf(stderr, "⟐ 发送请求到 %s (%s) ...\n", api_url, model);
    char *response = llm_chat(&cfg, json_body);
    free(json_body);

    if (!response) {
        fprintf(stderr, "错误: 请求失败\n");
        return 1;
    }

    /* 提取回复 */
    char *reply = json_extract_reply(response);
    if (reply) {
        printf("%s\n", reply);
        free(reply);
    } else {
        /* 备用：打印原始响应 */
        printf("(无法提取回复文本)\n");
        fprintf(stderr, "原始响应:\n%s\n", response);
    }

    free(response);
    return 0;
}
