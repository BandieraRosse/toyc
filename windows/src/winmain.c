#include <windows.h>

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

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line,
                  int show_command)
{
    SetUnhandledExceptionFilter(toy_windows_unhandled_exception);
    extern int __argc;
    extern char **__argv;
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show_command;
    return main(__argc, __argv);
}
