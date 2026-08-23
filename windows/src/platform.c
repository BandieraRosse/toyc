#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "toy_platform.h"

int toy_platform_list_models(char paths[][TOY_PLATFORM_PATH_MAX], int max)
{
    WIN32_FIND_DATAA entry;
    HANDLE search;
    int count = 0;
    char directory[MAX_PATH];
    const char prefix[] = "rasterfall/assets/models/";
    const char suffix[] = "rasterfall\\assets\\models\\*.rmesh";
    DWORD length;

    if (!paths || max <= 0) return 0;
    length = GetModuleFileNameA(NULL, directory, sizeof(directory));
    if (!length || length >= sizeof(directory)) return 0;
    while (length > 0 && directory[length - 1] != '\\' &&
           directory[length - 1] != '/')
        length--;
    if (length + strlen(suffix) >= sizeof(directory)) return 0;
    strcpy(directory + length, suffix);
    search = FindFirstFileA(directory, &entry);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            int prefix_length = (int)strlen(prefix);
            int name_length = (int)strlen(entry.cFileName);
            if (prefix_length + name_length < TOY_PLATFORM_PATH_MAX) {
                memcpy(paths[count], prefix, prefix_length);
                memcpy(paths[count] + prefix_length, entry.cFileName,
                       name_length + 1);
                count++;
            }
        }
    } while (count < max && FindNextFileA(search, &entry));
    FindClose(search);
    return count;
}
