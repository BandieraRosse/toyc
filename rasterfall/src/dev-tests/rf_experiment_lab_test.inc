/* Contracts that are hard to observe: suspension, session return and statistics. */
#include "../render/rasterfall_machine_screen.h"
#include "../render/rasterfall_lab_terminal.h"
struct rf_pane_test { int count,points[4][3]; };
static int rf_pane_test_quad(void *context,const int p[4][3],unsigned color)
{
    struct rf_pane_test *t=context;
    (void)color;t->count++;memcpy(t->points,p,sizeof(t->points));return 0;
}
static int rf_pane_contract_test(void)
{
    struct toy_map_draw d={0};struct rf_pane_test t={0};
    d.style=6;d.a=d.b=-40;d.c=-80;d.d=60;d.e=-90;d.f=120;
    if (rf_lab_terminal_emit(&d,rf_pane_test_quad,&t)<0 || t.count!=1) return -1;
    for (int i=0;i<4;++i) if (t.points[i][0]!=-40) return -2;
    if (t.points[0][2]!=-80 || t.points[1][2]!=60 ||
        t.points[0][1]!=120 || t.points[2][1]!=-90) return -3;
    d.a=-20;d.b=80;d.c=d.d=50;t.count=0;
    if (rf_lab_terminal_emit(&d,rf_pane_test_quad,&t)<0 || t.count!=1) return -4;
    for (int i=0;i<4;++i) if (t.points[i][2]!=50) return -5;
    d.d=60;
    if (rf_lab_terminal_emit(&d,rf_pane_test_quad,&t)>=0) return -6;
    d.a=d.b=0;d.c=d.d=0;
    if (rf_lab_terminal_emit(&d,rf_pane_test_quad,&t)>=0) return -7;
    __printf("MACHINE-PANE X/Z planes and invalid bounds passed\n");return 0;
}
struct rf_screen_test { unsigned pixels[64*64]; unsigned char coverage[64*64]; };
static int rf_screen_test_span(void *context,int x0,int x1,int y0,int y1,unsigned color)
{
    struct rf_screen_test *c=context;
    if (x0<0 || x1>64 || y0<0 || y1>64 || x1<=x0 || y1<=y0) return -1;
    for (int y=y0;y<y1;++y) for (int x=x0;x<x1;++x) {
        int p=y*64+x;
        if (c->coverage[p]++) return -1;
        c->pixels[p]=color;
    }
    return 0;
}
static int rf_machine_screen_contract_test(void)
{
    struct rf_screen_test a={0},b={0};
    if (fb_font_load("rasterfall/assets/fonts/gb2312-16.rfh")<0) return -1;
    if (rf_machine_screen_emit(64,64,"A",0xABCDEF,rf_screen_test_span,&a)<0 ||
        rf_machine_screen_emit(64,64,"ABCDEFGHIJKLMNOPQRSTUVWXYZ\n\0332B",0xABCDEF,rf_screen_test_span,&b)<0) return -2;
    int ink=0;
    for (int y=0;y<64;++y) for (int x=0;x<64;++x) {
        int p=y*64+x;
        if (a.coverage[p]!=1 || b.coverage[p]!=1) return -3;
        if (x>=8 && x<16 && y>=8 && y<24 && a.pixels[p]!=b.pixels[p]) return -4;
        if (a.pixels[p]==0xABCDEF) ink++;
    }
    memset(&b,0,sizeof(b));
    if (!ink || rf_machine_screen_emit(64,64,"\0332A",0xABCDEF,rf_screen_test_span,&b)<0) return -5;
    int large=0;for (int p=0;p<64*64;++p) if (b.pixels[p]==0xABCDEF) large++;
    if (large!=ink*4 || rf_machine_screen_emit(1025,64,"A",0,rf_screen_test_span,&b)>=0) return -6;
    __printf("MACHINE-SCREEN pixel-pitch/coverage/clipping/font-scale passed\n");
    return 0;
}
static int rf_lab_composite_contract_test(void)
{
    struct rf_map_runtime map={0};
    if (rf_map_runtime_load(&map,"rasterfall/tests/map_v1_lab.map")<0) return -1;
    const struct rf_map_runtime_region *r=rf_map_runtime_find_region(&map,"fixture");
    const struct rf_map_runtime_object *o=rf_map_runtime_find_object(&map,"control");
    const struct rf_map_runtime_surface *s=rf_map_runtime_find_surface(&map,"floor");
    const struct rf_map_runtime_collision *c=rf_map_runtime_find_collision(&map,"wall");
    const struct rf_map_runtime_interaction *i=rf_map_runtime_find_interaction(&map,"use");
    const struct rf_map_runtime_render *panel=rf_map_runtime_find_render(&map,"screen");
    if (!r || !o || !s || !c || !i || !panel || r->origin_x!=1000 || r->origin_z!=2000 ||
        strcmp(r->category,"model") || strcmp(r->enclosure,"open") ||
        o->x!=700 || o->z!=2400 || o->y!=50 || o->yaw!=90 ||
        s->bounds.min_x!=0 || s->bounds.max_z!=3000 || c->bounds.min_z!=1000 ||
        i->x!=1000 || i->z!=2000 || panel->bounds.min_x!=500) {
        rf_map_runtime_unload(&map);return -2;
    }
    struct toy_map_draw frozen={0};
    const struct rf_map_runtime_object *part=rf_map_runtime_find_object(&map,"assembly_part");
    const struct rf_map_runtime_render *screen=rf_map_runtime_find_render(&map,"assembly_screen");
    const struct rf_map_runtime_collision *body=rf_map_runtime_find_collision(&map,"assembly_part_col_0");
    if (!part || !screen || !body || part->x!=1070 || part->y!=35 || part->z!=1940 ||
        screen->bounds.min_x!=844 || screen->height!=-380 || body->base_y!=35 ||
        strcmp(rf_map_runtime_render_attribute(screen,"height2"),"-60")) {
        rf_map_runtime_unload(&map);return -7;
    }
    frozen.type=TOY_MAP_DRAW_SIGN;frozen.style=4;frozen.a=panel->bounds.min_x;
    rasterfall_render_terminal_set("contract","ON");
    rasterfall_render_terminal_freeze(panel,&frozen);
    rasterfall_render_terminal_set("contract","OFF");
    if (strcmp(frozen.text,"ON") || frozen.a!=500) { rf_map_runtime_unload(&map);return -3; }
    rasterfall_render_terminal_freeze(panel,&frozen);
    if (strcmp(frozen.text,"OFF")) { rf_map_runtime_unload(&map);return -4; }
    frozen.style=5;rasterfall_render_terminal_set("contract","\0332ON\n\0331READY");
    rasterfall_render_terminal_freeze(screen,&frozen);
    if (strcmp(frozen.text,"\0332ON\n\0331READY")) { rf_map_runtime_unload(&map);return -8; }
    char long_text[200];memset(long_text,'A',sizeof(long_text)-1);long_text[199]=0;
    rasterfall_render_terminal_set("contract",long_text);
    rasterfall_render_terminal_freeze(screen,&frozen);
    if (strcmp(frozen.text,long_text)) { rf_map_runtime_unload(&map);return -10; }
    rf_map_runtime_unload(&map);
    if (rf_map_runtime_load(&map,"rasterfall/tests/map_v1_lab_unknown.map")>=0) {
        rf_map_runtime_unload(&map);return -5;
    }
    if (rf_map_runtime_load(&map,"rasterfall/tests/map_v1_lab_overflow.map")>=0) {
        rf_map_runtime_unload(&map);return -6;
    }
    if (rf_map_runtime_load(&map,"rasterfall/tests/map_v1_lab_assembly_unknown.map")>=0 ||
        rf_map_runtime_load(&map,"rasterfall/tests/map_v1_lab_assembly_conflict.map")>=0) {
        rf_map_runtime_unload(&map);return -9;
    }
    __printf("EXPERIMENT-LAB composite/forward-reference/overflow/display-freeze passed\n");
    return 0;
}
struct rf_electronics_test_capture { unsigned hash,count; };
static int rf_electronics_test_quad(void *context,const int p[4][3],unsigned color)
{
    struct rf_electronics_test_capture *c=context;
    c->hash=(c->hash^color)*16777619u;c->count++;
    for (int i=0;i<4;++i) for (int k=0;k<3;++k)
        c->hash=(c->hash^(unsigned)p[i][k])*16777619u;
    return 0;
}
static int rf_electronics_contract_test(void)
{
    rf_labs_reset();rf_lab_control(RF_LAB_ELECTRONICS);
    if (!rf_labs.requested[RF_LAB_ELECTRONICS] || rf_labs.fan_rpm!=600) return -1;
    rf_labs_tick(RASTERFALL_WORLD_OUTPOST,0,1,1000);
    rf_labs_tick(RASTERFALL_WORLD_OUTPOST,0,1,51000);
    struct rasterfall_electronics_frame a=rasterfall_electronics_get_frame();
    if (a.phase[7]!=30000000u || !(a.powered_mask&(1u<<7))) return -2;
    rf_lab_control(RF_LAB_ELECTRONICS);
    if (rf_labs.fan_rpm!=1200 || rf_labs.fan_phase[RF_LAB_ELECTRONICS]!=a.phase[7]) return -3;
    rf_labs_tick(RASTERFALL_WORLD_OUTPOST,0,1,76000);
    struct rasterfall_electronics_frame b=rasterfall_electronics_get_frame();
    if (b.phase[7]!=0) return -4;
    rf_labs_tick(RASTERFALL_WORLD_OUTPOST,1,1,101000);
    struct rasterfall_electronics_frame paused=rasterfall_electronics_get_frame();
    if (paused.phase[7]!=b.phase[7] || paused.time_ms[7]!=b.time_ms[7] || paused.powered_mask) return -5;
    rf_lab_control(RF_LAB_ELECTRONICS);
    if (rf_labs.fan_rpm!=1800) return -6;
    rf_lab_control(RF_LAB_ELECTRONICS);
    if (rf_labs.requested[RF_LAB_ELECTRONICS]) return -7;
    rf_labs_tick(RASTERFALL_WORLD_OUTPOST,0,1,126000);
    if (rasterfall_electronics_get_frame().phase[7]!=b.phase[7]) return -8;
    struct rasterfall_prop_instance p={RASTERFALL_PROP_ASSET_LAB_COMPUTER_COMPUTE,0,0,0,0,1000,7};
    struct rf_electronics_test_capture x={0},y={0},z={0};
    if (rasterfall_electronics_activity(&p,&a,rf_electronics_test_quad,&x)<0 ||
        rasterfall_electronics_activity(&p,&b,rf_electronics_test_quad,&y)<0 ||
        rasterfall_electronics_activity(&p,&b,rf_electronics_test_quad,&z)<0 ||
        !x.count || x.hash==y.hash || y.hash!=z.hash) return -9;
    p.asset_id=RASTERFALL_PROP_ASSET_LAB_COMPUTER_CASE;p.x=p.z=0;
    a.powered_mask=1u<<7;a.time_ms[7]=0;b=a;b.time_ms[7]=400;
    memset(&x,0,sizeof(x));memset(&y,0,sizeof(y));
    if (rasterfall_electronics_activity(&p,&a,rf_electronics_test_quad,&x)<0 ||
        rasterfall_electronics_activity(&p,&b,rf_electronics_test_quad,&y)<0 || x.hash==y.hash) return -10;
    rf_labs_reset();
    if (rasterfall_electronics_get_frame().powered_mask) return -11;
    __printf("RF-ELECTRONICS RPM/phase-continuity/pause/power/rotors/status passed\n");return 0;
}
static int rf_experiment_lab_logic_test(struct rf_game_runtime *runtime)
{
    if (rf_lab_composite_contract_test()<0) return -12;
    if (rf_machine_screen_contract_test()<0) return -13;
    if (rf_pane_contract_test()<0) return -14;
    if (rf_electronics_contract_test()<0) return -15;
    if (rf_game_request_world(runtime,RASTERFALL_WORLD_OUTPOST)<0) return -10;
    struct camera origin=runtime->camera;
    uint64_t seed=runtime->session->seed;
    for (int i=0;i<RF_LAB_COUNT;++i) {
        const struct rf_experiment_definition *def=&rf_lab_definitions[i];
        if (!rf_map_runtime_find_surface(&runtime->session->map_ops.runtime,def->surface) ||
            !rf_map_runtime_find_object(&runtime->session->map_ops.runtime,def->terminal)) return -1;
        rf_labs.requested[i]=1;
        if (!rf_lab_effective(i,RASTERFALL_WORLD_OUTPOST,0,1) ||
            rf_lab_effective(i,RASTERFALL_WORLD_OUTPOST,1,1) ||
            rf_lab_effective(i,RASTERFALL_WORLD_PERF_EMPTY,0,1) ||
            (def->gpu_only && rf_lab_effective(i,RASTERFALL_WORLD_OUTPOST,0,0))) return -2;
    }
    rf_labs_tick(RASTERFALL_WORLD_OUTPOST,0,1,1000);
    rf_labs_tick(RASTERFALL_WORLD_OUTPOST,0,1,2000);
    uint64_t clock=rf_labs.time_us[RF_LAB_LIGHTING];
    rf_labs_tick(RASTERFALL_WORLD_OUTPOST,1,1,3000);
    if (rf_labs.time_us[RF_LAB_LIGHTING]!=clock) return -3;
    rf_perf_lab.isolated=1;
    for (int scene=0;scene<RF_PERF_LAB_SCENES;++scene) {
        rf_perf_lab.selected=scene;
        if (rf_perf_lab_start(runtime)<0) return -4;
        int expected=scene==1 || scene==3 ? 64 : 0;
        if (runtime->session->world_id!= (scene>=2 ?
                RASTERFALL_WORLD_PERF_COMPONENTS : RASTERFALL_WORLD_PERF_EMPTY) ||
            runtime->session->content.actor_count || runtime->session->content.spawn_null ||
            runtime->session->game_state.enemies_alive!=expected) return -5;
        /* Spawn regions override floor paint; an uncoloured region looks like a hole. */
        for (int i=0;i<runtime->session->level.spawn_count;++i)
            if (!runtime->session->level.spawn_zones[i].color) return -11;
        int64_t now=rf_perf_lab.started_us+RF_PERF_LAB_WARMUP_US+1;
        rf_perf_lab_sample(now,10000,0,0,0,0,0,2.0,10,expected,100,1,0,3,30,0,1280,720);
        rf_perf_lab_sample(now,12000,0,0,0,0,0,0.0,10,expected,100,0,0,3,30,0,1280,720);
        if (rf_perf_lab_finish(runtime,scene==3)<0) return -6;
        const struct rf_perf_lab_result *r=&rf_perf_lab.result;
        if (r->valid!=(scene==3?2:1) || r->gpu_samples!=1 || r->gpu_draw_ms!=2.0 ||
            r->mean_us!=11000 || runtime->session->world_id!=RASTERFALL_WORLD_OUTPOST ||
            runtime->session->seed!=seed || runtime->camera.x!=origin.x ||
            runtime->camera.z!=origin.z || runtime->session->game_state.enemies_alive ||
            !rf_labs.requested[RF_LAB_LIGHTING]) return -7;
    }
    /* Authored safe regions protect observation, never suppress bay spawning. */
    rf_perf_lab.isolated=0;
    for (int scene=0;scene<RF_PERF_LAB_SCENES;++scene) {
        rf_perf_lab.selected=scene;
        if (rf_perf_lab_start(runtime)<0) return -16;
        int expected=scene==1 || scene==3 ? 64 : 0;
        if (runtime->session->world_id!=RASTERFALL_WORLD_OUTPOST ||
            runtime->session->game_state.enemies_alive!=expected) return -17;
        if (rf_perf_lab_finish(runtime,scene==3)<0 ||
            runtime->session->game_state.enemies_alive ||
            runtime->camera.x!=origin.x || runtime->camera.z!=origin.z ||
            !rf_labs.requested[RF_LAB_LIGHTING]) return -18;
    }
    rf_perf_lab.isolated=1;
    /* Any workload growth, including a previously empty scene, invalidates it. */
    rf_perf_lab.selected=0;
    if (rf_perf_lab_start(runtime)<0) return -8;
    rf_perf_lab_sample(rf_perf_lab.started_us+RF_PERF_LAB_WARMUP_US+1,
        10000,0,0,0,0,0,1,10,1,100,1,0,3,30,0,1280,720);
    if (rf_perf_lab_finish(runtime,0)<0 || rf_perf_lab.result.valid!=2) return -9;
    memset(&rf_perf_lab,0,sizeof(rf_perf_lab));rf_perf_lab.isolated=1;
    rf_labs_reset();
    __printf("EXPERIMENT-LAB lifecycle/suspension/statistics passed\n");
    return 0;
}
