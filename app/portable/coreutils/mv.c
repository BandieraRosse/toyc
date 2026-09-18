#include "core.h"
#include "string.h"

static void output(const char *text)
{
    __write(STDOUT, text, strlen(text));
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        output("Usage: mv <old> <new>\n");
        return 1;
    }
    if (toyc_rename(argv[1], argv[2]) < 0) {
        output("mv: rename failed\n");
        return 1;
    }
    return 0;
}
