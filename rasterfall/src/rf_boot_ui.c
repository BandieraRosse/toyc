#include "core.h"
#include "tlibc_everything.h"
#include "rf_boot_ui.h"
#include "rf_boot_files.h"
#include "toy_platform.h"
#ifdef TOYC_WINDOWS
#include "rf_gpu_vulkan_backend.h"
#endif
#include "fb_draw.h"
#include "rf_boot_canvas.h"
#include <stdio.h>

#define FIRMWARE_FIRST_ROW_Y 201
#define FIRMWARE_ROW_STEP 50
#define BOOT_KEY_ESC 1
#define BOOT_KEY_1 2
#define BOOT_KEY_2 3
#define BOOT_KEY_3 4
#define BOOT_KEY_4 5
#define BOOT_KEY_5 6
#define BOOT_KEY_BACKSPACE 14
#define BOOT_KEY_ENTER 28
#define BOOT_KEY_UP 103
#define BOOT_KEY_DOWN 108
#define BOOT_BUTTON_LEFT 0x110
#define BOOT_LINES 18
#define BOOT_AUTO_US 5000000

enum boot_screen { BOOT_MANAGER, BOOT_SHELL, BOOT_WORKBENCH,
                   BOOT_SHELL_RENDERER, BOOT_DIAGNOSTICS, BOOT_FIRMWARE };

struct boot_ui {
    int screen;
    int selected;
    int renderer;
    int hover; /* graphical control under the pointer, independent of selection */
    char cwd[192];
    char command[192];
    int command_length;
    char lines[BOOT_LINES][176];
    int line_count;
    char error[160];
    struct toy_platform_hardware hardware;
    struct rf_core_status core_status;
    struct rf_gpu_status gpu_status;
    int gpu_ready;
    int automatic;
    int auto_seconds;
    int urgent_output;
};

void rf_boot_record_event(void *context, const char *service, int result,
                          int64_t elapsed_us)
{
    struct rf_boot_journal *journal = (struct rf_boot_journal *)context;
    if (journal && !journal->started_us && !strcmp(service, "window"))
        journal->started_us = rf_core_clock_now_us() - elapsed_us;
    if (journal && journal->count < RF_BOOT_MAX_EVENTS) {
        struct rf_boot_event *event = &journal->events[journal->count++];
        event->service = service;
        event->result = result;
        event->elapsed_us = elapsed_us;
    }
    rf_boot_log_task("core", service, result, elapsed_us);
}

void rf_boot_log_task(const char *owner, const char *task, int result,
                      int64_t elapsed_us)
{
    char line[300];
    snprintf(line, sizeof(line), "RF-BOOT stage=%s task=%s status=%s elapsed_us=%lld",
             owner, task, result < 0 ? "fail" :
             result ? "unavailable" : "ok", (long long)elapsed_us);
    __printf("%s\n", line);
#ifdef TOYC_WINDOWS
    toy_windows_log(line);
#endif
}

static void boot_marker(const char *line)
{
    __printf("%s\n", line);
#ifdef TOYC_WINDOWS
    toy_windows_log(line);
#endif
}

static void boot_line(struct boot_ui *ui, const char *line)
{
    size_t length = strlen(line);
    if (length > 144) {
        char part[145];
        memcpy(part, line, 144); part[144] = 0;
        boot_line(ui, part);
        boot_line(ui, line + 144);
        return;
    }
    if (ui->line_count == BOOT_LINES) {
        memmove(ui->lines, ui->lines + 1,
                sizeof(ui->lines[0]) * (BOOT_LINES - 1));
        ui->line_count--;
    }
    snprintf(ui->lines[ui->line_count++], sizeof(ui->lines[0]),
             "%.*s", (int)sizeof(ui->lines[0]) - 1, line);
}

static void boot_error_line(struct boot_ui *ui, const char *line)
{
    ui->urgent_output = 1;
    boot_line(ui, line);
}

static void boot_journal_draw(struct toy_surface *s,
                              const struct rf_boot_journal *journal,
                              int x, int y, int rows)
{
    char line[160];
    int start = journal && journal->count > rows ? journal->count - rows : 0;
    boot_text(s, x, y, "RESULT   SERVICE / TASK                         ELAPSED", BOOT_DIM);
    boot_rule(s, x, y + 25, 568, BOOT_EDGE);
    if (!journal) return;
    for (int i = start; i < journal->count; ++i) {
        const struct rf_boot_event *event = &journal->events[i];
        int row = y + 36 + (i - start) * 24;
        boot_text(s, x, row, event->result < 0 ? "[FAIL]" :
                  event->result ? "[N/A ]" : "[ OK ]",
                  event->result ? BOOT_AMBER : BOOT_CYAN);
        snprintf(line, sizeof(line), "%-34.34s %9.3f ms", event->service,
                 (double)event->elapsed_us / 1000.0);
        boot_text(s, x + 72, row, line, BOOT_TEXT);
    }
}

static struct toy_surface *boot_overlay(struct rf_core *core)
{
    rf_core_render_frame_begin_v1(core, 0, 0, 0, 1024, 0, 1024);
    core->render_frame.sky_enabled = 0;
    for (int layer = RF_RENDER_LAYER_WORLD;
         layer <= RF_RENDER_LAYER_VIEWMODEL; ++layer)
        if (rf_core_render_frame_enter_layer_v1(
                core, (enum rf_render_layer_v1)layer) < 0)
            return NULL;
    if (rf_core_flush(core) < 0) return NULL;
    return rf_core_begin_screen_overlay(core);
}

static int boot_key(const struct toy_window_events *events, unsigned int key)
{
    for (int i = 0; i < events->key_event_count; i++)
        if (events->key_events[i].pressed && events->key_events[i].key == key)
            return 1;
    return 0;
}

