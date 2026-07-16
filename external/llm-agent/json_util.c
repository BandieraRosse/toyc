#include "json_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *json_build_chat_request(const char *model, const char *system_prompt,
                              const char *user_message)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    /* model */
    cJSON_AddStringToObject(root, "model", model ? model : "gpt-4o-mini");

    /* messages array */
    cJSON *msgs = cJSON_AddArrayToObject(root, "messages");
    if (!msgs) { cJSON_Delete(root); return NULL; }

    /* system message */
    if (system_prompt && system_prompt[0]) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(msgs, sys);
    }

    /* user message */
    cJSON *user = cJSON_CreateObject();
    cJSON_AddStringToObject(user, "role", "user");
    cJSON_AddStringToObject(user, "content", user_message ? user_message : "");
    cJSON_AddItemToArray(msgs, user);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

char *json_extract_reply(const char *response_body)
{
    if (!response_body) return NULL;

    cJSON *root = cJSON_Parse(response_body);
    if (!root) {
        fprintf(stderr, "json: 解析响应失败\n");
        return NULL;
    }

    char *result = NULL;

    /* OpenAI: response.choices[0].message.content */
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *msg = cJSON_GetObjectItem(choice, "message");
        cJSON *content = cJSON_GetObjectItem(msg, "content");
        if (cJSON_IsString(content)) {
            result = strdup(cJSON_GetStringValue(content));
        }
    }

    if (!result) {
        /* Anthropic: content[0].text */
        result = json_extract_anthropic_reply(response_body);
    }

    cJSON_Delete(root);
    return result;
}

char *json_extract_anthropic_reply(const char *response_body)
{
    if (!response_body) return NULL;

    cJSON *root = cJSON_Parse(response_body);
    if (!root) return NULL;

    char *result = NULL;
    cJSON *content = cJSON_GetObjectItem(root, "content");
    if (cJSON_IsArray(content) && cJSON_GetArraySize(content) > 0) {
        cJSON *block = cJSON_GetArrayItem(content, 0);
        cJSON *text = cJSON_GetObjectItem(block, "text");
        if (cJSON_IsString(text)) {
            result = strdup(cJSON_GetStringValue(text));
        }
    }

    cJSON_Delete(root);
    return result;
}
