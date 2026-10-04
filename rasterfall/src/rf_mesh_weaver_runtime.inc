/* Local experiment input and read-only presentation. Manufacturing truth is
 * owned by Game/session, independently of this optional terminal. */
#include "rf_mesh_weaver_presentation.h"
#include "rf_weaver_blueprints_generated.h"

static const char *rf_weaver_environment(const char *name)
{
#ifdef X86_64_TLIBC
    return get_env_var(global_envp,name);
#else
    return getenv(name);
#endif
}

struct rf_weaver_terminal_ui {
    int open, near, selected, row, pointer_x, pointer_y;
    const char *message;
};
static struct rf_weaver_terminal_ui rf_weaver_terminal;

static void rf_weaver_terminal_init(void)
{
    memset(&rf_weaver_terminal, 0, sizeof(rf_weaver_terminal));
    rf_weaver_terminal.message = "RF 自有蓝图；领取成品替换对应武器槽";
}

static const struct rf_weaver_blueprint *rf_weaver_selection(void)
{
    int index = rf_weaver_terminal.selected;
    if (index < 0 || (unsigned)index >= RF_WEAVER_BLUEPRINT_COUNT) index = 0;
    return &rf_weaver_blueprints[index];
}

static const char *rf_weaver_unavailable(const struct rf_weaver_blueprint *b)
{
    if (!b->manufacturable && !b->size_supported)
        return "模型有开放表面，且超出普通机制造空间";
    if (!b->manufacturable) return "制造实体体积未验证：蓝图暂不可制造";
    if (!b->size_supported) return "真实尺寸超出普通机制造空间";
    return NULL;
}

static int rf_weaver_configure(struct rasterfall_session *s, int offline)
{
    const struct rf_map_runtime_object *machine =
        rf_map_runtime_find_object(&s->map_ops.runtime, "mesh_weaver");
    int enabled = machine && offline && s->world_id == RASTERFALL_WORLD_OUTPOST;
    rasterfall_session_weaver_configure(s, enabled,
        machine ? machine->x : 0, machine ? machine->z + 105 : 0,
        machine ? -900 + machine->y + 364 : 0);
    if (!enabled) { rf_weaver_terminal.open = 0; rf_weaver_terminal.near = 0; }
    return enabled;
}

static int rf_weaver_near(const struct rasterfall_session *s, int x, int z)
{
    const struct rf_map_runtime_object *machine, *rf1;
    long long dx, dz;
    if (!s || !s->game_state.weaver.enabled) return 0;
    machine = rf_map_runtime_find_object(&s->map_ops.runtime, "mesh_weaver");
    rf1 = rf_map_runtime_find_object(&s->map_ops.runtime, "mesh_weaver_rf1");
    if (machine) {
        dx = (long long)x - (machine->x - 292);
        dz = (long long)z - (machine->z + 510);
        if (dx * dx + dz * dz <= 700LL * 700) return 1;
    }
    if (rf1) {
        dx = (long long)x - (rf1->x + 130);
        dz = (long long)z - (rf1->z + 180);
        if (dx * dx + dz * dz <= 750LL * 750) return 1;
    }
    return 0;
}

static void rf_weaver_terminal_activate(struct rasterfall_session *s, int direction)
{
    struct toy_mesh_weaver_supply supply;
    const struct rf_weaver_blueprint *b;
    const char *unavailable;
    int reason;
    if (!s || !s->game_state.weaver.enabled) {
        rf_weaver_terminal.message = "实验制造仅在离线前哨站开放";
        return;
    }
    if (rf_weaver_terminal.row == 0) {
        int count = (int)RF_WEAVER_BLUEPRINT_COUNT;
        rf_weaver_terminal.selected =
            (rf_weaver_terminal.selected + (direction < 0 ? count - 1 : 1)) % count;
        unavailable = rf_weaver_unavailable(rf_weaver_selection());
        rf_weaver_terminal.message = unavailable ? unavailable :
            "蓝图已选择；开始后本次任务固定使用该模型";
        return;
    }
    if (rf_weaver_terminal.row == 1) {
        b = rf_weaver_selection();
        unavailable = rf_weaver_unavailable(b);
        if (unavailable) { rf_weaver_terminal.message = unavailable; return; }
        reason = rasterfall_session_weaver_start(s, &b->geometry);
        rf_weaver_terminal.message = reason ? toy_mesh_weaver_reason_name(reason) :
            "制造已开始；关闭面板可近看，靠近成品按 E 领取";
        return;
    }
    supply = s->game_state.weaver.supply;
    if (rf_weaver_terminal.row == 2) supply.power_on = !supply.power_on;
    else if (rf_weaver_terminal.row == 3) supply.cpu_on = !supply.cpu_on;
    else if (rf_weaver_terminal.row == 4) supply.x1_on = !supply.x1_on;
    else return;
    toy_game_weaver_set_supply(&s->game_state, &supply);
    rf_weaver_terminal.message = "供给已切换；已耗资源和当前制造进度保留";
}

