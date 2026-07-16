#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include "third_party/cJSON/cJSON.h"

/*
 * 辅助函数：构建 LLM 请求的 JSON payload，提取回复中的文本。
 *
 * 兼容格式：Anthropic Messages API、OpenAI Chat Completions API。
 */

/*
 * 构建兼容 OpenAI 格式的请求体 JSON：
 *   { "model": "...", "messages": [...] }
 *
 * 返回 JSON 字符串（调用者须 free()）。
 */
char *json_build_chat_request(const char *model, const char *system_prompt,
                              const char *user_message);

/*
 * 从 JSON 响应中提取 assistant 回复文本（OpenAI Chat Completions 格式）。
 *
 * 返回 malloc'd 字符串，未找到返回 NULL。调用者须 free()。
 */
char *json_extract_reply(const char *response_body);

/*
 * 从 JSON 响应中提取回复文本（Anthropic Messages API 格式）。
 */
char *json_extract_anthropic_reply(const char *response_body);

#endif /* JSON_UTIL_H */
