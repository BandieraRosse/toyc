#include <stdlib.h>
#include <string.h>

#include "tlibc_types.h"

char **global_envp;

char *get_env_var(char *envp[], const char *name)
{
    size_t length;
    if (!envp || !name) return NULL;
    length = strlen(name);
    for (int i = 0; envp[i]; i++)
        if (!strncmp(envp[i], name, length) && envp[i][length] == '=')
            return envp[i] + length + 1;
    return NULL;
}
