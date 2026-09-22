#include "core.h"
#include "string.h"

static void output(const char *text)
{
    __write(STDOUT, text, strlen(text));
}

int main(int argc, char **argv)
{
    int status = 0;
    if (argc < 2) {
        output("Usage: mkdir <directory>...\n");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        int result = toyc_mkdir(argv[i], 0777);
        if (result < 0) {
            output("mkdir: "); output(argv[i]); output(": failed\n");
            status = 1;
        }
    }
    return status;
}