static void rf_weaver_terminal_input(struct rasterfall_session *s,
    int up, int down, int left, int right, int enter, int esc)
{
    if (!rf_weaver_terminal.open) return;
    if (esc) { rf_weaver_terminal.open = 0; return; }
    if (up) rf_weaver_terminal.row = (rf_weaver_terminal.row + 4) % 5;
    if (down) rf_weaver_terminal.row = (rf_weaver_terminal.row + 1) % 5;
    if (left || right || enter)
        rf_weaver_terminal_activate(s, left && !right && !enter ? -1 : 1);
}

static void rf_weaver_terminal_click(struct rasterfall_session *s,
    int width, int height, int pointer_x, int pointer_y)
{
    int x = (width - 920) / 2, y = (height - 580) / 2;
    int row = (pointer_y - (y + 130)) / 52;
    rf_weaver_terminal.pointer_x = pointer_x;
    rf_weaver_terminal.pointer_y = pointer_y;
    if (!rf_weaver_terminal.open || pointer_x < x + 24 || pointer_x >= x + 314 ||
        pointer_y < y + 130 || pointer_y >= y + 130 + 5 * 52 || row < 0 || row >= 5)
        return;
    rf_weaver_terminal.row = row;
    rf_weaver_terminal_activate(s, row == 0 && pointer_x < x + 90 ? -1 : 1);
}

static struct rf_weaver_presentation_state rf_weaver_present_state;
static struct rf_mesh_weaver_frame rf_weaver_presented_frame;

static void rf_weaver_present_reset(void)
{
    rf_weaver_presentation_reset(&rf_weaver_present_state);
    memset(&rf_weaver_presented_frame,0,sizeof(rf_weaver_presented_frame));
    rf_weaver_presented_frame.output_slot=-1;
}

static void rf_weaver_frame_source(const struct rasterfall_session *s,
    struct rf_mesh_weaver_frame *frame)
{
    const struct toy_mesh_weaver *w;
    const struct rf_map_runtime_object *machine;
    memset(frame, 0, sizeof(*frame));
    frame->output_slot = -1;
    if (!s) return;
    machine = rf_map_runtime_find_object(&s->map_ops.runtime, "mesh_weaver");
    if (!machine) return;
    w = &s->game_state.weaver;
    frame->present = 1;
    frame->x = machine->x; frame->z = machine->z; frame->y = -900 + machine->y;
    frame->phase = w->phase; frame->pause_reason = w->pause_reason;
    frame->weapon = w->phase == TOY_WEAVER_IDLE ?
        rf_weaver_selection()->geometry.weapon : w->blueprint.weapon;
    frame->powered = w->enabled && w->supply.power_on && w->supply.energy_kj > 0.0;
    frame->serial = w->job_serial;
    frame->collected_count = w->collected_count;
    frame->progress = w->phase == TOY_WEAVER_IDLE ? 0.0 : w->progress;
    frame->phase_ms = w->phase_ms;
    frame->phase_duration_ms = w->phase == TOY_WEAVER_CALIBRATING ?
        w->coefficients.calibration_ms : w->phase == TOY_WEAVER_DELIVERING ?
        w->coefficients.delivery_ms : 0.0;
    frame->time_ms = (unsigned)w->elapsed_ms;
    if (s->weaver_item_serial && w->phase == TOY_WEAVER_READY)
        frame->output_slot = s->weaver_item_index;
}

