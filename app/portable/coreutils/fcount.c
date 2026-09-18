/* Count regular files in one directory using the common directory iterator. */
#include "core.h"
#include "string.h"

int snprintf(char *str, size_t size, const char *format, ...);

static void output(const char *text)
{
    __write(STDOUT, text, strlen(text));
}

static int join_path(char *out, size_t capacity, const char *base,
                     const char *name)
{
    size_t base_len = strlen(base);
    size_t name_len = strlen(name);
    size_t slash = base_len && base[base_len - 1] != '/';
    if (base_len + slash + name_len + 1 > capacity) return -ENAMETOOLONG;
    for (size_t i = 0; i < base_len; i++) out[i] = base[i];
    if (slash) out[base_len++] = '/';
    for (size_t i = 0; i < name_len; i++) out[base_len + i] = name[i];
    out[base_len + name_len] = '\0';
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : ".";
    struct toyc_dir_iterator iterator;
    struct toyc_dir_entry entry;
    char name[TOYC_DIR_NAME_MAX];
    char child[4096];
    char line[64];
    int count = 0;
    int result = toyc_dir_open(&iterator, path);
    if (result < 0) {
        output("fcount: cannot open directory\n");
        return 1;
    }
    while ((result = toyc_dir_next(&iterator, name, sizeof(name), &entry)) > 0) {
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (entry.type == TOYC_FILE_UNKNOWN) {
            struct toyc_file_info info;
            if (join_path(child, sizeof(child), path, name) < 0 ||
                toyc_stat(child, &info) < 0)
                continue;
            entry.type = info.type;
        }
        if (entry.type == TOYC_FILE_REGULAR) count++;
    }
    toyc_dir_close(&iterator);
    if (result < 0) return 1;
    snprintf(line, sizeof(line), "%d\n", count);
    output(line);
    return 0;
}
