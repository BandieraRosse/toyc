#include "core.h"
#include "string.h"

static void output(const char *text)
{
    __write(STDOUT, text, strlen(text));
}

int main(int argc, char **argv)
{
    char path[4096];
    int result;
    (void)argc;
    (void)argv;
    result = toyc_getcwd(path, sizeof(path));
    if (result < 0) {
        output("pwd: failed\n");
        return 1;
    }
    output(path);
    output("\n");
    return 0;
}