/* One clock sample and state update for both render owners. Neither accessor
 * advances visual time or projects a second, potentially different snapshot. */
static void rf_weaver_present_tick(const struct rasterfall_session *s,
    uint64_t time_us,int paused)
{
    rf_weaver_frame_source(s,&rf_weaver_presented_frame);
    rf_weaver_presentation_update(&rf_weaver_present_state,&rf_weaver_presented_frame,
        s?s->scene_local.world_generation:0,time_us,paused);
}

static void rf_weaver_frame(const struct rasterfall_session *s,
    struct rf_mesh_weaver_frame *frame)
{
    if(!s || !rf_weaver_present_state.valid ||
       rf_weaver_present_state.world_generation!=s->scene_local.world_generation) {
        memset(frame,0,sizeof(*frame));frame->output_slot=-1;return;
    }
    *frame=rf_weaver_presented_frame;
}

static void rf_weaver_display(const struct rasterfall_session *s)
{
    const struct toy_mesh_weaver *w = &s->game_state.weaver;
    const struct rf_weaver_blueprint *b = rf_weaver_selection();
    const char *name = w->phase == TOY_WEAVER_IDLE ? b->name :
        toy_game_weapon_name(w->blueprint.weapon);
    const char *status = !w->enabled ? "OFFLINE ONLY" : w->pause_reason ?
        toy_mesh_weaver_reason_name(w->pause_reason) : toy_mesh_weaver_phase_name(w->phase);
    char text[256];
    int percent = w->phase == TOY_WEAVER_IDLE ? 0 : (int)(w->progress * 100.0 + 0.5);
    snprintf(text, sizeof(text), "RF MESH WEAVER / %s / %s %d%%", status, name, percent);
    rasterfall_render_terminal_set("mesh_weaver_lab", text);
    snprintf(text, sizeof(text), "RF MESH WEAVER\n%s / %d%%\n%s\n[E] CONTROL",
        name, percent, w->pause_reason ? "PAUSED" : toy_mesh_weaver_phase_name(w->phase));
    rasterfall_render_terminal_set("mesh_weaver_screen", text);
    snprintf(text, sizeof(text),
        "\033ARF1 / RF OWNED DATA\n\033FC1 %.1f GHz  X1 %s\n%.0f work/s\n%s  %d%%\n\033M%s\n[E] WEAVER CONTROL",
        w->supply.cpu_ghz, w->supply.x1_on ? "ON" : "OFF",
        toy_mesh_weaver_compute(w), name, percent, status);
    rasterfall_render_terminal_set("mesh_weaver_lab_computer", text);
    snprintf(text, sizeof(text), "RF1 / C1 %.1f GHz / X1 %s / %.0f work/s",
        w->supply.cpu_ghz, w->supply.x1_on ? "ON" : "OFF", toy_mesh_weaver_compute(w));
    rasterfall_render_terminal_set("mesh_weaver_rf1", text);
    snprintf(text, sizeof(text), "EXPERIMENT SUPPLY / %s / %.0f kW / %.2f MJ",
        w->supply.power_on ? "ON" : "OFF", w->supply.source_kw, w->supply.energy_kj / 1000.0);
    rasterfall_render_terminal_set("mesh_weaver_power", text);
}