static int boot_any_key(const struct toy_window_events *events)
{
    for (int i = 0; i < events->key_event_count; ++i)
        if (events->key_events[i].pressed) return 1;
    return 0;
}

static void boot_probe_hardware(struct boot_ui *ui, struct rf_core *core)
{
    toy_platform_hardware_query(&ui->hardware);
    rf_core_get_status(core, &ui->core_status);
    memset(&ui->gpu_status, 0, sizeof(ui->gpu_status));
    ui->gpu_ready = 0;
#ifdef TOYC_WINDOWS
    {
        struct rf_gpu_vulkan_context context;
        struct rf_gpu probe;
        struct toy_native_window_handle native;
        struct rf_gpu_native_window gpu_native;
        int64_t started = rf_core_clock_now_us();
        memset(&context, 0, sizeof(context));
        memset(&probe, 0, sizeof(probe));
        memset(&gpu_native, 0, sizeof(gpu_native));
        context.require_graphics = 1;
        if (toy_window_get_native_handle(core->window, &native) > 0) {
            gpu_native.type = native.type;
            gpu_native.window = native.window;
            gpu_native.instance = native.instance;
        }
        if (gpu_native.window &&
            rf_gpu_set_native_window(&rf_gpu_vulkan_backend,
                                     &context, &gpu_native) == 0 &&
            rf_gpu_init(&probe, RF_GPU_POLICY_OPTIONAL,
                        &rf_gpu_vulkan_backend, &context) == 0 &&
            rf_gpu_get_status(&probe, &ui->gpu_status) == 0 &&
            ui->gpu_status.state == RF_GPU_STATE_READY &&
            ui->gpu_status.renderer.native_presentation_v1)
            ui->gpu_ready = 1;
        rf_gpu_shutdown(&probe);
        rf_boot_log_task("boot-manager", "graphics-adapter-probe",
                         ui->gpu_ready ? 0 : 1,
                         rf_core_clock_now_us() - started);
    }
#endif
}

