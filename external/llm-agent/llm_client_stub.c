#include "llm_client.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * libcurl 不可用时的桩实现。
 * 提示用户安装开发包，返回 NULL。
 */

char *llm_chat(LLMConfig *cfg, const char *json_body)
{
    (void)cfg;
    (void)json_body;
    fprintf(stderr,
        "llm: libcurl 不可用。\n"
        "  请安装开发头文件:\n"
        "    sudo apt install libcurl4-openssl-dev   # Debian/Ubuntu\n"
        "    sudo dnf install libcurl-devel          # Fedora\n"
        "  然后重新编译: make clean && make\n");
    return NULL;
}
