/* Portable directory listing.  The provider owns all native directory ABI. */
#include "core.h"
#include "string.h"

int snprintf(char *str, size_t size, const char *format, ...);

static void output(const char *text)
{
    size_t left = strlen(text);
    while (left) {
        ssize_t n = __write(STDOUT, text, left);
        if (n <= 0) return;
        text += n;
        left -= (size_t)n;
    }
}

static int join_path(char *out, size_t cap, const char *base, const char *name)
{
    size_t a = strlen(base), b = strlen(name);
    int slash = a && base[a - 1] != '/';
    if (a + (size_t)slash + b + 1 > cap) return -ENAMETOOLONG;
    for (size_t i = 0; i < a; i++) out[i] = base[i];
    if (slash) out[a++] = '/';
    for (size_t i = 0; i < b; i++) out[a + i] = name[i];
    out[a + b] = '\0';
    return 0;
}

static const char *type_name(int type)
{
    if (type == TOYC_FILE_DIRECTORY) return "DIR";
    if (type == TOYC_FILE_REGULAR) return "FILE";
    if (type == TOYC_FILE_OTHER) return "OTHER";
    return "UNKNOWN";
}

int main(int argc, char **argv)
{
    const char *path = ".";
    int long_format = 0;
    struct toyc_dir_iterator iterator;
    struct toyc_dir_entry entry;
    char name[TOYC_DIR_NAME_MAX];
    char full[4096];
    char line[8192];
    int result;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && strcmp(argv[i], "-") != 0) {
            for (char *p = argv[i] + 1; *p; p++)
                if (*p == 'l') long_format = 1;
        } else {
            path = argv[i];
        }
    }
    result = toyc_dir_open(&iterator, path);
    if (result < 0) {
        output("ls: cannot open directory\n");
        return 1;
    }
    while ((result = toyc_dir_next(&iterator, name, sizeof(name), &entry)) > 0) {
        struct toyc_file_info info;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (!long_format) {
            output(name); output("\n");
            continue;
        }
        if (join_path(full, sizeof(full), path, name) < 0 ||
            toyc_stat(full, &info) < 0) {
            info.type = entry.type; info.size = 0; info.mtime_sec = 0;
        }
        snprintf(line, sizeof(line), "%s %lld %lld %s\n", type_name(info.type),
                 (long long)info.size, (long long)info.mtime_sec, name);
        output(line);
    }
    toyc_dir_close(&iterator);
    return result < 0 ? 1 : 0;
}
