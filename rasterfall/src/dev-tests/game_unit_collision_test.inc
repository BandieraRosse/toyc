/* Protect physical movement, static planning, height and melee contracts.
 * Fixtures use pool members, not model geometry or current map coordinates. */
static int game_unit_collision_logic_test(void)
{
    static struct toy_game unit_game, clone_game;
    struct toy_map_primitive ground, wall;
    struct toy_game_actor *a, *b, probe;
    struct toy_game_enemy *e;
    int i, x, z, before_hp, bite_event;
    long long dx, dz;
#define UNIT_CHECK(condition,code) do { if (!(condition)) { \
    __fprintf(2,"UNIT-COLLISION contract failed %d at %d,%d\n",code,a->x,a->z); \
    return code; } } while (0)
    toy_game_init(&unit_game,719);
    memset(unit_game.actors+1,0,sizeof(unit_game.actors)-sizeof(unit_game.actors[0]));
    memset(unit_game.enemies,0,sizeof(unit_game.enemies));
    memset(&ground,0,sizeof(ground));
    ground.shape=TOY_MAP_PRIMITIVE_FLAT; ground.flags=TOY_MAP_PRIMITIVE_WALKABLE;
    ground.minx=ground.minz=-8000; ground.maxx=ground.maxz=8000;
    toy_game_set_primitives(&unit_game,&ground,1,8000);
    unit_game.external_director=1;
    a=&unit_game.actors[0]; b=&unit_game.actors[1];
    memcpy(b,a,sizeof(*b)); b->kind=TOY_GAME_ACTOR_AI; b->actor_id=2;
    b->ai_stationary=1; b->fire_enabled=0; b->x=b->z=0;
    a->x=-1000; a->z=0;
    UNIT_CHECK(!toy_game_try_move_actor(&unit_game,a,1000,0) && a->x==-1000,1);
    UNIT_CHECK(toy_game_move_actor_sliding(&unit_game,a,2000,0),2);
    UNIT_CHECK(a->x<=-2*TOY_GAME_PLAYER_RADIUS && a->x>=-370 && !a->z && !b->x,3);
    for (i=0;i<30;i++) toy_game_move_actor_sliding(&unit_game,a,80,0);
    UNIT_CHECK(a->x<=-2*TOY_GAME_PLAYER_RADIUS && !a->z,4);
    a->x=-700; a->z=-280;
    for (i=0;i<25;i++) {
        toy_game_move_actor_sliding(&unit_game,a,45,0);
        dx=(long long)a->x-b->x; dz=(long long)a->z-b->z;
        UNIT_CHECK(dx*dx+dz*dz>=4LL*TOY_GAME_PLAYER_RADIUS*TOY_GAME_PLAYER_RADIUS,5);
    }
    UNIT_CHECK(a->x>100 && a->z<-280,6);
    a->x=b->x; a->z=b->z;
    UNIT_CHECK(toy_game_move_actor_sliding(&unit_game,a,600,0) && a->x>=600,7);
    a->x=-100; a->z=0;
    UNIT_CHECK(!toy_game_try_move_actor(&unit_game,a,-50,0),8);
    UNIT_CHECK(toy_game_try_move_actor(&unit_game,a,-600,0),9);
    b->faction=TOY_GAME_FACTION_HOSTILE;
    a->x=-1000;
    UNIT_CHECK(!toy_game_try_move_actor(&unit_game,a,1000,0),10);
    b->state=TOY_GAME_ACTOR_DOWNED;
    UNIT_CHECK(toy_game_try_move_actor(&unit_game,a,1000,0),11);
    b->state=TOY_GAME_ACTOR_ALIVE; b->hp=0; a->x=-1000;
    UNIT_CHECK(toy_game_try_move_actor(&unit_game,a,1000,0),12);
    b->hp=b->max_hp; b->ground_y=RASTERFALL_HUMAN_HEIGHT_RFU+100; a->x=-1000;
    UNIT_CHECK(toy_game_try_move_actor(&unit_game,a,1000,0),13);
    b->ground_y=RASTERFALL_HUMAN_HEIGHT_RFU-100; a->x=-1000;
    UNIT_CHECK(!toy_game_try_move_actor(&unit_game,a,1000,0),14);
    b->ground_y=0;
    memcpy(&probe,a,sizeof(probe));
    UNIT_CHECK(toy_game_try_move_actor(&unit_game,&probe,1000,0),15);
    UNIT_CHECK(toy_game_short_connection(&unit_game,-1000,0,1000,0,TOY_GAME_PLAYER_RADIUS,0),16);
    x=1000; z=0;
    UNIT_CHECK(toy_game_actor_navigation_target(&unit_game,a,1000,0,40,16,&x,&z),17);
    a->airborne_ms=100; a->airborne_y=1200;
    UNIT_CHECK(!toy_game_move_actor_forced_swept(&unit_game,a,2000,0,1200,1200) && a->x==1000,18);
    a->x=-1000; a->airborne_y=100;
    UNIT_CHECK(toy_game_move_actor_forced_swept(&unit_game,a,2000,0,100,100) && a->x<0,19);
    a->x=-1000; a->airborne_y=2000;
    UNIT_CHECK(toy_game_move_actor_forced_swept(&unit_game,a,2000,0,2000,-1000) && a->x<1000,24);
    a->x=-1000; a->airborne_y=0; a->airborne_ms=0;
    /* Existing overlap cannot escape through a terrain wall. */
    memset(&wall,0,sizeof(wall)); wall.minx=-900; wall.maxx=-800;
    wall.minz=-2000; wall.maxz=2000; wall.base_y=0;
    wall.surface_y0=wall.surface_y1=1200;
    wall.shape=TOY_MAP_PRIMITIVE_BOX; wall.flags=TOY_MAP_PRIMITIVE_COLLISION;
    struct toy_map_primitive terrain[2];
    memcpy(&terrain[0],&ground,sizeof(ground)); memcpy(&terrain[1],&wall,sizeof(wall));
    toy_game_set_primitives(&unit_game,terrain,2,8000);
    a->x=b->x=-600;
    toy_game_move_actor_sliding(&unit_game,a,-1000,0);
    UNIT_CHECK(a->x>=wall.maxx+TOY_GAME_PLAYER_RADIUS,20);
    toy_game_set_primitives(&unit_game,&ground,1,8000);
    b->active=0; a->x=a->z=0;
    e=&unit_game.enemies[0]; memset(e,0,sizeof(*e));
    e->active=1; e->hp=100; e->speed=40; e->x=-1000;
    e->type=TOY_GAME_ENEMY_PURSUIT_COMMON;
    before_hp=a->hp;
    a->evasion.reserve_milli=0;
    a->evasion.pressure_ms=3000;
    for (i=0;i<70;i++) {
        toy_game_update_world(&unit_game,16);
        dx=(long long)e->x-a->x; dz=(long long)e->z-a->z;
        UNIT_CHECK(dx*dx+dz*dz>=(long long)(TOY_GAME_PLAYER_RADIUS+TOY_GAME_ENEMY_RADIUS)*
            (TOY_GAME_PLAYER_RADIUS+TOY_GAME_ENEMY_RADIUS),21);
    }
    if(a->hp>=before_hp) __fprintf(2,"UNIT-COLLISION melee enemy=%d,%d y=%d state=%d target=%d cooldown=%d reserve=%d\n",
        e->x,e->z,e->ground_y,unit_game.state,e->target_kind,e->bite_cooldown_ms,a->evasion.reserve_milli);
    UNIT_CHECK(a->hp<before_hp,22);
    /* A ready impact cooldown must not turn a bite into a counter-launch. */
    e->x=-(TOY_GAME_PLAYER_RADIUS+TOY_GAME_ENEMY_RADIUS+4); e->z=0;
    e->bite_cooldown_ms=0;
    a->knockback_cooldown_ms=0;
    a->evasion.reserve_milli=0; a->evasion.pressure_ms=3000;
    before_hp=a->hp; x=e->x; z=e->z;
    toy_game_drain_events(&unit_game,NULL,TOY_GAME_MAX_EVENTS);
    toy_game_update_world(&unit_game,16);
    {
        unsigned char events[TOY_GAME_MAX_EVENTS];
        int count=toy_game_drain_events(&unit_game,events,TOY_GAME_MAX_EVENTS);
        bite_event=0;
        for(i=0;i<count;i++) if(events[i]==TOY_GAME_EV_BITE) bite_event=1;
    }
    UNIT_CHECK(a->hp<before_hp && e->bite_cooldown_ms>0 &&
        bite_event,29);
    UNIT_CHECK(e->x==x && e->z==z && !e->airborne_ms &&
        !e->knockback_x && !e->knockback_z && !a->airborne_ms &&
        !a->knockback_x && !a->knockback_z && !a->knockback_cooldown_ms,30);
    memset(unit_game.enemies,0,sizeof(unit_game.enemies));
    for(i=0;i<2;i++) {
        e=&unit_game.enemies[i]; e->active=1; e->hp=100; e->speed=40;
        e->x=-1000; e->z=i ? 160 : -160; e->type=TOY_GAME_ENEMY_PURSUIT_COMMON;
    }
    for(i=0;i<70;i++) {
        toy_game_update_world(&unit_game,16);
        dx=(long long)unit_game.enemies[0].x-unit_game.enemies[1].x;
        dz=(long long)unit_game.enemies[0].z-unit_game.enemies[1].z;
        UNIT_CHECK(dx*dx+dz*dz>=4LL*TOY_GAME_ENEMY_RADIUS*TOY_GAME_ENEMY_RADIUS,27);
    }
    /* Actors are blocked by enemies too, including a larger body. */
    memset(unit_game.enemies,0,sizeof(unit_game.enemies));
    e=&unit_game.enemies[TOY_GAME_MAX_ENEMIES-1];
    e->active=1; e->hp=100; e->type=TOY_GAME_ENEMY_TANK;
    e->x=e->z=0; a->x=-1000; a->z=0;
    UNIT_CHECK(!toy_game_try_move_actor(&unit_game,a,1000,0),25);
    toy_game_move_actor_sliding(&unit_game,a,2000,0);
    UNIT_CHECK(a->x<=-(TOY_GAME_PLAYER_RADIUS+TOY_GAME_TANK_RADIUS),26);
    /* Every live candidate matters, including the last occupied pool slot. */
    memset(unit_game.enemies,0,sizeof(unit_game.enemies));
    for (i=1;i<TOY_GAME_MAX_ACTORS;i++) {
        memcpy(&unit_game.actors[i],a,sizeof(*a));
        unit_game.actors[i].x=3000; unit_game.actors[i].z=i*20;
    }
    b=&unit_game.actors[TOY_GAME_MAX_ACTORS-1]; b->x=b->z=0;
    a->x=-1000; a->z=0;
    UNIT_CHECK(!toy_game_try_move_actor(&unit_game,a,1000,0),23);
    memcpy(&clone_game,&unit_game,sizeof(clone_game));
    for(i=0;i<64;i++) {
        toy_game_move_actor_sliding(&unit_game,a,45,12);
        toy_game_move_actor_sliding(&clone_game,&clone_game.actors[0],45,12);
        UNIT_CHECK(!memcmp(unit_game.actors,clone_game.actors,sizeof(unit_game.actors)) &&
            !memcmp(unit_game.enemies,clone_game.enemies,sizeof(unit_game.enemies)) &&
            unit_game.rng==clone_game.rng,28);
    }
    __printf("UNIT-COLLISION blocking, slide, sweep, overlap escape, height, static planning and contact melee: PASS\n");
#undef UNIT_CHECK
    return 0;
}
