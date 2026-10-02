/* Presentation-only treadmill stations. Both renderers consume the same
 * sampled values; geometry extraction never advances this clock/history. */
struct outpost_showcase_entry { int family, type, x, z, moving; };
#define SHOWCASE_PAIR(family,type,x) \
    {RASTERFALL_ENEMY_VISUAL_##family,TOY_GAME_ENEMY_PURSUIT_##type,x,2048,0}, \
    {RASTERFALL_ENEMY_VISUAL_##family,TOY_GAME_ENEMY_PURSUIT_##type,x,-1536,1}
static const struct outpost_showcase_entry outpost_showcase[] = {
    SHOWCASE_PAIR(BLOCK_INFECTED,COMMON,-4000),
    SHOWCASE_PAIR(BLOCK_INFECTED,FAST,-2500),
    SHOWCASE_PAIR(BLOCK_INFECTED,HEAVY,-1000),
    SHOWCASE_PAIR(HUMANOID_INFECTED,COMMON,1000),
    SHOWCASE_PAIR(HUMANOID_INFECTED,FAST,2500),
    SHOWCASE_PAIR(HUMANOID_INFECTED,HEAVY,4000)
};
#undef SHOWCASE_PAIR
_Static_assert(sizeof(outpost_showcase)/sizeof(outpost_showcase[0]) ==
    RASTERFALL_OUTPOST_SHOWCASE_COUNT,"showcase capacity must match stations");
static int outpost_showcase_visible;
static uint64_t outpost_showcase_time_us, outpost_showcase_step_us;
static struct rasterfall_infected_sample
    outpost_showcase_motion[RASTERFALL_OUTPOST_SHOWCASE_COUNT];

#define OUTPOST_WALK_NORTH_Z 3044
#define OUTPOST_WALK_SOUTH_Z -2956
struct outpost_walk_entry {
    int type, x, z, direction;
    struct rasterfall_infected_sample motion;
};
static struct outpost_walk_entry outpost_walk[] = {
    {.type=TOY_GAME_ENEMY_PURSUIT_COMMON,.x=-3200,
     .z=OUTPOST_WALK_NORTH_Z,.direction=-1},
    {.type=TOY_GAME_ENEMY_PURSUIT_FAST,.x=0,
     .z=OUTPOST_WALK_NORTH_Z,.direction=-1},
    {.type=TOY_GAME_ENEMY_PURSUIT_HEAVY,.x=3200,
     .z=OUTPOST_WALK_NORTH_Z,.direction=-1}
};
static int outpost_walk_visible;
static uint64_t outpost_walk_time_us, outpost_walk_step_us;

void rasterfall_render_set_outpost_walk(int visible,uint64_t time_us)
{
    int reset=!visible || !outpost_walk_visible ||
        time_us<outpost_walk_time_us || time_us-outpost_walk_time_us>1000000;
    if (reset) {
        outpost_walk_step_us=time_us;
        for (int i=0;i<RASTERFALL_OUTPOST_WALK_COUNT;++i) {
            struct rasterfall_infected_motion previous={0};
            outpost_walk[i].z=OUTPOST_WALK_NORTH_Z;
            outpost_walk[i].direction=-1;
            rasterfall_infected_sample_motion(&previous,1,outpost_walk[i].x,
                outpost_walk[i].z,time_us,&outpost_walk[i].motion);
        }
    }
    outpost_walk_visible=visible;
    outpost_walk_time_us=time_us;
    if (!visible) return;
    while (time_us-outpost_walk_step_us>=16000) {
        outpost_walk_step_us+=16000;
        for (int i=0;i<RASTERFALL_OUTPOST_WALK_COUNT;++i) {
            struct outpost_walk_entry *entry=&outpost_walk[i];
            const struct toy_game_enemy_info *info=toy_game_enemy_info(entry->type);
            int distance=(info->speed_min+info->speed_max)/2;
            int next=entry->z+entry->direction*distance;
            if (next<OUTPOST_WALK_SOUTH_Z) {
                next=OUTPOST_WALK_SOUTH_Z+(OUTPOST_WALK_SOUTH_Z-next);
                entry->direction=1;
            } else if (next>OUTPOST_WALK_NORTH_Z) {
                next=OUTPOST_WALK_NORTH_Z-(next-OUTPOST_WALK_NORTH_Z);
                entry->direction=-1;
            }
            entry->z=next;
            rasterfall_infected_sample_motion(&entry->motion.next,1,entry->x,
                entry->z,outpost_walk_step_us,&entry->motion);
        }
    }
}

void rasterfall_render_set_outpost_showcase(int visible,uint64_t time_us)
{
    int reset=!visible || !outpost_showcase_visible ||
        time_us<outpost_showcase_time_us ||
        time_us-outpost_showcase_time_us>1000000;
    if (reset) {
        memset(outpost_showcase_motion,0,sizeof(outpost_showcase_motion));
        outpost_showcase_step_us=time_us;
        for (int i=0;i<RASTERFALL_OUTPOST_SHOWCASE_COUNT;++i) {
            struct rasterfall_infected_motion previous={0};
            rasterfall_infected_sample_motion(&previous,1,0,0,time_us,
                &outpost_showcase_motion[i]);
        }
    }
    outpost_showcase_visible=visible;
    outpost_showcase_time_us=time_us;
    if (!visible) return;
    /* Match gameplay's 16 ms movement units, using each type's midpoint speed.
     * Virtual displacement drives the shared sampler while stations stay put. */
    while (time_us-outpost_showcase_step_us>=16000) {
        outpost_showcase_step_us+=16000;
        for (int i=0;i<RASTERFALL_OUTPOST_SHOWCASE_COUNT;++i) {
            const struct toy_game_enemy_info *info=
                toy_game_enemy_info(outpost_showcase[i].type);
            struct rasterfall_infected_motion previous=outpost_showcase_motion[i].next;
            int distance=outpost_showcase[i].moving ?
                (info->speed_min+info->speed_max)/2 : 0;
            previous.x=0;
            rasterfall_infected_sample_motion(&previous,1,distance,0,
                outpost_showcase_step_us,&outpost_showcase_motion[i]);
        }
    }
}

static int render_outpost_showcase(struct toy_renderer *renderer,
    const struct camera *camera)
{
    int pixels=0;
    for (int i=0;i<RASTERFALL_OUTPOST_SHOWCASE_COUNT;++i) {
        const struct outpost_showcase_entry *entry=&outpost_showcase[i];
        struct toy_game_enemy model={0};
        model.active=1;model.type=entry->type;
        rasterfall_render_lab_point("character_lab_area",entry->x,entry->z,&model.x,&model.z);
        model.dir_z=1024;
        pixels+=render_infected_enemy(renderer,camera,&model,&model,-1,
            1000,0,entry->family,&outpost_showcase_motion[i]);
    }
    return pixels;
}

static int render_outpost_walk(struct toy_renderer *renderer,
    const struct camera *camera)
{
    int pixels=0;
    for (int i=0;i<RASTERFALL_OUTPOST_WALK_COUNT;++i) {
        const struct outpost_walk_entry *entry=&outpost_walk[i];
        struct toy_game_enemy model={0};
        model.active=1;model.type=entry->type;
        rasterfall_render_lab_point("walk_lab_area",entry->x,entry->z,&model.x,&model.z);
        model.dir_z=entry->direction*1024;
        pixels+=render_infected_enemy(renderer,camera,&model,&model,-1,
            1000,0,RASTERFALL_ENEMY_VISUAL_HUMANOID_INFECTED,&entry->motion);
    }
    return pixels;
}
