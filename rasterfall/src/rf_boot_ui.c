#include "core.h"
#include "tlibc_everything.h"
#include "rf_boot_ui.h"
#include "rf_boot_files.h"
#ifdef TOYC_WINDOWS
#include "rf_gpu_vulkan_backend.h"
#endif
#include "fb_draw.h"
#include "fb_font.h"
#include <stdio.h>

#define BOOT_BG 0x080D13u
#define BOOT_PANEL 0x111C26u
#define BOOT_TEXT 0xD5E2E7u
#define BOOT_DIM 0x78939Du
#define BOOT_CYAN 0x70E6EEu
#define BOOT_AMBER 0xD9A955u
#define BOOT_KEY_ESC 1
#define BOOT_KEY_1 2
#define BOOT_KEY_2 3
#define BOOT_KEY_BACKSPACE 14
#define BOOT_KEY_ENTER 28
#define BOOT_KEY_UP 103
#define BOOT_KEY_DOWN 108
#define BOOT_BUTTON_LEFT 0x110
#define BOOT_LINES 18

struct boot_ui {
    int screen; /* 0 init, 1 terminal, 2 graphical, 3 renderer prompt */
    int selected;
    int renderer;
    char cwd[192];
    char command[192];
    int command_length;
    char lines[BOOT_LINES][176];
    int line_count;
    char error[160];
};

void rf_boot_record_event(void *context, const char *service, int result,
                          int64_t elapsed_us)
{
    struct rf_boot_journal *journal = (struct rf_boot_journal *)context;
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
    if (ui->line_count == BOOT_LINES) {
        memmove(ui->lines, ui->lines + 1,
                sizeof(ui->lines[0]) * (BOOT_LINES - 1));
        ui->line_count--;
    }
    snprintf(ui->lines[ui->line_count++], sizeof(ui->lines[0]),
             "%.*s", (int)sizeof(ui->lines[0]) - 1, line);
}

static void boot_text(struct toy_surface *s, int x, int y,
                      const char *value, uint32_t color)
{
    char clipped[192];
    int cells;
    size_t length;
    if (!s || !s->pixels || !value) return;
    x = x * s->width / 1280;
    y = y * s->height / 720;
    if (x < 0 || y < 0 || y + FB_FONT_H >= s->height) return;
    cells = (s->width - x - 1) / FB_FONT_W;
    if (cells <= 0) return;
    length = strlen(value);
    if (length > (size_t)cells) length = (size_t)cells;
    if (length >= sizeof(clipped)) length = sizeof(clipped) - 1;
    memcpy(clipped, value, length);
    clipped[length] = 0;
    fb_draw_string((unsigned char *)s->pixels, x, y, clipped, color,
                   s->stride);
}

