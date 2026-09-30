#ifdef TOYC_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif
#include <stdio.h>
#include <string.h>
#include "rf_boot_files.h"

static int boot_attributes(const char *path, int *directory)
{
#ifdef TOYC_WINDOWS
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return -1;
    *directory = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    if (lstat(path, &st) < 0 || S_ISLNK(st.st_mode)) return -1;
    *directory = S_ISDIR(st.st_mode);
#endif
    return 0;
}

static int boot_resolve(const char *relative, char *disk, size_t cap,
                        int *directory)
{
    const char *at = relative;
    int is_dir;
    size_t used;
    if (!relative || !disk || !directory) return -1;
    used = strlen(RF_BOOT_FILE_ROOT);
    if (used >= cap) return -1;
    memcpy(disk, RF_BOOT_FILE_ROOT, used + 1);
    if (boot_attributes(disk, &is_dir) < 0 || !is_dir) return -1;
    while (*at) {
        const char *end = strchr(at, '/');
        size_t length = end ? (size_t)(end - at) : strlen(at);
        if (!length || length > 175 || !strncmp(at, "..", length) ||
            (length == 1 && *at == '.') ||
            memchr(at, '\\', length) || memchr(at, ':', length) ||
            used + length + 2 > cap) return -1;
        disk[used++] = '/';
        memcpy(disk + used, at, length);
        used += length;
        disk[used] = 0;
        if (boot_attributes(disk, &is_dir) < 0) return -1;
        at = end ? end + 1 : at + length;
        if (*at && !is_dir) return -1;
    }
    *directory = is_dir;
    return 0;
}

int rf_boot_files_stat(const char *relative, int *directory)
{
    char disk[512];
    return boot_resolve(relative, disk, sizeof(disk), directory);
}

int rf_boot_files_list(const char *relative,
                       char entries[RF_BOOT_LIST_LIMIT][176],
                       int *count, int *more)
{
    char disk[512];
    int directory;
    if (!entries || !count || !more ||
        boot_resolve(relative, disk, sizeof(disk), &directory) < 0 ||
        !directory) return -1;
    *count = *more = 0;
#ifdef TOYC_WINDOWS
    {
        char pattern[516];
        WIN32_FIND_DATAA entry;
        HANDLE search;
        if (snprintf(pattern, sizeof(pattern), "%s/*", disk) >=
            (int)sizeof(pattern)) return -1;
        search = FindFirstFileA(pattern, &entry);
        if (search == INVALID_HANDLE_VALUE)
            return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
        do {
            size_t name_size;
            if (!strcmp(entry.cFileName, ".") ||
                !strcmp(entry.cFileName, "..") ||
                (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            name_size = strlen(entry.cFileName);
            if (name_size > 173) continue;
            if (*count >= RF_BOOT_LIST_LIMIT) { *more = 1; break; }
            memcpy(entries[*count], entry.cFileName, name_size);
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                entries[*count][name_size++] = '/';
            entries[*count][name_size] = 0;
            ++*count;
        } while (FindNextFileA(search, &entry));
        FindClose(search);
    }
#else
    {
        DIR *stream = opendir(disk);
        struct dirent *entry;
        if (!stream) return -1;
        while ((entry = readdir(stream))) {
            char child[512];
            int child_dir;
            size_t name_size;
            if (!strcmp(entry->d_name, ".") ||
                !strcmp(entry->d_name, "..")) continue;
            if (snprintf(child, sizeof(child), "%s/%s", disk,
                         entry->d_name) >= (int)sizeof(child) ||
                boot_attributes(child, &child_dir) < 0) continue;
            name_size = strlen(entry->d_name);
            if (name_size > 173) continue;
            if (*count >= RF_BOOT_LIST_LIMIT) { *more = 1; break; }
            memcpy(entries[*count], entry->d_name, name_size);
            if (child_dir) entries[*count][name_size++] = '/';
            entries[*count][name_size] = 0;
            ++*count;
        }
        closedir(stream);
    }
#endif
    return 0;
}

int rf_boot_files_read(const char *relative, unsigned char *bytes, size_t cap,
                       size_t *size, int *more)
{
    char disk[512];
    int directory;
    FILE *file;
    if (!bytes || !cap || !size || !more ||
        boot_resolve(relative, disk, sizeof(disk), &directory) < 0 ||
        directory) return -1;
    file = fopen(disk, "rb");
    if (!file) return -1;
    *size = fread(bytes, 1, cap, file);
    *more = fgetc(file) != EOF;
    if (ferror(file)) { fclose(file); return -1; }
    fclose(file);
    return 0;
}