static char boot_character(unsigned int key)
{
    static const struct { unsigned int key; char c; } map[] = {
        {16,'q'},{17,'w'},{18,'e'},{19,'r'},{20,'t'},{21,'y'},
        {22,'u'},{23,'i'},{24,'o'},{25,'p'},{30,'a'},{31,'s'},
        {32,'d'},{33,'f'},{34,'g'},{35,'h'},{36,'j'},{37,'k'},
        {38,'l'},{44,'z'},{45,'x'},{46,'c'},{47,'v'},{48,'b'},
        {49,'n'},{50,'m'},{57,' '},{53,'/'},{52,'.'},{12,'-'}
    };
    if (key >= 2 && key <= 11) return key == 11 ? '0' : (char)('1' + key - 2);
    for (unsigned int i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (map[i].key == key) return map[i].c;
    return 0;
}

/* This is a read-only view of the packaged assets.  Reject any component
 * which could escape the mount, including Windows path separators and drives. */
static int boot_path(struct boot_ui *ui, const char *argument,
                     char *relative, size_t cap)
{
    char joined[384], part[192];
    size_t used = 0;
    int written;
    const char *p;
    if (!argument || !*argument) argument = ".";
    if (*argument == '/') written = snprintf(joined, sizeof(joined),
                                              "%s", argument + 1);
    else if (*ui->cwd) written = snprintf(joined, sizeof(joined),
                                          "%s/%s", ui->cwd, argument);
    else written = snprintf(joined, sizeof(joined), "%s", argument);
    if (written < 0 || written >= (int)sizeof(joined)) return -1;
    relative[0] = 0;
    p = joined;
    while (*p) {
        size_t length = 0;
        while (*p == '/') p++;
        if (!*p) break;
        while (p[length] && p[length] != '/') length++;
        if (length >= sizeof(part)) return -1;
        memcpy(part, p, length); part[length] = 0;
        if (!strcmp(part, ".")) { p += length; continue; }
        if (!strcmp(part, "..")) {
            char *last;
            if (!used) return -1;
            last = strrchr(relative, '/');
            used = last ? (size_t)(last - relative) : 0;
            relative[used] = 0;
        } else {
            for (size_t i = 0; i < length; i++)
                if (part[i] == '\\' || part[i] == ':' || (unsigned char)part[i] < 32)
                    return -1;
            if (used + length + (used ? 1 : 0) >= cap) return -1;
            if (used) relative[used++] = '/';
            memcpy(relative + used, part, length + 1);
            used += length;
        }
        p += length;
    }
    return 0;
}

static void boot_ls(struct boot_ui *ui, const char *argument)
{
    char relative[192];
    char entries[RF_BOOT_LIST_LIMIT][176];
    int count, more;
    if (boot_path(ui, argument, relative, sizeof(relative)) < 0) {
        boot_error_line(ui, "ls: path outside /assets"); return;
    }
    if (rf_boot_files_list(relative, entries, &count, &more) < 0) {
        boot_error_line(ui, "ls: directory unavailable"); return;
    }
    if (!count) boot_line(ui, "(empty)");
    for (int i = 0; i < count; ++i) boot_line(ui, entries[i]);
    if (more) boot_line(ui, "(more entries; use a narrower directory)");
}

static void boot_cat(struct boot_ui *ui, const char *argument)
{
    char relative[192], line[176];
    unsigned char bytes[1024];
    size_t size, at = 0;
    int more, count = 0, n = 0;
    if (!argument || !*argument ||
        boot_path(ui, argument, relative, sizeof(relative)) < 0 ||
        !*relative) { boot_error_line(ui, "cat: valid file path required"); return; }
    if (rf_boot_files_read(relative, bytes, sizeof(bytes), &size, &more) < 0) {
        boot_error_line(ui, "cat: file unavailable"); return;
    }
    while (at < size && count < 10) {
        unsigned char c = bytes[at++];
        if (!c) { boot_error_line(ui, "cat: binary file"); return; }
        if (c == '\n' || n == 90) {
            line[n] = 0; boot_line(ui, line); n = 0; ++count;
            if (c == '\n') continue;
        }
        if (c >= 32 && c < 127) line[n++] = (char)c;
        else if (c == '\t') line[n++] = ' ';
    }
    if (n && count < 10) { line[n] = 0; boot_line(ui, line); }
    if (more || at < size) boot_line(ui, "(output truncated)");
}

static int boot_command(struct boot_ui *ui, struct rf_core *core)
{
    char command[192], *arg, line[416];
    snprintf(command, sizeof(command), "%s", ui->command);
    arg = strchr(command, ' ');
    if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }
    else arg = NULL;
    snprintf(line, sizeof(line), "rf:/assets/%s> %s", ui->cwd, ui->command);
    boot_line(ui, line);
    ui->command[0] = 0; ui->command_length = 0;
    if (!*command) return 0;
    if (!strcmp(command, "help")) {
        boot_line(ui, "COMMAND             ACTION");
        boot_line(ui, "help                Show this command reference");
        boot_line(ui, "ls [path]           List files and directories");
        boot_line(ui, "cd [path]           Change directory; cd / returns to mount root");
        boot_line(ui, "pwd                 Print current directory");
        boot_line(ui, "cat <file>          Preview a text file (bounded output)");
        boot_line(ui, "clear               Clear terminal output");
        boot_line(ui, "devices             Inspect CPU, audio and Vulkan adapter");
        boot_line(ui, "boot [cpu|gpu]      Select a renderer or launch it directly");
        boot_line(ui, "exit                Return to RF Boot Manager");
        boot_line(ui, "");
        boot_line(ui, "FILESYSTEM  Read-only package assets. Paths are relative to /assets.");
        boot_line(ui, "EXAMPLE     ls maps   /   cd maps   /   cat outpost.map");
    } else if (!strcmp(command, "pwd")) {
        snprintf(line, sizeof(line), "/assets/%s", ui->cwd); boot_line(ui, line);
    } else if (!strcmp(command, "ls")) boot_ls(ui, arg);
    else if (!strcmp(command, "cat")) boot_cat(ui, arg);
    else if (!strcmp(command, "cd")) {
        char relative[192];
        int directory = 0;
        if (boot_path(ui, arg ? arg : "/", relative, sizeof(relative)) < 0)
            boot_error_line(ui, "cd: path outside /assets");
        else {
            if (rf_boot_files_stat(relative, &directory) == 0 && directory)
                snprintf(ui->cwd, sizeof(ui->cwd), "%s", relative);
            else boot_error_line(ui, "cd: directory unavailable");
        }
    } else if (!strcmp(command, "clear")) ui->line_count = 0;
    else if (!strcmp(command, "devices")) {
        struct rf_core_status status;
        ui->urgent_output = 1; /* Measured status is immediately readable. */
        if (rf_core_get_status(core, &status) == 0) {
            boot_line(ui, status.renderer_ready ?
                      "CPU software renderer: initialized" :
                      "CPU software renderer: unavailable");
            boot_line(ui, status.audio_ready ?
                      "Audio: initialized" : "Audio: unavailable (optional)");
        }
#ifdef TOYC_WINDOWS
        {
            struct rf_gpu_vulkan_context context;
            struct rf_gpu probe;
            struct rf_gpu_status gpu_status;
            int64_t started = rf_core_clock_now_us();
            char info[176];
            memset(&context, 0, sizeof(context));
            memset(&probe, 0, sizeof(probe));
            memset(&gpu_status, 0, sizeof(gpu_status));
            if (rf_gpu_init(&probe, RF_GPU_POLICY_OPTIONAL,
                            &rf_gpu_vulkan_backend, &context) == 0 &&
                rf_gpu_get_status(&probe, &gpu_status) == 0 &&
                gpu_status.state == RF_GPU_STATE_READY) {
                snprintf(info, sizeof(info), "Vulkan adapter: %.130s",
                         gpu_status.info.adapter_name);
                boot_line(ui, info);
                boot_line(ui, "GPU Scene native present: checked on selection");
            } else boot_line(ui, "Vulkan adapter: unavailable");
            rf_gpu_shutdown(&probe);
            rf_boot_log_task("devices", "vulkan-adapter-probe",
                             gpu_status.state == RF_GPU_STATE_READY ? 0 : 1,
                             rf_core_clock_now_us() - started);
        }
#else
        boot_line(ui, "GPU Scene: unavailable on this platform");
#endif
        boot_line(ui, "GPU upload: not started; allocation/budget: not queryable");
    } else if (!strcmp(command, "exit")) { ui->screen = BOOT_MANAGER; ui->selected = 2; }
    else if (!strcmp(command, "boot")) {
        if (arg && (!strcmp(arg, "cpu") || !strcmp(arg, "--renderer cpu"))) {
            ui->renderer = RF_CORE_RENDERER_CPU; return 1;
        }
        if (arg && (!strcmp(arg, "gpu") || !strcmp(arg, "--renderer gpu"))) {
            ui->renderer = RF_CORE_RENDERER_GPU_SCENE; return 1;
        }
        ui->screen = BOOT_SHELL_RENDERER; ui->selected = 0;
    } else {
        snprintf(line, sizeof(line), "Unknown command: %.120s", command);
        boot_error_line(ui, line);
        boot_line(ui, "Type help to list commands, or boot to start Outpost.");
    }
    return 0;
}

/* Graphical controls share rectangles with their pointer hit tests. */
static const struct boot_rect boot_controls[] = {
    {568, 206, 616, 126}, {568, 350, 616, 126},
    {880, 554, 304, 56}, {568, 554, 264, 56}
};

static int boot_hit(const struct toy_surface *s, int px, int py)
{
    for (int i = 0; i < 4; ++i)
        if (boot_hit_rect(s, &boot_controls[i], px, py)) return i;
    return -1;
}