static void boot_box(struct toy_surface *s, int x, int y, int w, int h,
                     uint32_t fill, uint32_t edge)
{
    if (!s || !s->pixels) return;
    x = x * s->width / 1280;
    y = y * s->height / 720;
    w = w * s->width / 1280;
    h = h * s->height / 720;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
        x + w > s->width || y + h > s->height)
        return;
    fb_fill_rect((unsigned char *)s->pixels, x, y, w, h, fill, s->stride);
    fb_draw_rect((unsigned char *)s->pixels, x, y, w, h, edge, s->stride);
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
        boot_line(ui, "ls: path outside /assets"); return;
    }
    if (rf_boot_files_list(relative, entries, &count, &more) < 0) {
        boot_line(ui, "ls: directory unavailable"); return;
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
        !*relative) { boot_line(ui, "cat: valid file path required"); return; }
    if (rf_boot_files_read(relative, bytes, sizeof(bytes), &size, &more) < 0) {
        boot_line(ui, "cat: file unavailable"); return;
    }
    while (at < size && count < 10) {
        unsigned char c = bytes[at++];
        if (!c) { boot_line(ui, "cat: binary file"); return; }
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
    char command[192], *arg, line[192];
    snprintf(command, sizeof(command), "%s", ui->command);
    arg = strchr(command, ' ');
    if (arg) { *arg++ = 0; while (*arg == ' ') arg++; }
    else arg = NULL;
    snprintf(line, sizeof(line), "rf:/assets/%s> %s", ui->cwd, ui->command);
    boot_line(ui, line);
    ui->command[0] = 0; ui->command_length = 0;
    if (!*command) return 0;
    if (!strcmp(command, "help")) {
        boot_line(ui, "help  ls [path]  cd [path]  pwd  cat <file>");
        boot_line(ui, "clear  devices  boot [cpu|gpu]  exit");
        boot_line(ui, "Filesystem: read-only /assets package mount");
    } else if (!strcmp(command, "pwd")) {
        snprintf(line, sizeof(line), "/assets/%s", ui->cwd); boot_line(ui, line);
    } else if (!strcmp(command, "ls")) boot_ls(ui, arg);
    else if (!strcmp(command, "cat")) boot_cat(ui, arg);
    else if (!strcmp(command, "cd")) {
        char relative[192];
        int directory = 0;
        if (boot_path(ui, arg ? arg : "/", relative, sizeof(relative)) < 0)
            boot_line(ui, "cd: path outside /assets");
        else {
            if (rf_boot_files_stat(relative, &directory) == 0 && directory)
                snprintf(ui->cwd, sizeof(ui->cwd), "%s", relative);
            else boot_line(ui, "cd: directory unavailable");
        }
    } else if (!strcmp(command, "clear")) ui->line_count = 0;
    else if (!strcmp(command, "devices")) {
        struct rf_core_status status;
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
    } else if (!strcmp(command, "exit")) { ui->screen = 0; ui->selected = 0; }
    else if (!strcmp(command, "boot")) {
        if (arg && (!strcmp(arg, "cpu") || !strcmp(arg, "--renderer cpu"))) {
            ui->renderer = RF_CORE_RENDERER_CPU; return 1;
        }
        if (arg && (!strcmp(arg, "gpu") || !strcmp(arg, "--renderer gpu"))) {
            ui->renderer = RF_CORE_RENDERER_GPU_SCENE; return 1;
        }
        ui->screen = 3; ui->selected = 0;
    } else boot_line(ui, "unknown command; type help");
    return 0;
}

static void boot_draw(struct toy_surface *s, const struct boot_ui *ui)
{
    char line[256];
    if (ui->screen == 0) {
        boot_text(s, 480, 120, "RF INIT", BOOT_CYAN);
        boot_text(s, 420, 150, "Rasterfall Initialization Manager", BOOT_TEXT);
        boot_box(s, 320, 220, 640, 230, BOOT_BG, BOOT_DIM);
        for (int i = 0; i < 2; i++) {
            const char *name = i ? "Graphical Boot" : "Terminal Environment";
            uint32_t fill = i == ui->selected ? BOOT_TEXT : BOOT_BG;
            uint32_t ink = i == ui->selected ? BOOT_BG : BOOT_TEXT;
            boot_box(s, 345, 263 + i * 66, 590, 48, fill, BOOT_BG);
            snprintf(line, sizeof(line), ">  %s", name);
            boot_text(s, 365, 279 + i * 66, line, ink);
        }
        boot_text(s, 340, 482, ui->selected ?
                  "Mouse-ready renderer selection and world loading" :
                  "Interactive shell and guided world startup", BOOT_DIM);
        boot_text(s, 420, 565, "UP DOWN  SELECT        ENTER  BOOT", BOOT_DIM);
    } else if (ui->screen == 1 || ui->screen == 3) {
        boot_text(s, 56, 45, "RF TERMINAL / Rasterfall Core", BOOT_CYAN);
        boot_text(s, 56, 71, "Read-only filesystem: /assets    Type help for commands", BOOT_DIM);
        for (int i = 0; i < ui->line_count; i++)
            boot_text(s, 56, 110 + i * 24, ui->lines[i], BOOT_TEXT);
        if (ui->screen == 3) {
            boot_box(s, 55, 570, 780, 105, BOOT_PANEL, BOOT_CYAN);
            boot_text(s, 75, 585, "BOOT / OUTPOST / SELECT RENDERER", BOOT_CYAN);
            boot_text(s, 75, 615, ui->selected ? "  1 CPU Software" : "> 1 CPU Software", BOOT_TEXT);
            boot_text(s, 400, 615, ui->selected ? "> 2 GPU Scene" : "  2 GPU Scene", BOOT_TEXT);
            boot_text(s, 75, 643, "1/2 SELECT   ENTER START   ESC SHELL", BOOT_DIM);
        } else {
            snprintf(line, sizeof(line), "rf:/assets/%s> %s_", ui->cwd, ui->command);
            boot_text(s, 56, 590, line, BOOT_CYAN);
        }
    } else {
        boot_text(s, 75, 63, "RASTERFALL / GRAPHICAL BOOT", BOOT_CYAN);
        boot_text(s, 75, 98, "OUTPOST  /  SELECT RENDERING HARDWARE", BOOT_TEXT);
        boot_box(s, 70, 145, 1140, 445, BOOT_PANEL, BOOT_DIM);
        for (int i = 0; i < 2; i++) {
            int y = 215 + i * 130;
            boot_box(s, 110, y, 1060, 104,
                     i == ui->selected ? 0x173945u : BOOT_BG,
                     i == ui->selected ? BOOT_CYAN : BOOT_DIM);
            boot_text(s, 145, y + 24, i ? "GPU SCENE" : "CPU SOFTWARE", BOOT_TEXT);
            boot_text(s, 145, y + 54, i ?
                      "Native graphics / capability checked at startup" :
                      "Software renderer / ready", BOOT_DIM);
        }
        boot_box(s, 862, 515, 308, 52, BOOT_CYAN, BOOT_CYAN);
        boot_text(s, 920, 532, "START OUTPOST", BOOT_BG);
        boot_text(s, 110, 532, "ESC  RF INIT", BOOT_DIM);
        boot_text(s, 95, 625, "GPU availability is verified when selected.", BOOT_DIM);
    }
    if (*ui->error) boot_text(s, 55, 686, ui->error, BOOT_AMBER);
}

