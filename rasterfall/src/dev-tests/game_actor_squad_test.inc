/* Stable lifetime/priority/work contracts, exercised through real actor AI. */
static int game_actor_squad_logic_test(void)
{
    static struct toy_game squad_game;
    static struct toy_map_primitive squad_ground;
    static struct toy_map_primitive squad_obstacle[2];
    struct toy_game_actor_identity refs[25],anchor;
    struct toy_game_squad_handle handles[5],old_handle,partial;
    int i,j,x,z,px,pz,slot;
    unsigned int reserve;
#define SQUAD_CHECK(condition,code) do { if(!(condition)) { \
    __fprintf(2,"ACTOR-SQUAD contract failed %d\n",code);return code; } } while(0)
    toy_game_init(&squad_game,719);
    memset(&squad_ground,0,sizeof(squad_ground));
    squad_ground.shape=TOY_MAP_PRIMITIVE_FLAT;
    squad_ground.flags=TOY_MAP_PRIMITIVE_WALKABLE;
    squad_ground.minx=squad_ground.minz=-50000;
    squad_ground.maxx=squad_ground.maxz=50000;
    toy_game_set_primitives(&squad_game,&squad_ground,1,50000);
    squad_game.external_director=1;
    for(i=0;i<25;i++) {
        int index=toy_game_add_ai(&squad_game,TOY_GAME_AI_LEVEL_2,
            (i%5-2)*720,(i/5)*4000,"Squad contract")-1;
        SQUAD_CHECK(index>0,1);
        refs[i].index=index;refs[i].actor_id=squad_game.actors[index].actor_id;
        refs[i].generation=squad_game.actors[index].combat_generation;
        squad_game.actors[index].fire_enabled=0;
    }
    for(i=0;i<5;i++) {
        SQUAD_CHECK(toy_game_squad_bind(&squad_game,TOY_GAME_FACTION_ALLIED,
            &refs[i*5],5,&handles[i]),2);
        SQUAD_CHECK(toy_game_squad_assault(&squad_game,handles[i],0,20000),3);
    }
    SQUAD_CHECK(!toy_game_squad_bind(&squad_game,TOY_GAME_FACTION_ALLIED,refs,1,&partial),4);
    for(i=0;i<96;i++) {
        toy_game_update_ai_teammates(&squad_game,16);
        SQUAD_CHECK(squad_game.squad_profile.membership_checks+
            squad_game.squad_profile.resolve_checks+squad_game.squad_profile.anchor_checks<=60,5);
        SQUAD_CHECK(squad_game.squad_profile.refreshes<=2 &&
            squad_game.squad_profile.candidate_probes<=10,6);
    }
    x=-1;z=-1;
    if(!squad_game.squads[0].members[0].goal_valid)
        __fprintf(2,"ACTOR-SQUAD no initial goal leader=%d column=%d fit=%d refresh=%d probes=%d eligible=%d room=%d\n",
            squad_game.squads[0].leader,squad_game.squads[0].column,squad_game.squads[0].fit_streak,
            squad_game.squad_profile.refreshes,squad_game.squad_profile.candidate_probes,
            squad_game.squads[0].members[0].eligible,squad_game.room_limit);
    SQUAD_CHECK(toy_game_squad_actor_goal(&squad_game,refs[0].index,&x,&z),7);
    SQUAD_CHECK(x==0 && z==20000,8);
    /* Explicit independent MOVE and STOP remain unchanged and win every tick. */
    struct toy_game_actor *a=&squad_game.actors[refs[0].index];
    a->command_destination_active=1;a->command_x=a->x+1500;a->command_z=a->z;
    px=a->x;pz=a->z;
    for(i=0;i<10;i++)toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(a->x>px && a->command_destination_active && a->command_z==pz,9);
    x=123;z=456;
    SQUAD_CHECK(!toy_game_squad_actor_goal(&squad_game,refs[0].index,&x,&z) && x==123 && z==456,10);
    SQUAD_CHECK(toy_game_squad_actor_goal(&squad_game,refs[2].index,&x,&z),11);
    a->command_x=a->x;a->command_z=a->z;px=a->x;pz=a->z;
    for(i=0;i<40;i++)toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(a->x==px && a->z==pz && a->command_destination_active,12);
    /* Claimed rescue suppresses optional goals, even with a valid cached goal. */
    a=&squad_game.actors[refs[2].index];a->ai_rescue_active=1;
    SQUAD_CHECK(!toy_game_squad_actor_goal(&squad_game,refs[2].index,&x,&z),13);
    a->ai_rescue_active=0;
    /* Downed leader keeps its lifetime/ordinal; revived old leader does not
     * displace the current live leader or reacquire the same center slot. */
    toy_game_squad_reset(&squad_game);
    for(i=0;i<5;i++) {
        a=&squad_game.actors[refs[i].index];a->command_destination_active=0;
        a->ai_rescue_active=0;
    }
    SQUAD_CHECK(toy_game_squad_bind(&squad_game,TOY_GAME_FACTION_ALLIED,refs,5,&handles[0]),14);
    SQUAD_CHECK(toy_game_squad_assault(&squad_game,handles[0],0,18000),15);
    toy_game_update_ai_teammates(&squad_game,16);
    a=&squad_game.actors[refs[0].index];
    a->state=TOY_GAME_ACTOR_DOWNED;a->hp=0;a->x=-20000;a->z=-20000;
    toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(squad_game.squads[handles[0].index].leader==1 &&
        squad_game.squads[handles[0].index].members[0].active,16);
    SQUAD_CHECK(toy_game_revive_actor(&squad_game,refs[0].index,TOY_GAME_REVIVE_MS),17);
    toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(squad_game.squads[handles[0].index].leader==1,18);
    for(i=0;i<5;i++)for(j=0;j<i;j++)
        SQUAD_CHECK(squad_game.squads[handles[0].index].members[i].slot!=
            squad_game.squads[handles[0].index].members[j].slot,19);
    /* Reused ID, generation and faction invalidation clear references, not actors. */
    a=&squad_game.actors[refs[2].index];a->actor_id+=1000;
    toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(!squad_game.squad_lookup[refs[2].index] && a->active,20);
    a=&squad_game.actors[refs[3].index];a->combat_generation++;
    toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(!squad_game.squad_lookup[refs[3].index] && a->active,21);
    a=&squad_game.actors[refs[4].index];a->faction=TOY_GAME_FACTION_HOSTILE;
    toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(!squad_game.squad_lookup[refs[4].index] && a->active,22);
    old_handle=handles[0];
    SQUAD_CHECK(toy_game_squad_release(&squad_game,old_handle),23);
    SQUAD_CHECK(!toy_game_squad_assault(&squad_game,old_handle,0,0),24);
    SQUAD_CHECK(toy_game_squad_bind(&squad_game,TOY_GAME_FACTION_ALLIED,&refs[5],1,&partial),25);
    SQUAD_CHECK(partial.generation!=old_handle.generation,26);
    /* Partial groups grow without deleting a sixth actor or changing inventory. */
    for(i=6;i<10;i++)SQUAD_CHECK(toy_game_squad_append(&squad_game,partial,refs[i]),27);
    SQUAD_CHECK(!toy_game_squad_append(&squad_game,partial,refs[10]),28);
    SQUAD_CHECK(toy_game_squad_bind(&squad_game,TOY_GAME_FACTION_ALLIED,&refs[10],1,&handles[1]),29);
    SQUAD_CHECK(squad_game.squads[partial.index].count+
        squad_game.squads[handles[1].index].count==6,30);
    a=&squad_game.actors[refs[5].index];slot=a->current_slot;
    reserve=(unsigned int)a->slots[slot].reserve;px=a->slots[slot].mag;
    SQUAD_CHECK(toy_game_squad_assault(&squad_game,partial,0,12000),31);
    for(i=0;i<64;i++)toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK((unsigned int)a->slots[slot].reserve==reserve && a->slots[slot].mag==px,32);
    /* FOLLOW anchor loss and navigation rebuild retire cached goals/handles. */
    anchor.index=0;anchor.actor_id=squad_game.actors[0].actor_id;
    anchor.generation=squad_game.actors[0].combat_generation;
    SQUAD_CHECK(toy_game_squad_follow(&squad_game,partial,anchor),33);
    for(i=0;i<20;i++)toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(toy_game_squad_actor_goal(&squad_game,refs[5].index,&px,&pz),36);
    squad_game.actors[0].sy=-1024;squad_game.actors[0].cy=0;
    for(i=0;i<20;i++)toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(toy_game_squad_actor_goal(&squad_game,refs[5].index,&x,&z) && x==px && z==pz,37);
    squad_game.actors[0].x+=720;
    for(i=0;i<20;i++)toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(squad_game.squads[partial.index].sy==1024 &&
        squad_game.squads[partial.index].cy==0,38);
    squad_game.actors[0].faction=TOY_GAME_FACTION_HOSTILE;
    x=123;z=456;
    SQUAD_CHECK(!toy_game_squad_actor_goal(&squad_game,refs[5].index,&x,&z) &&
        x==123 && z==456,42);
    toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(squad_game.squads[partial.index].leader==-1 &&
        !toy_game_squad_actor_goal(&squad_game,refs[5].index,&x,&z),43);
    squad_game.actors[0].faction=TOY_GAME_FACTION_ALLIED;
    for(i=0;i<20;i++)toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(toy_game_squad_actor_goal(&squad_game,refs[5].index,&x,&z),44);
    /* Public queries cannot use a reused FOLLOW anchor before the next tick.
     * Check each identity component independently, preserving output on failure. */
    px=x;pz=z;squad_game.actors[0].combat_generation++;
    x=123;z=456;
    SQUAD_CHECK(!toy_game_squad_actor_goal(&squad_game,refs[5].index,&x,&z) &&
        x==123 && z==456,45);
    squad_game.actors[0].combat_generation--;squad_game.actors[0].actor_id++;
    SQUAD_CHECK(!toy_game_squad_actor_goal(&squad_game,refs[5].index,&x,&z) &&
        x==123 && z==456,46);
    squad_game.actors[0].actor_id--;
    SQUAD_CHECK(toy_game_squad_actor_goal(&squad_game,refs[5].index,&x,&z) &&
        x==px && z==pz,47);
    squad_game.actors[0].combat_generation++;
    toy_game_update_ai_teammates(&squad_game,16);
    SQUAD_CHECK(!toy_game_squad_actor_goal(&squad_game,refs[5].index,&x,&z),34);
    toy_game_rebuild_navigation(&squad_game);
    SQUAD_CHECK(!toy_game_squad_assault(&squad_game,partial,0,0) &&
        !squad_game.squad_lookup[refs[5].index],35);
    /* A locally fitting wedge with a blocked final slot must remain a column.
     * The 180-RFU arrival band makes these different queries observable without
     * naming a map or door. Previously this entered/exited every other cadence. */
    toy_game_init(&squad_game,721);
    memcpy(&squad_obstacle[0],&squad_ground,sizeof(squad_ground));
    memset(&squad_obstacle[1],0,sizeof(squad_obstacle[1]));
    squad_obstacle[1].shape=TOY_MAP_PRIMITIVE_BOX;
    squad_obstacle[1].flags=TOY_MAP_PRIMITIVE_COLLISION;
    squad_obstacle[1].minx=-900;squad_obstacle[1].maxx=-500;
    squad_obstacle[1].minz=3420;squad_obstacle[1].maxz=3620;
    squad_obstacle[1].surface_y0=squad_obstacle[1].surface_y1=1400;
    toy_game_set_primitives(&squad_game,squad_obstacle,2,50000);
    for(i=0;i<2;i++) {
        int index=toy_game_add_ai(&squad_game,TOY_GAME_AI_LEVEL_2,i?-720:0,i?2400:3820,"COLUMN FIT")-1;
        refs[i].index=index;refs[i].actor_id=squad_game.actors[index].actor_id;
        refs[i].generation=squad_game.actors[index].combat_generation;
        squad_game.actors[index].fire_enabled=0;
    }
    SQUAD_CHECK(toy_game_squad_bind(&squad_game,TOY_GAME_FACTION_ALLIED,refs,2,&partial) &&
        toy_game_squad_assault(&squad_game,partial,0,4000),39);
    px=0;pz=0;
    for(i=0;i<160;i++) {
        toy_game_update_ai_teammates(&squad_game,16);
        px+=squad_game.squad_profile.column_entries;pz+=squad_game.squad_profile.column_exits;
        SQUAD_CHECK(squad_game.squad_profile.candidate_probes<=10 &&
            squad_game.squad_profile.refreshes<=2,40);
    }
    SQUAD_CHECK(px==1 && pz==0 && squad_game.squads[partial.index].column,41);
    __printf("ACTOR-SQUAD identity/leader/rescue/MOVE/STOP/partial/reset/finite-ammo/stationary-turn/budget passed\n");
#undef SQUAD_CHECK
    return 0;
}