static void rf_weaver_terminal_draw(struct rasterfall_canvas *canvas,
    const struct rasterfall_session *s)
{
    const struct toy_mesh_weaver *w = &s->game_state.weaver;
    const struct rf_weaver_blueprint *b = rf_weaver_selection();
    const struct toy_game_weapon_info *weapon = toy_game_weapon_info(b->geometry.weapon);
    struct toy_mesh_weaver_cost cost = {0};
    int valid = toy_mesh_weaver_cost(&b->geometry, &w->coefficients, &cost) == 0;
    int x = (canvas->width - 920) / 2, y = (canvas->height - 580) / 2;
    const char *status = w->pause_reason ? toy_mesh_weaver_reason_name(w->pause_reason) :
        toy_mesh_weaver_phase_name(w->phase);
    char line[192];
    double ideal = valid ? toy_mesh_weaver_estimate(w, &cost) : -1.0;
    double progress = w->phase == TOY_WEAVER_IDLE ? 0.0 : w->progress;
    if (progress < 0.0) progress = 0.0;
    if (progress > 1.0) progress = 1.0;
    rasterfall_canvas_rect(canvas, x - 3, y - 3, 926, 586, 0x58B7C0, 255);
    rasterfall_canvas_rect(canvas, x, y, 920, 580, 0x111D2A, 255);
    rasterfall_canvas_text(canvas, x + 24, y + 23, "网格编织机  /  RF MESH WEAVER", 0xDCF4F1);
    rasterfall_canvas_text(canvas, x + 24, y + 51,
        "RF 组织自有蓝图  /  普通机  /  RF1 + C1 + M1 + X1", 0x8FB2C1);
    snprintf(line, sizeof(line), "%s   %s   %.1f%%", status,
        w->phase == TOY_WEAVER_IDLE ? "" : toy_game_weapon_name(w->blueprint.weapon),
        progress * 100.0);
    rasterfall_canvas_text(canvas, x + 24, y + 87, line,
        w->pause_reason ? 0xE8AF73 : 0x8AE0D2);
    for (int row = 0; row < 5; ++row) {
        int ry = y + 130 + row * 52;
        rasterfall_canvas_rect(canvas, x + 24, ry, 290, 43,
            rf_weaver_terminal.row == row ? 0x2E5362 : 0x1D3040, 255);
        if (row == 0) snprintf(line, sizeof(line), "<  蓝图  %s  >", b->name);
        else if (row == 1) snprintf(line, sizeof(line), "%s",
            w->phase == TOY_WEAVER_READY ? "出料占用：靠近取枪" :
            w->phase == TOY_WEAVER_IDLE ? "开始制造" : "当前任务进行中");
        else if (row == 2) snprintf(line, sizeof(line), "电源  %s  /  %.0f kW",
            w->supply.power_on ? "开" : "关", w->supply.source_kw);
        else if (row == 3) snprintf(line, sizeof(line), "C1 CPU  %s  /  %.1f GHz",
            w->supply.cpu_on ? "开" : "关", w->supply.cpu_ghz);
        else snprintf(line, sizeof(line), "X1 编织加速  %s", w->supply.x1_on ? "开" : "关");
        rasterfall_canvas_text(canvas, x + 38, ry + 13, line, 0xD8EBEB);
    }
    snprintf(line, sizeof(line), "所选蓝图：%s  /  %s", b->name,
        rf_weaver_unavailable(b) ? "暂不可制造" : "可制造");
    rasterfall_canvas_text(canvas, x + 344, y + 133, line, 0xD8EBEB);
    snprintf(line, sizeof(line), "顶点 Nv %u     三角面 Nt %u",
        b->geometry.vertex_count, b->geometry.triangle_count);
    rasterfall_canvas_text(canvas, x + 344, y + 164, line, 0x9ABBC8);
    snprintf(line, sizeof(line), "实体 %.3f cm3    尺寸 %u x %u x %u mm",
        b->geometry.volume_mm3 / 1000.0,
        b->geometry.bounds_mm[0], b->geometry.bounds_mm[1], b->geometry.bounds_mm[2]);
    rasterfall_canvas_text(canvas, x + 344, y + 195, line, 0x9ABBC8);
    if (valid) snprintf(line, sizeof(line), "工作 %.1f    成型 %.2f kJ", cost.work, cost.form_kj);
    else snprintf(line, sizeof(line), "体积未验证；不计算制造成本");
    rasterfall_canvas_text(canvas, x + 344, y + 226, line, 0x9ABBC8);
    if (ideal >= 0.0) snprintf(line, sizeof(line), "预计 %.2f 秒（含校准与交付）", ideal);
    else snprintf(line, sizeof(line), "预计时间：等待有效电力与算力");
    rasterfall_canvas_text(canvas, x + 344, y + 257, line, 0x8AE0D2);
    snprintf(line, sizeof(line), "含标准弹匣 %d 发 / 零备弹 / 成本已计入", weapon->mag_size);
    rasterfall_canvas_text(canvas, x + 344, y + 288, line, 0x9ABBC8);
    snprintf(line, sizeof(line), "当前算力 %.0f work/s  /  成型 %.2f kW",
        toy_mesh_weaver_compute(w), toy_mesh_weaver_form_power(w));
    rasterfall_canvas_text(canvas, x + 344, y + 332, line, 0xD8EBEB);
    snprintf(line, sizeof(line), "已用 %.1f work / %.2f kJ  +  基础 %.2f kJ",
        w->used_compute, w->used_form_kj, w->used_base_kj);
    rasterfall_canvas_text(canvas, x + 344, y + 363, line, 0x9ABBC8);
    snprintf(line, sizeof(line), "实验供给余量 %.3f / 20.000 MJ    蓝图库 %.0f MiB",
        w->supply.energy_kj / 1000.0, (double)w->supply.storage_bytes / 1048576.0);
    rasterfall_canvas_text(canvas, x + 24, y + 410, line, 0xD8EBEB);
    rasterfall_canvas_rect(canvas, x + 24, y + 443, 872, 11, 0x273C4A, 255);
    if (progress > 0.0) rasterfall_canvas_rect(canvas, x + 24, y + 443,
        (int)(872 * progress), 11, w->pause_reason ? 0xCF9561 : 0x58BDB4, 255);
    rasterfall_canvas_text(canvas, x + 24, y + 477,
        rf_weaver_unavailable(b) ? rf_weaver_unavailable(b) : rf_weaver_terminal.message, 0xE8AF73);
    rasterfall_canvas_text(canvas, x + 24, y + 517,
        "方向键选择与切换 / 回车执行 / 鼠标点击 / Esc 关闭；制造持续推进", 0xA1C5D0);
    rasterfall_canvas_text(canvas, x + 24, y + 549,
        "成品旁按 E 领取并替换武器槽；弹药由行动部补给；换图或重开重置实验", 0x819AA8);
    rasterfall_canvas_rect(canvas, rf_weaver_terminal.pointer_x - 5,
        rf_weaver_terminal.pointer_y, 11, 1, 0xFFFFFF, 255);
    rasterfall_canvas_rect(canvas, rf_weaver_terminal.pointer_x,
        rf_weaver_terminal.pointer_y - 5, 1, 11, 0xFFFFFF, 255);
}