int rf_boot_run(struct rf_core *core, struct rf_boot_result *result,
                const struct rf_boot_journal *journal, const char *error)
{
    struct boot_ui ui;
    int ready;
    char line[176];
    if (!core || !result) return -1;
    memset(&ui, 0, sizeof(ui));
    if (error) snprintf(ui.error, sizeof(ui.error), "%s", error);
    /* The first present reflects measured work; no clock-driven playback. */
    ready = rf_core_begin_frame(core, BOOT_BG);
    if (ready > 0) {
        struct toy_surface *s = boot_overlay(core);
        if (!s) return -1;
        boot_text(s, 54, 58, "RF CORE / bootstrap", BOOT_CYAN);
        if (journal) for (int i = 0; i < journal->count; ++i) {
            const struct rf_boot_event *event = &journal->events[i];
            snprintf(line, sizeof(line), "[ %s ] %-24s %8.3f ms",
                     event->result < 0 ? "FAIL" : event->result ? "N/A" : "OK",
                     event->service, (double)event->elapsed_us / 1000.0);
            boot_text(s, 54, 102 + i * 26, line, BOOT_TEXT);
        }
        boot_text(s, 54, 550, "RF INIT ready - select a boot environment", BOOT_DIM);
        if (rf_core_present_boot_frame(core) < 0) return -1;
    }
    boot_marker("RF-BOOT stage=menu status=ready");
    for (;;) {
        struct toy_window_events *events = rf_core_events(core);
        if (rf_core_poll_events_timeout(core, 16) < 0) {
            __fprintf(2, "RF-BOOT menu event poll failed\n"); return -1;
        }
        if (rf_core_should_exit(core)) {
            boot_marker("RF-BOOT stage=menu status=closed"); return 0;
        }
        if (ui.screen == 0) {
            if (boot_key(events, BOOT_KEY_UP) || boot_key(events, BOOT_KEY_DOWN))
                ui.selected = !ui.selected;
            if (boot_key(events, BOOT_KEY_ENTER)) {
                ui.screen = ui.selected ? 2 : 1;
                ui.selected = 0;
                if (ui.screen == 1) {
                    boot_line(&ui, "[ OK ] RF Terminal command environment ready");
                    boot_line(&ui, "Type help for commands; boot enters Outpost.");
                    boot_marker("RF-BOOT stage=terminal status=ready");
                } else boot_marker("RF-BOOT stage=graphical status=ready");
            }
        } else if (ui.screen == 1) {
            for (int i = 0; i < events->key_event_count; i++) {
                unsigned int key = events->key_events[i].key;
                char c = 0;
                if (!events->key_events[i].pressed) continue;
                if (key == BOOT_KEY_ENTER) {
                    if (boot_command(&ui, core)) goto selected;
                } else if (key == BOOT_KEY_BACKSPACE) {
                    if (ui.command_length) ui.command[--ui.command_length] = 0;
                } else if (key == BOOT_KEY_ESC) { ui.screen = 0; ui.selected = 0; }
                else {
                    c = boot_character(key);
                    if (c && ui.command_length + 1 < (int)sizeof(ui.command)) {
                        ui.command[ui.command_length++] = c;
                        ui.command[ui.command_length] = 0;
                    }
                }
            }
        } else if (ui.screen == 3) {
            if (boot_key(events, BOOT_KEY_ESC)) ui.screen = 1;
            if (boot_key(events, BOOT_KEY_1)) ui.selected = 0;
            if (boot_key(events, BOOT_KEY_2)) ui.selected = 1;
            if (boot_key(events, BOOT_KEY_ENTER)) {
                ui.renderer = ui.selected; goto selected;
            }
        } else {
            int px = core->surface.width > 0 ?
                events->pointer_x * 1280 / core->surface.width : -1;
            int py = core->surface.height > 0 ?
                events->pointer_y * 720 / core->surface.height : -1;
            if (boot_key(events, BOOT_KEY_ESC) ||
                (events->button_pressed && events->button == BOOT_BUTTON_LEFT &&
                 px < 380 && py > 500)) {
                ui.screen = 0; ui.selected = 1;
            }
            if (boot_key(events, BOOT_KEY_UP)) ui.selected = 0;
            if (boot_key(events, BOOT_KEY_DOWN)) ui.selected = 1;
            if (events->pointer_moved && px >= 110 && px < 1170) {
                if (py >= 215 && py < 319) ui.selected = 0;
                if (py >= 345 && py < 449) ui.selected = 1;
            }
            if (boot_key(events, BOOT_KEY_ENTER) ||
                (events->button_pressed && events->button == BOOT_BUTTON_LEFT &&
                 px >= 862 && px < 1170 &&
                 py >= 515 && py < 567)) {
                ui.renderer = ui.selected; goto selected;
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
            boot_draw(s, &ui);
            if (rf_core_present_boot_frame(core) < 0) {
                __fprintf(2, "RF-BOOT menu present failed\n"); return -1;
            }
        }
    }
selected:
    result->renderer = ui.renderer;
    result->graphical = ui.screen == 2;
    snprintf(line, sizeof(line),
             "RF-BOOT stage=renderer status=selected backend=%s",
             ui.renderer ? "gpu-scene" : "cpu");
    boot_marker(line);
    return 1;
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
    boot_text(s, 55, 48, graphical ? "RASTERFALL / GRAPHICAL BOOT" :
              "RF TERMINAL / boot", BOOT_CYAN);
    snprintf(line, sizeof(line), "OUTPOST / startup stages %d of %d complete",
             completed, total);
    boot_text(s, 55, 88, line, BOOT_TEXT);
    snprintf(line, sizeof(line), "[ WAIT ] %s", current_task);
    boot_text(s, 55, 135, line, BOOT_AMBER);
    if (graphical) {
        boot_box(s, 55, 180, 900, 32, BOOT_BG, BOOT_DIM);
        if (completed > 0)
            boot_box(s, 57, 182, 896 * completed / total, 28,
                     BOOT_CYAN, BOOT_CYAN);
    } else {
        char bar[48];
        int cells = 30 * completed / total;
        for (int i = 0; i < 30; ++i) bar[i] = i < cells ? '#' : '-';
        bar[30] = 0;
        snprintf(line, sizeof(line), "[%s] startup stages", bar);
        boot_text(s, 55, 183, line, BOOT_TEXT);
    }
    for (int i = 0; i < journal->count && i < 10; ++i) {
        const struct rf_boot_event *event = &journal->events[i];
        snprintf(line, sizeof(line), "[ %s ] %-26s %8.3f ms",
                 event->result < 0 ? "FAIL" : event->result ? "N/A" : "OK",
                 event->service, (double)event->elapsed_us / 1000.0);
        boot_text(s, 55, 250 + i * 27, line, BOOT_TEXT);
    }
    return rf_core_present_boot_frame(core);
}