static void boot_draw(struct toy_surface *s, const struct boot_ui *ui,
                       struct rf_boot_canvas *canvas)
{
    char line[416];
    if (ui->screen == BOOT_MANAGER) {
        static const char *names[5] = {
            "START RASTERFALL", "RF WORKBENCH", "RF SHELL",
            "DIAGNOSTICS", "POWER OFF"
        };
        static const char *tags[5] = {
            "AUTO", "GRAPHICAL", "TERMINAL", "SYSTEM", "EXIT"
        };
        static const char *details[5] = {
            "Find the best graphics adapter; use CPU if unavailable.",
            "Graphical environment  /  choose CPU or GPU Scene.",
            "Terminal environment  /  inspect files and launch manually.",
            "Inspect hardware and RF service readiness.",
            "Close Rasterfall and return to the operating system."
        };
        boot_chrome(s, "RF BOOT MANAGER", "KEYBOARD ONLY");
        boot_text(s, 192, 113, "SYSTEM ENTRY  /  01", BOOT_CYAN);
        boot_type(s, 192, 143, "RF BOOT MANAGER", BOOT_TEXT, 2, 15);
        boot_text(s, 192, 192, "Select an environment or start Rasterfall.", BOOT_DIM);
        boot_rule(s, 192, 220, 896, BOOT_EDGE);
        for (int i = 0; i < 5; ++i) {
            int y = 238 + i * 67;
            int active = i == ui->selected;
            boot_box(s, 192, y, 896, 58,
                     active ? BOOT_TEXT : BOOT_PANEL,
                     active ? BOOT_TEXT : BOOT_EDGE);
            if (!active) boot_box(s, 192, y, 4, 58, BOOT_CYAN, BOOT_CYAN);
            snprintf(line, sizeof(line), "%02d", i + 1);
            boot_type(s, 214, y + 17, line, active ? BOOT_BG : BOOT_CYAN, 2, 2);
            boot_type(s, 276, y + 18, names[i], active ? BOOT_BG : BOOT_TEXT, 2, 18);
            boot_text(s, 966, y + 21, tags[i], active ? BOOT_BG : BOOT_DIM);
        }
        boot_text(s, 192, 593, details[ui->selected], BOOT_TEXT);
        if (ui->auto_seconds > 0)
            snprintf(line, sizeof(line), "AUTO START IN %d  /  ANY KEY TO HOLD", ui->auto_seconds);
        else snprintf(line, sizeof(line), "UP / DOWN  SELECT    ENTER  OPEN    1-5  DIRECT");
        boot_text(s, 192, 620, line, BOOT_CYAN);
    } else if (ui->screen == BOOT_SHELL || ui->screen == BOOT_SHELL_RENDERER) {
        boot_chrome(s, "RF SHELL / TERMINAL ENVIRONMENT", "KEYBOARD ONLY");
        boot_type(s, 48, 100, "RF SHELL", BOOT_TEXT, 2, 8);
        boot_text(s, 48, 146, "FILESYSTEM  /assets   [READ ONLY]", BOOT_DIM);
        boot_text(s, 728, 146, "help  Commands     boot  Start Outpost", BOOT_CYAN);
        boot_rule(s, 48, 178, 1184, BOOT_EDGE);
        int visible = ui->screen == BOOT_SHELL_RENDERER ? 13 : BOOT_LINES;
        int start = ui->line_count > visible ? ui->line_count - visible : 0;
        for (int i = start; i < ui->line_count; ++i) {
            const char *value = ui->lines[i];
            uint32_t color = !strncmp(value, "rf:", 3) ? BOOT_CYAN : BOOT_TEXT;
            boot_type(s, 56, 195 + (i - start) * 22, value, color, 1, 144);
            /* Command echo and editable input always appear immediately. */
            if (strncmp(value, "rf:", 3))
                rf_boot_canvas_region(canvas, 56, 195 + (i - start) * 22, 1152, 16);
        }
        if (ui->screen == BOOT_SHELL_RENDERER) {
            boot_rule(s, 48, 499, 1184, BOOT_EDGE);
            boot_text(s, 56, 516, "BOOT / OUTPOST     Select renderer", BOOT_CYAN);
            boot_text(s, 56, 550, ui->selected ? "  [1] CPU Software" : "> [1] CPU Software", BOOT_TEXT);
            boot_text(s, 432, 550, ui->selected ? "> [2] GPU Scene" : "  [2] GPU Scene", BOOT_TEXT);
            boot_text(s, 56, 586, ui->selected ?
                      "Vulkan / native present verified at startup" :
                      "Software rasterization / CPU", BOOT_DIM);
        } else {
            boot_rule(s, 48, 597, 1184, BOOT_EDGE);
            boot_text(s, 56, 612, ">", BOOT_CYAN);
            snprintf(line, sizeof(line), "rf:/assets/%s> %s", ui->cwd, ui->command);
            size_t length = strlen(line);
            const char *tail = length > 139 ? line + length - 139 : line;
            boot_type(s, 80, 612, tail, BOOT_CYAN, 1, 139);
            boot_box(s, 80 + (int)strlen(tail) * 8, 612, 8, 16, BOOT_CYAN, BOOT_CYAN);
        }
        boot_text(s, 744, 673, ui->screen == BOOT_SHELL_RENDERER ?
                  "1 / 2  Select   ENTER  Boot   ESC  Shell" :
                  "ENTER  Run command    ESC  Boot Manager", BOOT_DIM);
    } else if (ui->screen == BOOT_DIAGNOSTICS) {
        const struct rf_gpu_backend_info *gpu = &ui->gpu_status.info;
        boot_chrome(s, "DIAGNOSTICS", "KEYBOARD ONLY");
        boot_text(s, 96, 114, "SYSTEM / HARDWARE REPORT", BOOT_CYAN);
        boot_type(s, 96, 149, "DIAGNOSTICS", BOOT_TEXT, 2, 11);
        boot_text(s, 96, 205, "HOST", BOOT_CYAN);
        boot_rule(s, 96, 232, 1088, BOOT_EDGE);
        if (ui->hardware.physical_cores)
            snprintf(line, sizeof(line), "PHYSICAL CPU CORES    %d",
                     ui->hardware.physical_cores);
        else snprintf(line, sizeof(line), "PHYSICAL CPU CORES    NOT QUERYABLE");
        boot_text(s, 96, 253, line, ui->hardware.physical_cores ? BOOT_TEXT : BOOT_AMBER);
        if (ui->hardware.memory_mib)
            snprintf(line, sizeof(line), "INSTALLED MEMORY      %llu MiB",
                     ui->hardware.memory_mib);
        else snprintf(line, sizeof(line), "INSTALLED MEMORY      NOT QUERYABLE");
        boot_text(s, 96, 281, line, ui->hardware.memory_mib ? BOOT_TEXT : BOOT_AMBER);
        boot_text(s, 96, 329, "GRAPHICS", BOOT_CYAN);
        boot_rule(s, 96, 356, 1088, BOOT_EDGE);
        boot_text(s, 96, 377, ui->gpu_ready ?
                  "GPU SCENE             AVAILABLE" :
                  "GPU SCENE             UNAVAILABLE / CPU FALLBACK",
                  ui->gpu_ready ? FIRMWARE_GREEN : BOOT_AMBER);
        snprintf(line, sizeof(line), "VULKAN ADAPTER        %.96s",
                 ui->gpu_status.state == RF_GPU_STATE_READY ? gpu->adapter_name : "unavailable");
        boot_text(s, 96, 405, line, ui->gpu_ready ? BOOT_TEXT : BOOT_AMBER);
        if (ui->gpu_status.state == RF_GPU_STATE_READY) {
            snprintf(line, sizeof(line), "DEVICE ID             %04X:%04X  /  ADAPTER %u",
                     gpu->vendor_id, gpu->device_id, gpu->capabilities.adapter_index);
            boot_text(s, 96, 433, line, BOOT_DIM);
        }
        boot_text(s, 96, 481, "RF SERVICES", BOOT_CYAN);
        boot_rule(s, 96, 508, 1088, BOOT_EDGE);
        boot_text(s, 96, 529, ui->core_status.renderer_ready ?
                  "CPU RENDERER          READY" : "CPU RENDERER          UNAVAILABLE",
                  ui->core_status.renderer_ready ? FIRMWARE_GREEN : BOOT_AMBER);
        boot_text(s, 96, 557, ui->core_status.audio_ready ?
                  "AUDIO                 READY" : "AUDIO                 UNAVAILABLE (OPTIONAL)",
                  ui->core_status.audio_ready ? FIRMWARE_GREEN : BOOT_AMBER);
        boot_text(s, 96, 585, ui->core_status.filesystem_ready ?
                  "FILESYSTEM            READY" : "FILESYSTEM            UNAVAILABLE",
                  ui->core_status.filesystem_ready ? FIRMWARE_GREEN : BOOT_AMBER);
        boot_text(s, 96, 619, "ESC  Return to RF Boot Manager", BOOT_CYAN);
    } else {
        rf_boot_canvas_region(canvas, 48, 260, 432, 345);
        boot_chrome(s, "RF WORKBENCH / GRAPHICAL ENVIRONMENT", "MOUSE + KEYBOARD");
        boot_text(s, 48, 111, "DESTINATION / 01", BOOT_CYAN);
        boot_type(s, 48, 146, "OUTPOST", BOOT_TEXT, 3, 7);
        boot_text(s, 48, 207, "Choose how your world is rendered.", BOOT_DIM);
        boot_chip(s, 178, 290, ui->selected);
        boot_text(s, 48, 500, ui->selected ? "02 / GPU SCENE" : "01 / CPU SOFTWARE", BOOT_CYAN);
        boot_text(s, 48, 530, ui->selected ? "Vulkan graphics / native presentation" :
                  "Software rasterization / host processor", BOOT_TEXT);
        boot_text(s, 48, 566, ui->selected ? "Device capability is checked on launch." :
                  "Uses the CPU to draw the world.", BOOT_DIM);
        boot_text(s, 48, 591, "Renderer can also be changed in game.", BOOT_DIM);
        boot_text(s, 568, 144, "RENDERING BACKEND", BOOT_CYAN);
        boot_text(s, 568, 173, "Select a backend, then start Outpost.", BOOT_DIM);
        for (int i = 0; i < 2; ++i) {
            const struct boot_rect *r = &boot_controls[i];
            int active = i == ui->selected;
            boot_box(s, r->x, r->y, r->w, r->h, active ? 0x142D31u : BOOT_PANEL,
                     active || ui->hover == i ? BOOT_CYAN : BOOT_EDGE);
            if (active) boot_box(s, r->x, r->y, 4, r->h, BOOT_CYAN, BOOT_CYAN);
            boot_text(s, r->x + 24, r->y + 20, i ? "02" : "01", BOOT_DIM);
            boot_type(s, r->x + 64, r->y + 19, i ? "GPU SCENE" : "CPU SOFTWARE", BOOT_TEXT, 2, 12);
            boot_text(s, r->x + 488, r->y + 26, active ? "[SELECTED]" : "[        ]", active ? BOOT_CYAN : BOOT_DIM);
            boot_text(s, r->x + 64, r->y + 69, i ? "Vulkan / hardware graphics" : "CPU / software graphics", BOOT_TEXT);
            boot_text(s, r->x + 64, r->y + 94, i ? "Availability verified on launch" : "Current boot display uses this backend", BOOT_DIM);
        }
        boot_text(s, 568, 503, "UP / DOWN  Select                    ENTER  Start", BOOT_DIM);
        for (int i = 2; i < 4; ++i) {
            const struct boot_rect *r = &boot_controls[i];
            uint32_t fill = i == 2 ? (ui->hover == i ? BOOT_TEXT : BOOT_CYAN) : BOOT_BG;
            boot_box(s, r->x, r->y, r->w, r->h, fill,
                     i == 2 || ui->hover == i ? BOOT_CYAN : BOOT_EDGE);
            boot_text(s, r->x + 32, r->y + 20, i == 2 ? "START OUTPOST    >" : "<  BOOT MANAGER",
                      i == 2 ? BOOT_BG : BOOT_TEXT);
        }
        boot_text(s, 936, 673, "SELECT / CONFIRM / LAUNCH", BOOT_DIM);
    }
    if (*ui->error) {
        boot_box(s, 48, 662, 1184, 34, BOOT_BG, BOOT_AMBER);
        snprintf(line, sizeof(line), "[FAIL] %s", ui->error);
        boot_type(s, 56, 671, line, BOOT_AMBER, 1, 145);
    }
}