static void rf_weaver_camera(const struct rasterfall_session *session,struct camera *camera)
{
    const struct rf_map_runtime_object *machine=
        rf_map_runtime_find_object(&session->map_ops.runtime,"mesh_weaver");
    const struct rf_map_runtime_region *area=rf_map_runtime_find_region(
        &session->map_ops.runtime,"mesh_weaver_lab_area");
    if(!machine && !area) return;
    int origin_x=machine?machine->x:area->origin_x;
    int origin_z=machine?machine->z:area->origin_z;
    int origin_y=machine?machine->y:0;
    const char *view=rf_weaver_environment("RF_WEAVER_VIEW");
    double dx=950,dz=2050,dy=530,tx=0,ty=-420,tz=0;
    if(view && !strcmp(view,"interaction")) {dx=0;dz=850;dy=0;}
    else if(view && !strcmp(view,"front")) {dx=160;dz=1620;dy=100;}
    else if(view && !strcmp(view,"rear")) {dx=-1100;dz=-1520;dy=160;}
    else if(view && !strcmp(view,"head")) {dx=-80;dz=-60;dy=-535;tx=-300;ty=-622;tz=-300;}
    else if(view && !strcmp(view,"gun")) {dx=80;dz=1100;dy=-260;ty=-407;}
    else if(view && !strcmp(view,"top")) {dx=200;dz=1100;dy=1580;}
    else if(view && !strcmp(view,"wide")) {dx=1550;dz=3100;dy=560;}
    camera->x=origin_x+(int)dx;camera->z=origin_z+(int)dz;
    camera->y=origin_y+(int)dy;
    double vx=tx-dx,vz=tz-dz,vy=ty-dy;
    double horizontal=sqrt(vx*vx+vz*vz),length=sqrt(horizontal*horizontal+vy*vy);
    camera->sy=(int)(vx/horizontal*1024);camera->cy=(int)(vz/horizontal*1024);
    camera->pitch_sy=(int)(vy/length*1024);camera->pitch_cy=(int)(horizontal/length*1024);
}
