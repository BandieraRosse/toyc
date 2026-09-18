/* SPDX-License-Identifier: MIT
 *
 * Portable grep subset.  File and directory operations use only the Toyc
 * common contract; no linux_dirent64 or Win32 directory structure escapes
 * the platform provider.
 */
#include "core.h"
#include "string.h"

int snprintf(char *str, size_t size, const char *format, ...);

#define GREP_BUF_SIZE 65536
static char grep_buffer[GREP_BUF_SIZE];
static const char *grep_pattern;
static int opt_ignore_case;
static int opt_invert;
static int opt_line_number;
static int opt_recursive;
static int opt_count;
static int opt_quiet;
static int found_match;

static void output_bytes(const char *text)
{
    size_t length = strlen(text);
    while (length) {
        ssize_t written = __write(STDOUT, text, length);
        if (written <= 0) return;
        text += written;
        length -= (size_t)written;
    }
}

static void output_number(int value)
{
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    output_bytes(buffer);
}

static char lower_ascii(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}

static int line_matches(const char *line)
{
    const char *h;
    if (!opt_ignore_case) {
        int matched = strstr(line, grep_pattern) != NULL;
        return opt_invert ? !matched : matched;
    }
    if (!*grep_pattern) return !opt_invert;
    for (h = line; *h; h++) {
        const char *a = h;
        const char *b = grep_pattern;
        while (*a && *b && lower_ascii(*a) == lower_ascii(*b)) {
            a++; b++;
        }
        if (!*b) return !opt_invert;
    }
    return opt_invert;
}

static void print_line(const char *path, int line_number, const char *line,
                       int show_path)
{
    size_t len = strlen(line);
    if (opt_quiet) return;
    if (show_path) { output_bytes(path); output_bytes(":"); }
    if (opt_line_number) { output_number(line_number); output_bytes(":"); }
    output_bytes(line);
    if (len == 0 || line[len - 1] != '\n') output_bytes("\n");
}

static int grep_file(const char *path, int show_path)
{
    int use_stdin = strcmp(path, "(standard input)") == 0;
    int fd = use_stdin ? STDIN : __openat(AT_FDCWD, path, O_RDONLY, 0);
    size_t pending = 0;
    int line_number = 0;
    int matches = 0;
    if (fd < 0) {
        output_bytes("grep: "); output_bytes(path); output_bytes(": ");
        output_number(fd); output_bytes("\n");
        return 0;
    }
    for (;;) {
        ssize_t got = __read(fd, grep_buffer + pending,
                             sizeof(grep_buffer) - pending - 1);
        size_t start = 0;
        if (got < 0) {
            if (!use_stdin) __close(fd);
            return 0;
        }
        if (got == 0) break;
        pending += (size_t)got;
        grep_buffer[pending] = '\0';
        for (size_t i = 0; i < pending; i++) {
            if (grep_buffer[i] == '\n') {
                grep_buffer[i] = '\0';
                line_number++;
                if (line_matches(grep_buffer + start)) {
                    matches++;
                    found_match = 1;
                    if (!opt_count)
                        print_line(path, line_number, grep_buffer + start, show_path);
                }
                grep_buffer[i] = '\n';
                start = i + 1;
            }
        }
        pending -= start;
        if (pending) __memmove(grep_buffer, grep_buffer + start, pending);
    }
    if (pending) {
        grep_buffer[pending] = '\0';
        line_number++;
        if (line_matches(grep_buffer)) {
            matches++;
            found_match = 1;
            if (!opt_count)
                print_line(path, line_number, grep_buffer, show_path);
        }
    }
    if (!use_stdin) __close(fd);
    if (opt_count && !opt_quiet) {
        if (show_path) { output_bytes(path); output_bytes(":"); }
        output_number(matches); output_bytes("\n");
    }
    return matches;
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

static void grep_tree(const char *path)
{
    struct toyc_dir_iterator iterator;
    char name[TOYC_DIR_NAME_MAX];
    char child[4096];
    struct toyc_dir_entry entry;
    int result = toyc_dir_open(&iterator, path);
    if (result < 0) return;
    while ((result = toyc_dir_next(&iterator, name, sizeof(name), &entry)) > 0) {
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (join_path(child, sizeof(child), path, name) < 0) continue;
        if (entry.type == TOYC_FILE_UNKNOWN) {
            struct toyc_file_info info;
            if (toyc_stat(child, &info) < 0) continue;
            entry.type = info.type;
        }
        if (entry.type == TOYC_FILE_DIRECTORY) grep_tree(child);
        else if (entry.type == TOYC_FILE_REGULAR)
            grep_file(child, 1);
    }
    toyc_dir_close(&iterator);
}

static void usage(void)
{
    output_bytes("Usage: grep [-i] [-v] [-n] [-r] [-c] [-q] <pattern> <file>...\n");
}

int main(int argc, char **argv)
{
    int first;
    if (argc < 2) { usage(); return 1; }
    for (first = 1; first < argc && argv[first][0] == '-'; first++) {
        char *p = argv[first] + 1;
        if (strcmp(argv[first], "--") == 0) { first++; break; }
        while (*p) {
            if (*p == 'i') opt_ignore_case = 1;
            else if (*p == 'v') opt_invert = 1;
            else if (*p == 'n') opt_line_number = 1;
            else if (*p == 'r') opt_recursive = 1;
            else if (*p == 'c') opt_count = 1;
            else if (*p == 'q') opt_quiet = 1;
            else { usage(); return 1; }
            p++;
        }
    }
    if (first >= argc) { usage(); return 1; }
    grep_pattern = argv[first++];
    if (first == argc) {
        /* stdin is already a common Toyc fd, so this path needs no filename. */
        char *stdin_name = "(standard input)";
        int old_recursive = opt_recursive;
        opt_recursive = 0;
        grep_file(stdin_name, 0);
        opt_recursive = old_recursive;
    } else {
        int show_path = (argc - first > 1) || opt_recursive;
        for (; first < argc; first++) {
            if (opt_recursive) {
                struct toyc_file_info info;
                if (toyc_stat(argv[first], &info) == 0 &&
                    info.type == TOYC_FILE_DIRECTORY)
                    grep_tree(argv[first]);
                else
                    grep_file(argv[first], show_path);
            } else {
                grep_file(argv[first], show_path);
            }
        }
    }
    return found_match ? 0 : 1;
}
