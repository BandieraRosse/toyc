/* Live actors, normal AI and normal rendering. This owner only spawns its
 * participants and issues deployment orders; it never selects their poses. */
#define RF_IDLE_LAB_ACTORS 4
static struct {
    uint64_t world;
    int count,mode,elapsed_ms;
    int ids[RF_IDLE_LAB_ACTORS+1];
    unsigned generations[RF_IDLE_LAB_ACTORS+1];
} rf_idle_lab;
static const char *rf_idle_lab_modes[]={"OFF","IDLE / RANDOM HOLDS","WALK / RETURN","TARGET / LIVE FIRE"};

static struct toy_game_actor *rf_idle_lab_actor(struct toy_game *g,int i)
{
    if(i<0 || i>=rf_idle_lab.count)return NULL;
    struct toy_game_actor *a=toy_game_actor_by_id(g,rf_idle_lab.ids[i]);
    return a && a->combat_generation==rf_idle_lab.generations[i]?a:NULL;
}
static void rf_idle_lab_remove(struct toy_game *g,int i)
{
    struct toy_game_actor *a=rf_idle_lab_actor(g,i);
    if(a) {
        g->money-=a->combat_stats.money_earned;
        memset(a,0,sizeof(*a));
    }
}
static void rf_idle_lab_clear(struct rasterfall_session *s)
{
    if(rf_idle_lab.world==s->scene_local.world_generation)
        for(int i=0;i<rf_idle_lab.count;++i)rf_idle_lab_remove(&s->game_state,i);
    memset(&rf_idle_lab,0,sizeof(rf_idle_lab));
}
static int rf_idle_lab_add(struct toy_game *g,int character,int x,int z,int target)
{
    int id=target?toy_game_add_gunner(g,0,x,z,"LAB TARGET"):
        toy_game_add_ai(g,TOY_GAME_AI_LEVEL_2,x,z,"LIVE HOLD AI");
    struct toy_game_actor *a=toy_game_actor_by_id(g,id);
    if(!a)return -1;
    if(!target)a->character_id=character;
    a->flag_index=-1;a->companion=0;a->deployment_x=x;a->deployment_z=z;
    a->sy=0;a->cy=target?-1024:1024;a->pitch_sy=0;a->pitch_cy=1024;
    toy_game_actor_equip_weapon(g,a,TOY_GAME_WEAPON_AK);
    toy_game_actor_refill_ammo(g,a);
    if(target) {a->hit_test_dummy=1;a->fire_enabled=0;}
    toy_game_actor_reset_combat(a,1);
    int slot=rf_idle_lab.count++;
    rf_idle_lab.ids[slot]=id;rf_idle_lab.generations[slot]=a->combat_generation;
    return 0;
}
static void rf_idle_lab_step(struct rf_game_runtime *runtime,int dt_ms)
{
    struct rasterfall_session *s=runtime->session;
    struct toy_game *g=&s->game_state;
    int requested=rf_labs.requested[RF_LAB_IDLE_RIFLE];
    if(rf_idle_lab.count && rf_idle_lab.world!=s->scene_local.world_generation) {
        rf_idle_lab_clear(s);rf_labs.requested[RF_LAB_IDLE_RIFLE]=0;return;
    }
    if(s->world_id!=RASTERFALL_WORLD_OUTPOST || runtime->net.mode!=RASTERFALL_NET_OFF ||
        rf_perf_lab.running || rf_combat_lab.running || !requested) {
        rf_idle_lab_clear(s);return;
    }
    if(runtime->lifecycle_paused || g->state!=TOY_GAME_PLAYING)return;
    const struct rf_map_runtime_region *area=rf_map_runtime_find_region(&s->map_ops.runtime,"idle_rifle_lab_area");
    if(!area)return;
    if(!rf_idle_lab.count) {
        static const int characters[]={RASTERFALL_CHARACTER_NONE,RASTERFALL_CHARACTER_RF_RIFLEMAN,
            RASTERFALL_CHARACTER_SQUAD_B_HEAVY,RASTERFALL_CHARACTER_SQUAD_A_RECON};
        rf_idle_lab.world=s->scene_local.world_generation;
        for(int i=0;i<RF_IDLE_LAB_ACTORS;++i)
            if(rf_idle_lab_add(g,characters[i],area->origin_x-2700+i*1800,area->origin_z,0)<0) {
                rf_idle_lab_clear(s);rf_labs.requested[RF_LAB_IDLE_RIFLE]=0;
                s->banner_text="LIVE AI LAB: NO FREE ACTOR SLOTS";s->banner_ms=3000;return;
            }
    }
    if(rf_idle_lab.mode!=requested) {
        if(rf_idle_lab.count>RF_IDLE_LAB_ACTORS) {
            rf_idle_lab_remove(g,RF_IDLE_LAB_ACTORS);rf_idle_lab.count=RF_IDLE_LAB_ACTORS;
        }
        rf_idle_lab.mode=requested;rf_idle_lab.elapsed_ms=0;
        for(int i=0;i<RF_IDLE_LAB_ACTORS;++i) {
            struct toy_game_actor *a=rf_idle_lab_actor(g,i);
            if(a) {a->deployment_x=area->origin_x-2700+i*1800;a->deployment_z=area->origin_z;}
        }
        if(requested==3 && rf_idle_lab_add(g,0,area->origin_x,area->origin_z+2800,1)<0) {
            rf_labs.requested[RF_LAB_IDLE_RIFLE]=1;return;
        }
    }
    rf_idle_lab.elapsed_ms=(rf_idle_lab.elapsed_ms+dt_ms)%12000;
    if(requested==2)for(int i=0;i<RF_IDLE_LAB_ACTORS;++i) {
        struct toy_game_actor *a=rf_idle_lab_actor(g,i);
        if(a)a->deployment_z=area->origin_z+((rf_idle_lab.elapsed_ms/6000)%2?1600:-1600);
    }
    if(requested==3) {
        struct toy_game_actor *a=rf_idle_lab_actor(g,RF_IDLE_LAB_ACTORS);
        if(!a || a->state!=TOY_GAME_ACTOR_ALIVE)rf_labs.requested[RF_LAB_IDLE_RIFLE]=1;
    }
}
static void rf_idle_lab_display(struct rasterfall_session *s,int suspended)
{
    char text[192];
    int mode=rf_labs.requested[RF_LAB_IDLE_RIFLE];
    const char *status=mode && suspended?"SUSPENDED":rf_idle_lab_modes[mode];
    snprintf(text,sizeof(text),"LIVE AI / %s",status);
    rasterfall_render_terminal_set("idle_rifle_lab",text);
    snprintf(text,sizeof(text),"\033MRF / LIVE AI\n\033F%s\n\033A[E] NEXT MODE\n\033MIDLE / WALK / TARGET / OFF",status);
    rasterfall_render_terminal_set("idle_rifle_lab_computer",text);
    static const char *names[]={"BLOCK","RIFLEMAN","HEAVY","SCOUT"};
    for(int i=0;i<RF_IDLE_LAB_ACTORS;++i) {
        struct toy_game_actor *a=rf_idle_lab_actor(&s->game_state,i);
        char channel[40];snprintf(channel,sizeof(channel),"idle_rifle_lane_%d",i);
        snprintf(text,sizeof(text),"%s / %s",names[i],!a?"OFF":a->reloading?"RELOAD":
            a->combat_target.kind>=0?"TARGET":a->moving?"MOVING":"IDLE");
        rasterfall_render_terminal_set(channel,text);
    }
}
static void rf_idle_lab_camera(const struct rasterfall_session *s,struct camera *camera)
{
    rf_lab_camera_position(s,camera,"idle_rifle_lab_area",0,5600);
    camera->y=1600;camera->sy=0;camera->cy=-1024;
    camera->pitch_sy=-280;camera->pitch_cy=985;
}