/* Draw directly into the window before the Core renderer is available. */
static int firmware_draw(struct rf_core *core,
                         const struct rf_boot_journal *journal,
                         const char *current_task, int seconds_left,
                         struct rf_boot_canvas *canvas)
{
    struct toy_surface surface;
    char line[160];
    int ready;
    if (!core || !core->window) return -1;
    ready = toy_window_begin_frame(core->window, &surface);
    if (ready < 0) return -1;
    if (ready > 0) {
        rf_boot_canvas_begin(canvas);
        fb_fill_rect((unsigned char *)surface.pixels, 0, 0,
                     surface.width, surface.height, BOOT_BG, surface.stride);
        boot_type(&surface, 48, 48, "RF PLATFORM FIRMWARE", BOOT_TEXT, 2, 24);
        boot_type(&surface, 48, 102, "Initializing platform services...", BOOT_DIM, 1, 50);
        boot_type(&surface, 48, 167, "STATUS", BOOT_DIM, 1, 8);
        boot_type(&surface, 200, 167, "MODULE", BOOT_TEXT, 1, 12);
        boot_type(&surface, 1020, 167, "TIME", BOOT_DIM, 1, 8);
        if (journal) {
            int row = 0;
            for (int i = 0; i < journal->count; ++i) {
                const struct rf_boot_event *event = &journal->events[i];
                int y;
                if (!strncmp(event->service, "gpu-", 4)) continue;
                y = FIRMWARE_FIRST_ROW_Y + row++ * FIRMWARE_ROW_STEP;
                if (event->result >= 0)
                    rf_boot_canvas_region(canvas, 48, y, 1184, 32);
                boot_type(&surface, 48, y, event->result < 0 ? "[FAIL]" :
                          event->result ? "[ N/A]" : "[ OK ]",
                          event->result ? BOOT_AMBER : FIRMWARE_GREEN, 2, 6);
                boot_type(&surface, 200, y, event->service, BOOT_TEXT, 2, 32);
                snprintf(line, sizeof(line), "%9.2fms",
                         (double)event->elapsed_us / 1000.0);
                boot_type(&surface, 1016, y, line, BOOT_DIM, 2, 13);
            }
        }
        if (journal && journal->started_us && journal->completed_us) {
            int64_t total_us = journal->completed_us - journal->started_us;
            /* The rule and summary appear only after every Core service finishes. */
            boot_rule(&surface, 48,
                      FIRMWARE_FIRST_ROW_Y + 5 * FIRMWARE_ROW_STEP + 15,
                      1184, BOOT_EDGE);
            boot_type(&surface, 48, FIRMWARE_FIRST_ROW_Y + 6 * FIRMWARE_ROW_STEP,
                      "[TOTAL]", FIRMWARE_BLUE, 2, 7);
            boot_type(&surface, 200, FIRMWARE_FIRST_ROW_Y + 6 * FIRMWARE_ROW_STEP,
                      "CORE INITIALIZATION",
                      FIRMWARE_BLUE, 2, 32);
            snprintf(line, sizeof(line), "%9.2fms",
                     (double)total_us / 1000.0);
            boot_type(&surface, 1016, FIRMWARE_FIRST_ROW_Y + 6 * FIRMWARE_ROW_STEP,
                      line, FIRMWARE_BLUE, 2, 13);
        }
        if (current_task) {
            snprintf(line, sizeof(line), "[ WORK ] %s", current_task);
            boot_type(&surface, 48, FIRMWARE_FIRST_ROW_Y + 7 * FIRMWARE_ROW_STEP,
                      line, BOOT_TEXT, 2, 70);
        } else {
            boot_type(&surface, 48, FIRMWARE_FIRST_ROW_Y + 7 * FIRMWARE_ROW_STEP,
                      "[ OK ] CORE INITIALIZATION COMPLETE",
                      FIRMWARE_GREEN, 2, 40);
            snprintf(line, sizeof(line), "BOOT MANAGER IN %d SECOND%s  |  ENTER TO CONTINUE NOW",
                     seconds_left, seconds_left == 1 ? "" : "S");
            boot_type(&surface, 48, FIRMWARE_FIRST_ROW_Y + 8 * FIRMWARE_ROW_STEP,
                      line, BOOT_CYAN, 2, 72);
        }
        rf_boot_canvas_compose(canvas, &surface, BOOT_FIRMWARE,
                               rf_core_time_us(core), 0);
        if (toy_window_present(core->window) < 0) return -1;
    }
    return 0;
}

