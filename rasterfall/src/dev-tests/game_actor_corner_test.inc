/* A body-valid strip around a box has no straight conservative-grid anchor.
 * Endpoint repair must prove a bounded same-floor bend, not relax collision. */
static int actor_corner_logic_test(void)
{
    static struct toy_game corner_game;
    struct toy_map_primitive world[7];
    struct toy_game_update_profile profile;
    int fixture,tick,planned,x,z,range;
#define CORNER_CHECK(condition,code) do { if(!(condition)) { \
    __fprintf(2,"ACTOR-CORNER contract failed %d\n",code);return code; } } while(0)
    for(fixture=0;fixture<3;fixture++) {
        memset(world,0,sizeof(world));
        world[0].shape=TOY_MAP_PRIMITIVE_FLAT;world[0].flags=TOY_MAP_PRIMITIVE_WALKABLE;
        world[0].minx=world[0].minz=-6000;world[0].maxx=world[0].maxz=6000;
        world[1].shape=world[2].shape=TOY_MAP_PRIMITIVE_BOX;
        world[1].flags=world[2].flags=TOY_MAP_PRIMITIVE_COLLISION;
        world[1].minx=world[1].minz=-500;world[1].maxx=world[1].maxz=-100;
        world[1].surface_y0=world[1].surface_y1=1200;
        world[2].minx=-4000;world[2].maxx=4000;world[2].minz=900;world[2].maxz=1200;
        world[2].surface_y0=world[2].surface_y1=1200;
        int count=3;
        if(fixture==1) {
            /* The start remains valid but a closed box surrounds its region. */
            for(int i=3;i<7;i++) {
                world[i].shape=TOY_MAP_PRIMITIVE_BOX;world[i].flags=TOY_MAP_PRIMITIVE_COLLISION;
                world[i].surface_y0=world[i].surface_y1=1200;
            }
            world[3].minx=-1100;world[3].maxx=-800;world[3].minz=-1100;world[3].maxz=1100;
            world[4].minx=500;world[4].maxx=800;world[4].minz=-1100;world[4].maxz=1100;
            world[5].minx=-1100;world[5].maxx=800;world[5].minz=-1100;world[5].maxz=-800;
            world[6].minx=-1100;world[6].maxx=800;world[6].minz=800;world[6].maxz=1100;
            count=7;
        } else if(fixture==2) {
            world[0].maxx=300;
            world[3].shape=TOY_MAP_PRIMITIVE_FLAT;world[3].flags=TOY_MAP_PRIMITIVE_WALKABLE;
            world[3].minx=300;world[3].maxx=6000;world[3].minz=-6000;world[3].maxz=6000;
            world[3].surface_y0=world[3].surface_y1=900;count=4;
        }
        toy_game_init(&corner_game,719);corner_game.external_director=1;
        toy_game_set_primitives(&corner_game,world,count,6000);
        struct toy_game_actor *a=toy_game_local_player_actor(&corner_game);
        a->x=-50;a->z=87;
        memset(&profile,0,sizeof(profile));corner_game.update_profile=&profile;
        CORNER_CHECK(!toy_game_position_blocked_at_height(&corner_game,a->x,a->z,180,0),1);
        x=fixture==1?2500:1000;z=-200;
        if(fixture==1)CORNER_CHECK(!toy_game_position_blocked_at_height(&corner_game,x,z,180,0),12);
        planned=toy_game_actor_navigation_target(&corner_game,a,x,z,40,16,&x,&z);
        if(fixture) {
            CORNER_CHECK(!planned && !a->nav_active,fixture+2);
            CORNER_CHECK(profile.nav_candidates<=8,13);
            continue;
        }
        if(!planned || !a->nav_active || x<=a->x || z!=a->z)
            __fprintf(2,"ACTOR-CORNER initial planned=%d nav=%d waypoint=%d,%d searches=%u candidates=%u short=%u\n",
                planned,a->nav_active,x,z,profile.nav_searches,profile.nav_candidates,profile.nav_short_queries);
        CORNER_CHECK(planned && a->nav_active && x>a->x && z==a->z,5);
        CORNER_CHECK(profile.nav_searches==0 && profile.nav_candidates<=8,6);
        range=(TOY_GAME_SHORT_CONNECTION_RANGE+corner_game.nav_cell_size-1)/corner_game.nav_cell_size;
        CORNER_CHECK(profile.nav_short_queries<=(unsigned)((2*range+1)*(2*range+1)+16),7);
        CORNER_CHECK(toy_game_short_connection(&corner_game,a->x,a->z,x,z,180,0),8);
        for(tick=0;tick<250 && ((long long)(a->x-1000)*(a->x-1000)+
            (long long)(a->z+200)*(a->z+200)>180LL*180);tick++) {
            int sx=1000,sz=-200;
            CORNER_CHECK(toy_game_actor_navigation_target(&corner_game,a,1000,-200,40,16,&sx,&sz),9);
            long long dx=(long long)sx-a->x,dz=(long long)sz-a->z;
            int length=isqrt(dx*dx+dz*dz);
            if(length)toy_game_move_actor_sliding(&corner_game,a,(int)(dx*40/length),(int)(dz*40/length));
            CORNER_CHECK(!toy_game_position_blocked_at_height(&corner_game,a->x,a->z,180,a->ground_y) &&
                a->ground_y==0,10);
        }
        CORNER_CHECK((long long)(a->x-1000)*(a->x-1000)+(long long)(a->z+200)*(a->z+200)<=180LL*180,11);
    }
    __printf("ACTOR-CORNER bounded same-floor repair, collision, closed-region and elevated-exit: PASS\n");
#undef CORNER_CHECK
    return 0;
}
