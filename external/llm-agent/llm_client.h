#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

/* ─── API 配置 ────────────────────────────────────── */

typedef struct {
    const char *api_key;    /* API 密钥 */
    const char *api_url;    /* API 端点 URL（如 https://api.openai.com/v1/chat/completions） */
    const char *model;      /* 模型名称（如 gpt-4o-mini, claude-sonnet-4-20250514） */
} LLMConfig;

/*
 * 向 LLM API 发送聊天请求，返回响应。
 *
 * 参数：
 *   cfg         — API 配置
 *   json_body   — 已序列化的 JSON payload（调用者须在返回后 free()）
 *
 * 返回：
 *   malloc'd HTTP 响应体字符串，失败返回 NULL。调用者须 free()。
 */
char *llm_chat(LLMConfig *cfg, const char *json_body);

#endif /* LLM_CLIENT_H */