int rf_boot_init_display(void *context, struct rf_core *core,
                         const char *current_task)
{
    struct toy_window_events events;
    struct rf_boot_journal *journal = (struct rf_boot_journal *)context;
    if (!current_task && journal && !journal->completed_us)
        journal->completed_us = rf_core_clock_now_us();
    /* Renderer choice happens after firmware; keep GPU work out of this view. */
    if (!(current_task && !strncmp(current_task, "gpu-", 4)) &&
        firmware_draw(core, journal, current_task, 3, NULL) < 0) return -1;
    if (toy_window_poll(core->window, &events, 0) < 0) return -1;
    if (events.close_requested) return -1;
    if (!current_task) boot_marker("RF-BOOT stage=core status=ready");
    return 0;
}

static int boot_run(struct rf_core *core, struct rf_boot_result *result,
                     const struct rf_boot_journal *journal, const char *error,
                     struct rf_boot_canvas *canvas)
{
    struct boot_ui ui;
    int ready;
    int64_t completed_at, auto_until;
    char line[176];
    if (!core || !result) return -1;
    memset(&ui, 0, sizeof(ui));
    ui.hover = -1;
    if (error) snprintf(ui.error, sizeof(ui.error), "%s", error);
    /* The measured completion screen counts down; Enter skips the wait. */
    if (!error) {
        completed_at = rf_core_time_us(core);
        if (firmware_draw(core, journal, NULL, 3, canvas) < 0) return -1;
        while (rf_core_time_us(core) - completed_at < 3000000) {
            int seconds_left;
            if (rf_core_poll_events_timeout(core, 16) < 0) return -1;
            if (rf_core_should_exit(core)) return 0;
            if (boot_key(rf_core_events(core), BOOT_KEY_ENTER)) break;
            seconds_left = 3 - (int)((rf_core_time_us(core) - completed_at) / 1000000);
            if (seconds_left > 0 &&
                firmware_draw(core, journal, NULL, seconds_left, canvas) < 0) return -1;
        }
    }
    auto_until = error ? 0 : rf_core_time_us(core) + BOOT_AUTO_US;
    boot_marker("RF-BOOT stage=menu status=ready");
    for (;;) {
        struct toy_window_events *events = rf_core_events(core);
        int previous_screen = ui.screen;
        int finish_scan;
        ui.urgent_output = 0;
        if (rf_core_poll_events_timeout(core, 16) < 0) {
            __fprintf(2, "RF-BOOT menu event poll failed\n"); return -1;
        }
        if (rf_core_should_exit(core)) {
            boot_marker("RF-BOOT stage=menu status=closed"); return 0;
        }
        finish_scan = canvas->sweeping && (boot_any_key(events) || events->button_pressed);
        if (ui.screen == BOOT_MANAGER) {
            int activate = boot_key(events, BOOT_KEY_ENTER);
            if (auto_until && (boot_any_key(events) || events->button_pressed))
                auto_until = 0;
            if (boot_key(events, BOOT_KEY_UP))
                ui.selected = (ui.selected + 4) % 5;
            if (boot_key(events, BOOT_KEY_DOWN))
                ui.selected = (ui.selected + 1) % 5;
            if (boot_key(events, BOOT_KEY_1)) { ui.selected = 0; activate = 1; }
            if (boot_key(events, BOOT_KEY_2)) { ui.selected = 1; activate = 1; }
            if (boot_key(events, BOOT_KEY_3)) { ui.selected = 2; activate = 1; }
            if (boot_key(events, BOOT_KEY_4)) { ui.selected = 3; activate = 1; }
            if (boot_key(events, BOOT_KEY_5)) { ui.selected = 4; activate = 1; }
            if (auto_until) {
                int64_t left = auto_until - rf_core_time_us(core);
                ui.auto_seconds = left > 0 ? (int)((left + 999999) / 1000000) : 0;
                if (left <= 0) { ui.selected = 0; activate = 1; auto_until = 0; }
            } else ui.auto_seconds = 0;
            if (activate) {
                if (ui.selected == 0) {
                    boot_probe_hardware(&ui, core);
                    ui.renderer = ui.gpu_ready ? RF_CORE_RENDERER_GPU_SCENE : RF_CORE_RENDERER_CPU;
                    ui.automatic = 1;
                    ui.screen = BOOT_WORKBENCH; /* Shared graphical progress. */
                    goto selected;
                }
                if (ui.selected == 1) {
                    ui.screen = BOOT_WORKBENCH; ui.selected = 0;
                    boot_marker("RF-BOOT stage=workbench status=ready");
                } else if (ui.selected == 2) {
                    ui.screen = BOOT_SHELL; ui.selected = 0;
                    boot_line(&ui, "[ OK ] RF Shell terminal environment ready");
                    boot_line(&ui, "Type help for commands; boot enters Outpost.");
                    boot_marker("RF-BOOT stage=shell status=ready");
                } else if (ui.selected == 3) {
                    boot_probe_hardware(&ui, core);
                    ui.screen = BOOT_DIAGNOSTICS;
                    boot_marker("RF-BOOT stage=diagnostics status=ready");
                } else {
                    boot_marker("RF-BOOT stage=power-off status=selected");
                    return 0;
                }
            }
        } else if (ui.screen == BOOT_SHELL) {
            for (int i = 0; i < events->key_event_count; i++) {
                unsigned int key = events->key_events[i].key;
                char c = 0;
                if (!events->key_events[i].pressed) continue;
                if (key == BOOT_KEY_ENTER) {
                    if (boot_command(&ui, core)) goto selected;
                } else if (key == BOOT_KEY_BACKSPACE) {
                    if (ui.command_length) ui.command[--ui.command_length] = 0;
                } else if (key == BOOT_KEY_ESC) { ui.screen = BOOT_MANAGER; ui.selected = 2; }
                else {
                    c = boot_character(key);
                    if (c && ui.command_length + 1 < (int)sizeof(ui.command)) {
                        ui.command[ui.command_length++] = c;
                        ui.command[ui.command_length] = 0;
                    }
                }
            }
        } else if (ui.screen == BOOT_SHELL_RENDERER) {
            if (boot_key(events, BOOT_KEY_ESC)) ui.screen = BOOT_SHELL;
            if (boot_key(events, BOOT_KEY_UP) || boot_key(events, BOOT_KEY_DOWN))
                ui.selected = !ui.selected;
            if (boot_key(events, BOOT_KEY_1)) ui.selected = 0;
            if (boot_key(events, BOOT_KEY_2)) ui.selected = 1;
            if (boot_key(events, BOOT_KEY_ENTER)) {
                ui.renderer = ui.selected; goto selected;
            }
        } else if (ui.screen == BOOT_DIAGNOSTICS) {
            if (boot_key(events, BOOT_KEY_ESC)) {
                ui.screen = BOOT_MANAGER; ui.selected = 3;
            }
        } else {
            int click = events->button_pressed && events->button == BOOT_BUTTON_LEFT;
            ui.hover = boot_hit(&core->surface, events->pointer_x, events->pointer_y);
            if (boot_key(events, BOOT_KEY_ESC) ||
                (click && ui.hover == 3)) {
                ui.screen = BOOT_MANAGER; ui.selected = 1;
            } else {
                if (boot_key(events, BOOT_KEY_UP)) ui.selected = 0;
                if (boot_key(events, BOOT_KEY_DOWN)) ui.selected = 1;
                if (click && ui.hover >= 0 && ui.hover < 2) ui.selected = ui.hover;
                if (boot_key(events, BOOT_KEY_ENTER) ||
                    (click && ui.hover == 2)) {
                    ui.renderer = ui.selected; goto selected;
                }
            }
        }
        ready = rf_core_begin_frame(core, BOOT_BG);
        if (ready < 0) {
            __fprintf(2, "RF-BOOT menu begin frame failed\n"); return -1;
        }
        if (ready > 0) {
            struct toy_surface *s = boot_overlay(core);
            if (!s) {
                __fprintf(2, "RF-BOOT menu overlay failed\n"); return -1;
            }
            rf_boot_canvas_begin(canvas);
            boot_draw(s, &ui, canvas);
            rf_boot_canvas_compose(canvas, s, ui.screen, rf_core_time_us(core),
                                   *ui.error || ui.urgent_output ||
                                   (finish_scan && previous_screen == ui.screen));
            if (rf_core_present_boot_frame(core) < 0) {
                __fprintf(2, "RF-BOOT menu present failed\n"); return -1;
            }
        }
    }
selected:
    result->renderer = ui.renderer;
    result->graphical = ui.screen == BOOT_WORKBENCH;
    result->automatic = ui.automatic;
    snprintf(line, sizeof(line),
             "RF-BOOT stage=renderer status=selected backend=%s",
             ui.renderer ? "gpu-scene" : "cpu");
    boot_marker(line);
    return 1;
}

