#include "tlibc_everything.h"
#include "toy_platform.h"
int toy_platform_list_models(char paths[][TOY_PLATFORM_PATH_MAX], int max)
{
    int count = 0;
    int fd;
    long bytes;
    char buffer[4096];
    struct linux_dirent64 *entry;
    const char directory[] = "rasterfall/assets/models";
    const char prefix[] = "rasterfall/assets/models/";

    if (!paths || max <= 0) return 0;

    fd = openat(AT_FDCWD, directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    if (fd < 0) return 0;
    while (count < max && (bytes = getdents64(fd,
                                                (struct linux_dirent64 *)buffer,
                                                sizeof(buffer))) > 0) {
        entry = (struct linux_dirent64 *)buffer;
        while (count < max && (char *)entry < buffer + bytes) {
            if (entry->d_type == DT_REG && strlen(entry->d_name) > 6 &&
                !strcmp(entry->d_name + strlen(entry->d_name) - 6, ".rmesh")) {
                snprintf(paths[count], TOY_PLATFORM_PATH_MAX, "%s%s",
                         prefix, entry->d_name);
                count++;
            }
            if (!entry->d_reclen) break;
            entry = (struct linux_dirent64 *)((char *)entry + entry->d_reclen);
        }
    }
    close(fd);
    return count;
}
