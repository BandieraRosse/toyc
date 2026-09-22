#include <windows.h>

#include "tlibc_types.h"

int main(int argc, char **argv);

static char *toy_windows_arg_utf8(const wchar_t *wide)
{
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                                    NULL, 0, NULL, NULL);
    char *value;
    if (bytes <= 0) return NULL;
    value = (char *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)bytes);
    if (!value) return NULL;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1,
                             value, bytes, NULL, NULL)) {
        HeapFree(GetProcessHeap(), 0, value);
        return NULL;
    }
    return value;
}

int wmain(int argc, wchar_t **wide_argv)
{
    char **argv = (char **)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                     ((SIZE_T)argc + 1) * sizeof(*argv));
    int i, result;
    if (!argv) return 1;
    for (i = 0; i < argc; i++) {
        argv[i] = toy_windows_arg_utf8(wide_argv[i]);
        if (!argv[i]) {
            while (i-- > 0) HeapFree(GetProcessHeap(), 0, argv[i]);
            HeapFree(GetProcessHeap(), 0, argv);
            return 1;
        }
    }
    result = main(argc, argv);
    for (i = 0; i < argc; i++) HeapFree(GetProcessHeap(), 0, argv[i]);
    HeapFree(GetProcessHeap(), 0, argv);
    return result;
}