int rf_boot_run(struct rf_core *core, struct rf_boot_result *result,
                const struct rf_boot_journal *journal, const char *error)
{
    struct rf_boot_canvas canvas;
    int status;
    memset(&canvas, 0, sizeof(canvas));
    status = boot_run(core, result, journal, error, &canvas);
    rf_boot_canvas_destroy(&canvas);
    return status;
}

int rf_boot_progress(struct rf_core *core, int graphical,
                     const struct rf_boot_journal *journal,
                     const char *current_task, int completed, int total)
{
    struct toy_surface *s;
    char line[192];
    int ready;
    if (!core || !current_task || !journal || total <= 0) return -1;
    if (rf_core_poll_events(core) < 0 || rf_core_should_exit(core)) return -1;
    ready = rf_core_begin_frame(core, BOOT_BG);
    if (ready <= 0) return ready;
    s = boot_overlay(core);
    if (!s) return -1;
    if (completed < 0) completed = 0;
    if (completed > total) completed = total;
    boot_chrome(s, graphical ? "RF WORKBENCH / STARTUP" : "RF SHELL / STARTUP",
                "LIVE TASK STATUS");
    boot_text(s, 48, 110, "DESTINATION / OUTPOST", BOOT_CYAN);
    boot_type(s, 48, 145, "WORLD STARTUP", BOOT_TEXT, 2, 13);
    snprintf(line, sizeof(line), "%02d / %02d", completed, total);
    boot_type(s, 48, 232, line, BOOT_TEXT, 3, 12);
    boot_text(s, 48, 293, "STARTUP STAGES COMPLETE", BOOT_DIM);
    boot_text(s, 48, 366, "[ WORK ] CURRENT TASK", BOOT_AMBER);
    boot_type(s, 48, 399, current_task, BOOT_TEXT, 1, 54);
    if (graphical) {
        boot_box(s, 48, 328, 432, 8, BOOT_PANEL, BOOT_PANEL);
        if (completed > 0)
            boot_box(s, 48, 328, 432 * completed / total, 8, BOOT_CYAN, BOOT_CYAN);
    } else {
        char bar[40];
        int cells = 36 * completed / total;
        for (int i = 0; i < 36; ++i) bar[i] = i < cells ? '#' : '.';
        bar[36] = 0;
        snprintf(line, sizeof(line), "[%s]", bar);
        boot_text(s, 48, 328, line, BOOT_CYAN);
    }
    boot_text(s, 48, 518, "Progress counts completed startup tasks.", BOOT_DIM);
    boot_text(s, 48, 546, "GPU upload / allocation / driver budget:", BOOT_DIM);
    boot_text(s, 48, 570, "not queryable by this boot interface.", BOOT_DIM);
    boot_text(s, 608, 110, "INITIALIZATION JOURNAL / LATEST EVENTS", BOOT_CYAN);
    boot_journal_draw(s, journal, 608, 150, 16);
    boot_text(s, 848, 673, "ELAPSED / MONOTONIC CLOCK", BOOT_DIM);
    return rf_core_present_boot_frame(core);
}
