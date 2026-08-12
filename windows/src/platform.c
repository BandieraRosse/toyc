#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include "toy_platform.h"

int toy_platform_list_models(char paths[][TOY_PLATFORM_PATH_MAX], int max)
{
    WIN32_FIND_DATAA data;
    HANDLE handle;
    int count = 0;
    if (!paths || max <= 0) return 0;
    handle = FindFirstFileA("rasterfall\\assets\\models\\*.rmesh", &data);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && count < max)
            snprintf(paths[count++], TOY_PLATFORM_PATH_MAX,
                     "rasterfall/assets/models/%s", data.cFileName);
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
    return count;
}
