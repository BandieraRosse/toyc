/* char ** 参数下标后仍是 char *，后续指针运算必须按单字节缩放。 */
static char *find_value(char **envp, const char *name)
{
    int name_len = 0;
    while (name[name_len]) name_len++;
    for (int i = 0; envp[i]; i++) {
        int j = 0;
        while (j < name_len && envp[i][j] == name[j]) j++;
        if (j == name_len && envp[i][j] == '=') return envp[i] + j + 1;
    }
    return (char *)0;
}

int main(void)
{
    char *envp[5];
    char *runtime;
    char *display;
    envp[0] = "HTTP_PROXY=http://127.0.0.1:7890";
    envp[1] = "XDG_RUNTIME_DIR=/run/user/1000";
    envp[2] = "NVM_DIR=/home/test/.nvm";
    envp[3] = "WAYLAND_DISPLAY=wayland-0";
    envp[4] = (char *)0;
    runtime = find_value(envp, "XDG_RUNTIME_DIR");
    display = find_value(envp, "WAYLAND_DISPLAY");
    if (!runtime || runtime[0] != '/' || runtime[1] != 'r') return 1;
    if (!display || display[0] != 'w' || display[8] != '0') return 2;
    return 0;
}
