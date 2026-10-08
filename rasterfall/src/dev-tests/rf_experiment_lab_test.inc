/* Contracts that are hard to observe: suspension, session return and statistics. */
#include "../render/rasterfall_machine_screen.h"
#include "../render/rasterfall_lab_terminal.h"
#include "../render/rf_display_geometry_cache.h"
struct rf_display_test { const struct rf_display_geometry *entry; unsigned index; };
static int rf_display_test_quad(void *context,const int p[4][3],unsigned color)
{
    struct rf_display_test *t=context;
    if(t->index>=t->entry->count) return -1;
    const struct rf_display_quad *q=&t->entry->quads[t->index++];
    if(memcmp(q->p,p,sizeof(q->p)) || q->color!=color) return -1;
    for(int k=0;k<4;++k) for(int a=0;a<3;++a)
        if(p[k][a]<t->entry->min[a] || p[k][a]>t->entry->max[a]) return -1;
    return 0;
}
static int rf_display_geometry_test(void)
{
    struct rf_display_geometry entry={0},limited={0};
    struct toy_map_draw d={0};unsigned budget=0,full=RF_DISPLAY_QUAD_BUDGET;
    int result=-1;
    d.type=TOY_MAP_DRAW_SIGN;d.a=-128;d.b=128;d.c=d.d=12;d.e=0;d.f=256;
    d.texture_u=d.texture_v=64;d.style=5;d.color=0x79E8C5;strcpy(d.text,"OFF");
    for(int variant=0;variant<9;++variant) {
        if(variant==1) strcpy(d.text,"\0332\033BON");
        if(variant==2) d.color=0xAABBCC;
        if(variant==3) d.facing=1;
        if(variant==4) {d.a+=400;d.b+=400;d.c+=80;d.d+=80;}
        if(variant==5) {d.b+=256;d.texture_u=128;}
        if(variant==6) {d.style=4;d.texture_u=4;strcpy(d.text,"RESULT 12.34 ms");}
        if(variant==7) {d.style=2;d.e=900;d.f=1400;}
        if(variant==8) strcpy(d.text,"");
        if(rf_display_geometry_prepare(&entry,&d,&budget)<0) goto done;
        struct rf_display_test check={&entry,0};
        if(rf_display_generate(&d,rf_display_test_quad,&check)<0 || check.index!=entry.count) goto done;
        unsigned saved=budget;
        if(rf_display_geometry_prepare(&entry,&d,&budget)<0 || saved!=budget) goto done;
        check.index=0;
        if(rf_display_geometry_emit(&entry,rf_display_test_quad,&check)<0 || check.index!=entry.count) goto done;
    }
    strcpy(d.text,"BUDGET FALLBACK");
    if(rf_display_geometry_prepare(&limited,&d,&full)>=0 || full!=RF_DISPLAY_QUAD_BUDGET || limited.valid>0) goto done;
    /* Resource identity/order changes must not preserve a stale screen. */
    d.style=5;d.texture_u=d.texture_v=64;d.b=d.a+256;d.e=0;d.f=256;
    strcpy(d.text,"OFF");
    if(rf_display_geometry_prepare(&entry,&d,&budget)<0) goto done;
    struct rf_display_test check={&entry,0};
    if(rf_display_generate(&d,rf_display_test_quad,&check)<0 || check.index!=entry.count) goto done;
    result=0;
done:
    free(entry.quads);free(limited.quads);
    if(!result) __printf("DISPLAY-CACHE content/placement/invalidation/order/bounds/budget passed\n");
    return result;
}
struct rf_pane_test { int count,points[4][3]; };
static int rf_projection_test_quad(void *context,const int p[4][3],unsigned color,int alpha)
{
    int *count=context;
    (void)p;(void)color;(void)alpha;(*count)++;return 0;
}
static int rf_box_finish_contract_test(void)
{
    struct toy_map *level=calloc(1,sizeof(*level));
    struct rasterfall_map_state map={0};
    struct toy_game_box spawn[TOY_MAP_MAX_ZONES];struct rasterfall_interactable unused;
    int spawn_count=0,air=0,item_count=0,result=-1,seen=0;
    if(!level)return -1;
    rasterfall_map_bind(&map,level,level->safe_rooms,spawn,&spawn_count,&air,&unused,&item_count);
    if(rasterfall_map_load_runtime_overlay(&map,"rasterfall/tests/map_v1_box_finishes.map")<0 ||
       rasterfall_map_project_runtime(&map)<0)goto done;
    for(int i=0;i<level->draw_count;++i) {
        const struct rf_map_runtime_render *source=rasterfall_map_render_projection_at(&map,i);
        const struct toy_map_draw *d=&level->draw[i];
        unsigned expected=!strcmp(source->id,"painted_slab")?0x1e3e6e8:
            !strcmp(source->id,"black_slab")?0x1000000:0;
        if(d->bottom_color!=expected || d->color!=0x758995 || d->f!=2304 || d->e!=2458)goto done;
        seen|=expected==0?1:expected==0x1000000?2:4;
    }
    if(seen==7)result=0;
done:
    rasterfall_map_unload(&map);free(level);
    if(!result)__printf("BOX-FINISH parser/runtime/projection/default/black passed\n");
    return result;
}
static int rf_projection_beams_contract_test(void)
{
    struct toy_map *level=calloc(1,sizeof(*level));
    struct rasterfall_map_state map={0};
    struct toy_game_box spawn[TOY_MAP_MAX_ZONES];
    struct rasterfall_interactable unused;
    int spawn_count=0,air=0,item_count=0,result=-1,seen=0;
    if(!level) return -1;
    rasterfall_map_bind(&map,level,level->safe_rooms,spawn,&spawn_count,&air,&unused,&item_count);
    if(rasterfall_map_load_runtime_overlay(&map,"rasterfall/tests/map_v1_lab_projection_beams.map")<0 ||
       rasterfall_map_project_runtime(&map)<0) goto done;
    for(int i=0;i<level->draw_count;++i) {
        const struct rf_map_runtime_render *source=rasterfall_map_render_projection_at(&map,i);
        const char *attribute=rf_map_runtime_render_attribute(source,"projection_beams");
        int disabled=attribute && !strcmp(attribute,"0"),on_count=0,off_count=0;
        struct toy_map_draw d=level->draw[i];
        if(d.projection_no_beams!=disabled || strcmp(d.text,"READABLE")) goto done;
        seen|=disabled?2:attribute?4:1;
        for(int style=2;style<=7;++style) {
            if(style==5 || style==6) continue;
            d.style=style;d.projection_no_beams=0;on_count=off_count=0;
            if(rf_lab_projection_light_emit(&d,1200,4000,rf_projection_test_quad,&on_count)<0) goto done;
            d.projection_no_beams=1;
            if(rf_lab_projection_light_emit(&d,1200,4000,rf_projection_test_quad,&off_count)<0 ||
               on_count<=off_count || off_count!=(style==7?0:1)) goto done;
            /* Disabling scattering must not remove the housing or text. */
            struct rf_display_geometry entry={0};unsigned budget=0;
            d.style=4;
            if(rf_display_geometry_prepare(&entry,&d,&budget)<0) {free(entry.quads);goto done;}
            d.projection_no_beams=0;
            struct rf_display_test check={&entry,0};
            if(!entry.count || rf_display_generate(&d,rf_display_test_quad,&check)<0 ||
               check.index!=entry.count) {free(entry.quads);goto done;}
            free(entry.quads);
        }
    }
    if(seen!=7) goto done;
    result=0;
done:
    rasterfall_map_unload(&map);free(level);
    if(!result) __printf("PROJECTION-BEAMS authored/default/IR/draw/shared-emitter/body-retention passed\n");
    return result;
}
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
static int rf_scene_freeze_cache_test(struct rasterfall_map_state *map)
{
    struct freeze_test_values {
        struct rf_gpu_scene_world_render_frame_v1 render[2];
        struct rf_gpu_scene_world_prop_frame_v1 props[2];
        struct rf_gpu_scene_world_input_v2 world[2][RF_GPU_SCENE_MAX_WORLD_V2];
    } *v=calloc(1,sizeof(*v));
    struct rf_gpu_scene_world_freeze_cache *cache=NULL;
    struct toy_map_draw saved_draw=map->level->draw[0];
    struct toy_map_prop saved_prop=map->level->props[0];
    int saved_count=map->level->draw_count,result=-1;
    if(!v) return -1;
    for(int pass=0;pass<9;++pass) {
        uint32_t count[2]={0};uint64_t frame=pass+1,generation=pass<4?1:2;int air=pass<3?0:1;
        if(pass==2) map->level->draw[0].color^=0x123456;
        if(pass==3) map->level->draw[0]=saved_draw;
        if(pass==7) map->level->draw[0].projection_no_beams=1;
        if(pass==8) map->level->draw[0].projection_no_beams=0;
        rasterfall_render_terminal_set("character_lab",pass==5?"CACHE UPDATE":"OFF");
        if(rf_gpu_scene_world_render_freeze(map,air,frame,generation,v->world[0],
            RF_GPU_SCENE_MAX_WORLD_V2,&count[0],&v->render[0])<0 ||
            rf_gpu_scene_world_render_freeze_cached(&cache,map,air,frame,generation,v->world[1],
            RF_GPU_SCENE_MAX_WORLD_V2,&count[1],&v->render[1])<0 || count[0]!=count[1] ||
            memcmp(v->world[0],v->world[1],count[0]*sizeof(v->world[0][0])) ||
            memcmp(&v->render[0],&v->render[1],sizeof(v->render[0]))) goto done;
        if(rf_gpu_scene_world_prop_freeze(map,frame,generation,&v->props[0])<0 ||
            rf_gpu_scene_world_prop_freeze_cached(&cache,map,frame,generation,&v->props[1])<0 ||
            memcmp(&v->props[0],&v->props[1],sizeof(v->props[0]))) goto done;
        /* Neither caller writes nor the next freeze may mutate a previous snapshot. */
        v->render[1].items[0].draw.color^=0xFFFFFF;
        v->props[1].items[0].prop.x++;
    }
    if(rf_gpu_scene_world_prop_freeze_cached(&cache,map,9,2,&v->props[1])<0) goto done;
    v->props[0]=v->props[1];map->level->props[0].x++;
    if(rf_gpu_scene_world_prop_freeze_cached(&cache,map,10,2,&v->props[1])>=0 ||
        memcmp(&v->props[0],&v->props[1],sizeof(v->props[0]))) goto done;
    map->level->props[0]=saved_prop;
    if(rf_gpu_scene_world_prop_freeze_cached(&cache,map,11,2,&v->props[1])<0) goto done;
    v->render[0]=v->render[1];map->level->draw_count--;
    uint32_t count=123;
    if(rf_gpu_scene_world_render_freeze_cached(&cache,map,0,12,2,v->world[1],
        RF_GPU_SCENE_MAX_WORLD_V2,&count,&v->render[1])>=0 || count!=123 ||
        memcmp(&v->render[0],&v->render[1],sizeof(v->render[0]))) goto done;
    result=0;
done:
    map->level->draw[0]=saved_draw;map->level->props[0]=saved_prop;map->level->draw_count=saved_count;
    rf_gpu_scene_world_freeze_cache_destroy(cache);free(v);
    if(!result) __printf("SCENE-FREEZE cache/content/generation/airwalls/value-ownership/failure passed\n");
    return result;
}
#include "rf_experiment_session_test.inc"
static int rf_experiment_lab_logic_test(struct rf_game_runtime *runtime)
{
    if (rf_experiment_session_test()<0) return -27;
    if (rf_lab_composite_contract_test()<0) return -12;
    if (rf_machine_screen_contract_test()<0) return -13;
    if (rf_display_geometry_test()<0) return -23;
    if (rf_projection_beams_contract_test()<0) return -25;
    if (rf_box_finish_contract_test()<0) return -26;
    if (rf_pane_contract_test()<0) return -14;
    if (rf_electronics_contract_test()<0) return -15;
    if (rf_game_request_world(runtime,RASTERFALL_WORLD_OUTPOST)<0) return -10;
    if (rf_scene_freeze_cache_test(&runtime->session->map_ops)<0) return -24;
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
    for (int scene=0;scene<4;++scene) {
        rf_perf_lab.selected=scene;
        if (rf_perf_lab_start(runtime)<0) return -4;
        int expected=scene==1 || scene==3 ? 64 : 0;
        if (runtime->session->world_id!=RASTERFALL_WORLD_OUTPOST ||
            runtime->session->game_state.enemies_alive!=expected) return -5;
        /* Spawn regions override floor paint; an uncoloured region looks like a hole. */
        for (int i=0;i<runtime->session->level.spawn_count;++i)
            if (!runtime->session->level.spawn_zones[i].color) return -11;
        int64_t first=rf_perf_lab.started_us+20000000;
        rf_perf_lab_sample(first,20000000,0,0,0,0,0,2.0,10,expected,100,1,0,3,30,0,1280,720);
        if(rf_perf_lab.samples || rf_perf_lab.started_us!=first || !rf_perf_lab.presented)return -31;
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
    for (int scene=0;scene<4;++scene) {
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
    /* Live trials borrow the normal update path and must release profiling,
     * participants, camera and presentation on every cancellation. */
    for(int scene=6;scene<8;++scene) {
        rf_perf_lab.selected=scene;rf_perf_lab.isolated=1;
        struct rasterfall_command input={0};
        if(rf_perf_lab_start(runtime)<0)return -26;
        if(runtime->session->game_state.update_profile!=&rf_perf_live.profile ||
            rf_perf_lab.isolated || !rf_perf_lab.interference)return -27;
        for(int step=0;step<8;++step)if(rf_game_update(runtime,&input,16)<0)return -28;
        if(rf_perf_live_result.ticks!=8)return -29;
        if(rf_perf_lab_finish(runtime,1)<0)return -29;
        if(runtime->session->game_state.update_profile || rf_combat_lab.member_count ||
            runtime->session->world_id!=RASTERFALL_WORLD_OUTPOST ||
            runtime->session->game_state.enemies_alive || runtime->session->seed!=seed ||
            runtime->camera.x!=origin.x || runtime->camera.z!=origin.z ||
            !rf_labs.requested[RF_LAB_LIGHTING])return -30;
    }
    rf_perf_lab.isolated=1;
    /* Any workload growth, including a previously empty scene, invalidates it. */
    rf_perf_lab.selected=0;
    if (rf_perf_lab_start(runtime)<0) return -8;
    rf_perf_lab_sample(rf_perf_lab.started_us,10000,0,0,0,0,0,1,10,0,100,1,0,3,30,0,1280,720);
    rf_perf_lab_sample(rf_perf_lab.started_us+RF_PERF_LAB_WARMUP_US+1,
        10000,0,0,0,0,0,1,10,1,100,1,0,3,30,0,1280,720);
    if (rf_perf_lab_finish(runtime,0)<0 || rf_perf_lab.result.valid!=2) return -9;
    memset(&rf_perf_lab,0,sizeof(rf_perf_lab));rf_perf_lab.isolated=1;
    rf_labs_reset();
    __printf("EXPERIMENT-LAB lifecycle/suspension/statistics passed\n");
    return 0;
}
