#include "llm_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

/* ─── HTTP 响应写入回调（动态缓冲区） ── */

struct WriteBuf {
    char *data;
    size_t len;
    size_t cap;
};

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total = size * nmemb;
    struct WriteBuf *buf = (struct WriteBuf *)userdata;

    size_t needed = buf->len + total + 1;
    if (needed > buf->cap) {
        buf->cap = needed * 2;
        char *p = realloc(buf->data, buf->cap);
        if (!p) return 0;
        buf->data = p;
    }
    memcpy(buf->data + buf->len, ptr, total);
    buf->len += total;
    buf->data[buf->len] = '\0';
    return total;
}

char *llm_chat(LLMConfig *cfg, const char *json_body)
{
    if (!cfg || !json_body) return NULL;

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "llm: 初始化 libcurl 失败\n");
        return NULL;
    }

    struct WriteBuf resp = {0};
    struct curl_slist *headers = NULL;

    /* HTTP 头 */
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");

    /* 根据 URL 格式判断 API key 头 */
    if (strstr(cfg->api_url, "anthropic.com")) {
        char auth[512];
        snprintf(auth, sizeof(auth), "x-api-key: %s", cfg->api_key);
        headers = curl_slist_append(headers, auth);
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    } else {
        /* OpenAI-compatible */
        char auth[512];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", cfg->api_key);
        headers = curl_slist_append(headers, auth);
    }

    /* 配置请求 */
    curl_easy_setopt(curl, CURLOPT_URL, cfg->api_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "toyc-llm-agent/0.1");

    /* 执行 */
    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "llm: 请求失败: %s\n", curl_easy_strerror(res));
        free(resp.data);
        return NULL;
    }

    return resp.data; /* 调用者须 free() */
}
