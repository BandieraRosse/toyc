#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "toy_platform.h"

static int scan_model_directory(const char *directory, const char *prefix,
                                char paths[][TOY_PLATFORM_PATH_MAX],
                                int max, int count)
{
    WIN32_FIND_DATAA entry;
    HANDLE search;
    char pattern[MAX_PATH];
    int length;

    if (count >= max) return count;
    length = (int)strlen(directory);
    if (length + 3 >= (int)sizeof(pattern)) return count;
    strcpy(pattern, directory);
    strcpy(pattern + length, "\\*");
    search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) return count;
    do {
        char child_directory[MAX_PATH];
        char child_prefix[TOY_PLATFORM_PATH_MAX];
        int name_length = (int)strlen(entry.cFileName);
        int is_dot = !strcmp(entry.cFileName, ".") ||
                     !strcmp(entry.cFileName, "..");
        if (is_dot) continue;
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (length + name_length + 2 >= (int)sizeof(child_directory) ||
                (int)strlen(prefix) + name_length + 2 >= TOY_PLATFORM_PATH_MAX)
                continue;
            strcpy(child_directory, directory);
            strcpy(child_directory + length, "\\");
            strcpy(child_directory + length + 1, entry.cFileName);
            strcpy(child_prefix, prefix);
            strcat(child_prefix, entry.cFileName);
            strcat(child_prefix, "/");
            count = scan_model_directory(child_directory, child_prefix,
                                          paths, max, count);
        } else if (count < max && name_length > 6 &&
                   !strcmp(entry.cFileName + name_length - 6, ".rmesh") &&
                   (int)strlen(prefix) + name_length < TOY_PLATFORM_PATH_MAX) {
            strcpy(paths[count], prefix);
            strcpy(paths[count] + strlen(prefix), entry.cFileName);
            count++;
        }
    } while (count < max && FindNextFileA(search, &entry));
    FindClose(search);
    return count;
}

int toy_platform_list_models(char paths[][TOY_PLATFORM_PATH_MAX], int max)
{
    int count = 0;
    char directory[MAX_PATH];
    const char prefix[] = "rasterfall/assets/models/";
    DWORD length;

    if (!paths || max <= 0) return 0;
    length = GetModuleFileNameA(NULL, directory, sizeof(directory));
    if (!length || length >= sizeof(directory)) return 0;
    while (length > 0 && directory[length - 1] != '\\' &&
           directory[length - 1] != '/')
        length--;
    if (length + strlen("rasterfall\\assets\\models") >= sizeof(directory))
        return 0;
    strcpy(directory + length, "rasterfall\\assets\\models");
    return scan_model_directory(directory, prefix, paths, max, count);
}
