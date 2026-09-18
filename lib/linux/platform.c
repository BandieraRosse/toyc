#include "tlibc_everything.h"
#include "toy_platform.h"

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
