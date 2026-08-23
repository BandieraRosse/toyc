#include <windows.h>
#include <shellapi.h>
#include <direct.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv);
void toy_windows_log(const char *message);

static LONG WINAPI toy_windows_unhandled_exception(EXCEPTION_POINTERS *info)
{
    char line[128];
    if (info && info->ExceptionRecord) {
        wsprintfA(line, "unhandled exception: code=0x%08lX address=%p",
                  info->ExceptionRecord->ExceptionCode,
                  info->ExceptionRecord->ExceptionAddress);
        toy_windows_log(line);
    } else {
        toy_windows_log("unhandled exception: no exception record");
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line,
                   int show_command)
{
    LPWSTR *wide_argv;
    char **utf8_argv;
    int wide_argc, result, i;
    SetUnhandledExceptionFilter(toy_windows_unhandled_exception);
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show_command;
    {
        char path[MAX_PATH];
        DWORD length = GetModuleFileNameA(NULL, path, sizeof(path));
        if (length > 0 && length < sizeof(path)) {
            char *slash = strrchr(path, '\\');
            if (slash) {
                *slash = 0;
                SetCurrentDirectoryA(path);
                _chdir(path);
            }
        }
    }
    wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (!wide_argv) return 1;
    utf8_argv = (char **)calloc((size_t)wide_argc + 1, sizeof(*utf8_argv));
    if (!utf8_argv) { LocalFree(wide_argv); return 1; }
    for (i = 0; i < wide_argc; i++) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1,
                                        NULL, 0, NULL, NULL);
        if (bytes <= 0 || !(utf8_argv[i] = (char *)malloc((size_t)bytes)) ||
            !WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1,
                                 utf8_argv[i], bytes, NULL, NULL)) {
            while (i-- > 0) free(utf8_argv[i]);
            free(utf8_argv); LocalFree(wide_argv); return 1;
        }
    }
    result = main(wide_argc, utf8_argv);
    for (i = 0; i < wide_argc; i++) free(utf8_argv[i]);
    free(utf8_argv);
    LocalFree(wide_argv);
    return result;
}
