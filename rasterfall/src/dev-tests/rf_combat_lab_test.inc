/* Stable ownership contract: a trial reset must never erase unrelated state. */
static int rf_combat_lab_logic_test(struct rf_game_runtime *runtime)
{
    struct toy_game *g=&runtime->session->game_state;
    const struct rf_map_runtime_region *field=rf_map_runtime_find_region(&runtime->session->map_ops.runtime,"perf_low_area");
    const struct rf_map_runtime_region *other=rf_map_runtime_find_region(&runtime->session->map_ops.runtime,"perf_enemy_area");
    if(!field || !other)return -1;
    int id=toy_game_add_ai(g,TOY_GAME_AI_LEVEL_1,field->origin_x,field->origin_z,"FOREIGN SENTINEL");
    struct toy_game_actor *foreign=toy_game_actor_by_id(g,id);
    if(!foreign)return -2;
    rf_combat_lab.suite=1;
    if(rf_combat_start(runtime,18,1,1337,1)==0)return -3;
    foreign->x=other->origin_x;foreign->z=other->origin_z;
    struct toy_game_actor foreign_before=*foreign,player_before=*toy_game_local_player_actor(g);
    unsigned char slots[TOY_GAME_MAX_ENEMIES];
    for(int i=0;i<TOY_GAME_MAX_ENEMIES;++i)slots[i]=g->enemies[i].active!=0;
    struct toy_game_box box={other->origin_x-40,other->origin_x+40,other->origin_z-40,other->origin_z+40,0,0};
    if(toy_game_spawn_horde_type(g,TOY_GAME_ENEMY_PURSUIT_COMMON,1,1,&box,1,0)!=1)return -4;
    int enemy=-1;
    for(int i=0;i<TOY_GAME_MAX_ENEMIES;++i)if(!slots[i] && g->enemies[i].active){enemy=i;break;}
    if(enemy<0)return -5;
    struct toy_game_enemy enemy_before=g->enemies[enemy];
    int count=g->enemies_alive,flags=runtime->session->flag_count;
    int original_money=g->money;
    uint64_t rng=g->rng;
    for(int repeat=0;repeat<2;++repeat) {
        if(rf_combat_start(runtime,repeat==0?20:18,1,1337,repeat+1)<0)return -6;
        if(repeat==0) {
            int before[RF_COMBAT_MEMBERS][4],allied_count=0,hostile_count=0;
            int fi=rf_combat_lab.flag_index;
            if(fi<0 || runtime->session->flags[fi].carrier_id!=-1)return -14;
            for(int i=0;i<rf_combat_lab.member_count;++i) {
                const struct rf_combat_member *m=&rf_combat_lab.members[i];
                if(m->enemy_slot>=0)continue;
                struct toy_game_actor *a=rf_combat_owned_actor(g,m);
                if(!a)return -15;
                before[i][0]=a->deployment_x;before[i][1]=a->deployment_z;
                before[i][2]=a->x;before[i][3]=a->z;
                /* Even a stale matching flag reference cannot command an enemy. */
                if(a->faction==TOY_GAME_FACTION_HOSTILE)a->flag_index=fi;
            }
            rasterfall_session_set_rts(runtime->session,1);
            if(!rasterfall_session_rts_move_flag(runtime->session,fi,
                field->origin_x+3200,field->origin_z-2400))return -16;
            for(int i=0;i<rf_combat_lab.member_count;++i) {
                const struct rf_combat_member *m=&rf_combat_lab.members[i];
                if(m->enemy_slot>=0)continue;
                const struct toy_game_actor *a=rf_combat_owned_actor(g,m);
                if(a->x!=before[i][2] || a->z!=before[i][3])return -17;
                if(a->faction==TOY_GAME_FACTION_HOSTILE) {
                    hostile_count++;
                    if(a->deployment_x!=before[i][0] || a->deployment_z!=before[i][1] ||
                       rasterfall_session_rts_order_actor(runtime->session,(int)(a-g->actors),a->actor_id,
                           a->combat_generation,field->origin_x,field->origin_z,0))return -18;
                    continue;
                }
                allied_count++;
                if(a->flag_index!=-1 || a->deployment_x!=before[i][0] || a->deployment_z!=before[i][1])return -19;
                int tx=field->origin_x+allied_count*600,tz=field->origin_z-2400;
                if(!rasterfall_session_rts_order_actor(runtime->session,(int)(a-g->actors),a->actor_id,
                    a->combat_generation,tx,tz,0) || !a->command_destination_active ||
                    a->command_x!=tx || a->command_z!=tz)return -20;
                for(int j=0;j<i;++j) {
                    const struct rf_combat_member *previous=&rf_combat_lab.members[j];
                    if(previous->enemy_slot>=0)continue;
                    const struct toy_game_actor *b=rf_combat_owned_actor(g,previous);
                    if(b->faction==TOY_GAME_FACTION_ALLIED &&
                       a->command_x==b->command_x && a->command_z==b->command_z)return -20;
                }
            }
            if(allied_count!=4 || !hostile_count)return -21;
            rasterfall_session_set_rts(runtime->session,0);
        }
        int money_before=g->money,target=-1;
        struct toy_game_actor *subject=rf_combat_subject(g),*replacement=NULL;
        struct toy_game_actor replacement_before;
        for(int i=0;i<rf_combat_lab.member_count;++i)
            if(rf_combat_lab.members[i].enemy_slot>=0){target=rf_combat_lab.members[i].enemy_slot;break;}
        if(!subject || target<0 || !toy_game_apply_reported_hit(g,subject,target,g->enemies[target].hp))return -9;
        if(subject->combat_stats.money_earned<=0)return -10;
        g->money+=37; /* Independent income must survive the lab refund. */
        if(repeat==1) {
            struct rf_combat_member *owned=&rf_combat_lab.members[1];
            struct toy_game_actor *a=rf_combat_owned_actor(g,owned);
            if(!a)return -11;
            memset(a,0,sizeof(*a));
            int next=toy_game_add_ai(g,TOY_GAME_AI_LEVEL_1,other->origin_x+1000,other->origin_z,"REUSED SLOT");
            replacement=toy_game_actor_by_id(g,next);
            if(!replacement || next!=owned->actor_id || replacement->combat_generation==owned->generation)return -12;
            replacement_before=*replacement;
        }
        rf_combat_finish(runtime,3);
        if(memcmp(foreign,&foreign_before,sizeof(*foreign)) ||
           memcmp(&g->enemies[enemy],&enemy_before,sizeof(enemy_before)) ||
           memcmp(toy_game_local_player_actor(g),&player_before,sizeof(player_before)) ||
           g->enemies_alive!=count || g->rng!=rng || runtime->session->flag_count!=flags || g->money!=money_before+37)return -7;
        if(replacement) {
            if(memcmp(replacement,&replacement_before,sizeof(*replacement)))return -13;
            memset(replacement,0,sizeof(*replacement));
        }
        if(rf_combat_lab.current.status!=3 || rf_combat_lab.current.ttk_ms!=-1)return -8;
    }
    memset(foreign,0,sizeof(*foreign));memset(&g->enemies[enemy],0,sizeof(g->enemies[enemy]));
    g->enemies_alive--;
    g->money=original_money;
    rf_combat_lab.suite=0;rf_combat_lab.result_open=0;rf_combat_lab.result_count=0;
    __printf("COMBAT-LAB RTS formation / faction isolation / owned cleanup / generation / reward refund / repeat / no false TTK passed\n");
    return 0;
}
