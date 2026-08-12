#include "tlibc_everything.h"
#include "toy_platform.h"

int toy_platform_list_models(char paths[][TOY_PLATFORM_PATH_MAX], int max)
{
    char buffer[8192];
    struct linux_dirent64 *entry;
    int fd, count = 0;
    long bytes;
    if (!paths || max <= 0) return 0;
    fd = __openat(AT_FDCWD, "rasterfall/assets/models", O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return 0;
    while (count < max && (bytes = __getdents64(
               fd, (struct linux_dirent64 *)buffer, sizeof(buffer))) > 0) {
        long offset = 0;
        while (offset < bytes && count < max) {
            entry = (struct linux_dirent64 *)(buffer + offset);
            if (!entry->d_reclen) break;
            if (entry->d_name[0] != '.' && strlen(entry->d_name) >= 6 &&
                !strcmp(entry->d_name + strlen(entry->d_name) - 6, ".rmesh"))
                snprintf(paths[count++], TOY_PLATFORM_PATH_MAX,
                         "rasterfall/assets/models/%s", entry->d_name);
            offset += entry->d_reclen;
        }
    }
    __close(fd);
    return count;
}
