#include "tlibc_everything.h"
#include "toy_platform.h"

void toy_platform_hardware_query(struct toy_platform_hardware *out)
{
    char line[256], buffer[4096], ch;
    int fd, used = 0, package = -1, core = -1, count = 0, i;
    int cursor = 0, available = 0, ended = 0;
    int packages[1024], cores[1024];
    unsigned long long kib = 0;
    memset(out, 0, sizeof(*out));
    fd = __openat(AT_FDCWD, "/proc/cpuinfo", O_RDONLY, 0);
    if (fd >= 0) {
        while (!ended) {
            if (cursor >= available) {
                available = (int)__read(fd, buffer, sizeof(buffer)); cursor = 0;
                if (available <= 0) ended = 1;
            }
            ch = ended ? '\n' : buffer[cursor++];
            if (ch != '\n') {
                if (used < 255) line[used++] = ch;
                continue;
            }
            line[used] = 0;
            if (!used || ended) {
                if (package >= 0 && core >= 0) {
                    for (i = 0; i < count; i++)
                        if (packages[i] == package && cores[i] == core) break;
                    if (i == count && count < 1024) {
                        packages[count] = package; cores[count++] = core;
                    }
                }
                package = core = -1;
            } else if (!strncmp(line, "physical id", 11)) {
                char *p = strchr(line, ':'); if (p) package = atoi(p+1);
            } else if (!strncmp(line, "core id", 7)) {
                char *p = strchr(line, ':'); if (p) core = atoi(p+1);
            }
            used = 0;
        }
        __close(fd);
    }
    out->physical_cores = count;
    fd = __openat(AT_FDCWD, "/proc/meminfo", O_RDONLY, 0);
    if (fd >= 0) {
        int n = (int)__read(fd, line, sizeof(line)-1);
        __close(fd);
        if (n > 0) {
            line[n] = 0;
            if (sscanf(line, "MemTotal: %llu kB", &kib) == 1)
                out->memory_mib = kib / 1024;
        }
    }
}

void toy_platform_host_sample(struct toy_platform_host_sample *out)
{
    struct toy_platform_hardware hardware;
    char buffer[4096];
    int fd, n;
    unsigned long long total = 0, available = 0;
    memset(out, 0, sizeof(*out));
    toy_platform_hardware_query(&hardware);
    out->physical_cores = hardware.physical_cores;
    fd = __openat(AT_FDCWD, "/proc/meminfo", O_RDONLY, 0);
    if (fd < 0) return;
    n = (int)__read(fd, buffer, sizeof(buffer)-1);
    __close(fd);
    if (n <= 0) return;
    buffer[n] = 0;
    for (char *line = buffer; line && *line; ) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (!strncmp(line, "MemTotal:", 9)) sscanf(line+9, "%llu", &total);
        if (!strncmp(line, "MemAvailable:", 13)) sscanf(line+13, "%llu", &available);
        line = next;
    }
    if (total && available <= total) {
        out->memory_total_mib = total / 1024;
        out->memory_used_mib = (total - available) / 1024;
        out->memory_valid = 1;
    }
}

static int scan_model_directory(const char *directory, const char *prefix,
                                char paths[][TOY_PLATFORM_PATH_MAX],
                                int max, int count)
{
    int fd;
    long bytes;
    char buffer[4096];
    struct linux_dirent64 *entry;

    if (count >= max) return count;
    fd = openat(AT_FDCWD, directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    if (fd < 0) return count;
    while (count < max && (bytes = getdents64(fd,
                                                (struct linux_dirent64 *)buffer,
                                                sizeof(buffer))) > 0) {
        entry = (struct linux_dirent64 *)buffer;
        while (count < max && (char *)entry < buffer + bytes) {
            char child_directory[TOY_PLATFORM_PATH_MAX];
            char child_prefix[TOY_PLATFORM_PATH_MAX];
            int name_length = (int)strlen(entry->d_name);
            int is_dot = !strcmp(entry->d_name, ".") ||
                         !strcmp(entry->d_name, "..");
            if (!is_dot && entry->d_type == DT_REG && name_length > 6 &&
                !strcmp(entry->d_name + name_length - 6, ".rmesh")) {
                if ((int)strlen(prefix) + name_length < TOY_PLATFORM_PATH_MAX) {
                    snprintf(paths[count], TOY_PLATFORM_PATH_MAX, "%s%s",
                             prefix, entry->d_name);
                    count++;
                }
            } else if (!is_dot && entry->d_type == DT_DIR &&
                       (int)strlen(directory) + name_length + 2 <
                       TOY_PLATFORM_PATH_MAX &&
                       (int)strlen(prefix) + name_length + 2 <
                       TOY_PLATFORM_PATH_MAX) {
                snprintf(child_directory, sizeof(child_directory), "%s/%s",
                         directory, entry->d_name);
                snprintf(child_prefix, sizeof(child_prefix), "%s%s/",
                         prefix, entry->d_name);
                count = scan_model_directory(child_directory, child_prefix,
                                              paths, max, count);
            }
            if (!entry->d_reclen) break;
            entry = (struct linux_dirent64 *)((char *)entry + entry->d_reclen);
        }
    }
    close(fd);
    return count;
}

int toy_platform_list_models(char paths[][TOY_PLATFORM_PATH_MAX], int max)
{
    if (!paths || max <= 0) return 0;
    return scan_model_directory("rasterfall/assets/models",
                                "rasterfall/assets/models/", paths, max, 0);
}
