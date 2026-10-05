/* Game-owned optional autonomous intent. Explicit RTS and rescue are resolved
 * by actor AI before this policy. No squad query performs graph search. */
#define ACTOR_SQUAD_SPACING 720
#define ACTOR_SQUAD_CADENCE 256
#define ACTOR_SQUAD_PROBES 10

static int actor_squad_abs(int value) { return value < 0 ? -value : value; }

static int actor_squad_identity(const struct toy_game *g,
    struct toy_game_actor_identity identity)
{
    const struct toy_game_actor *a;
    if (identity.index < 0 || identity.index >= TOY_GAME_MAX_ACTORS) return 0;
    a = &g->actors[identity.index];
    return a->active && a->actor_id == identity.actor_id &&
        a->combat_generation == identity.generation;
}

static int actor_squad_eligible(const struct toy_game_actor *a)
{
    return a->kind == TOY_GAME_ACTOR_AI && a->state == TOY_GAME_ACTOR_ALIVE &&
        a->hp > 0 && !a->control_disabled && !a->airborne_ms &&
        !a->movement_hold_token && !a->base_core && !a->developer_only &&
        !a->animation_demo && !a->hit_test_dummy && !a->ai_stationary &&
        !a->command_destination_active && !a->ai_rescue_active;
}

void toy_game_squad_reset(struct toy_game *g)
{
    if (!g) return;
    memset(g->squads, 0, sizeof(g->squads));
    memset(g->squad_lookup, 0, sizeof(g->squad_lookup));
    memset(&g->squad_profile, 0, sizeof(g->squad_profile));
    g->squad_cursor = 0;
    if (!++g->next_squad_generation) ++g->next_squad_generation;
}

static struct toy_game_squad *actor_squad_handle(struct toy_game *g,
    struct toy_game_squad_handle handle)
{
    struct toy_game_squad *s;
    if (!g || handle.index < 0 || handle.index >= TOY_GAME_SQUAD_GROUPS) return NULL;
    s = &g->squads[handle.index];
    return s->active && s->generation == handle.generation ? s : NULL;
}

int toy_game_squad_bind(struct toy_game *g, int faction,
    const struct toy_game_actor_identity *members, int count,
    struct toy_game_squad_handle *out)
{
    int i, j, group = -1;
    struct toy_game_squad *s;
    if (!g || !members || !out || count < 1 || count > TOY_GAME_SQUAD_MEMBERS ||
        (faction != TOY_GAME_FACTION_ALLIED && faction != TOY_GAME_FACTION_HOSTILE)) return 0;
    for (i = 0; i < TOY_GAME_SQUAD_GROUPS; ++i)
        if (!g->squads[i].active) { group = i; break; }
    if (group < 0) return 0;
    for (i = 0; i < count; ++i) {
        const struct toy_game_actor *a;
        if (!actor_squad_identity(g, members[i])) return 0;
        a = &g->actors[members[i].index];
        if (g->squad_lookup[members[i].index] || a->kind != TOY_GAME_ACTOR_AI ||
            a->faction != faction || a->base_core || a->developer_only ||
            a->animation_demo || a->hit_test_dummy || a->ai_stationary) return 0;
        for (j = 0; j < i; ++j) if (members[j].index == members[i].index) return 0;
    }
    s = &g->squads[group]; memset(s, 0, sizeof(*s));
    s->active = 1; s->count = count; s->faction = faction; s->leader = -1;
    s->map_generation = g->navigation_generation; s->cy = 1024;
    if (!++g->next_squad_generation) ++g->next_squad_generation;
    s->generation = g->next_squad_generation;
    for (i = 0; i < count; ++i) {
        s->members[i].identity = members[i]; s->members[i].active = 1;
        s->members[i].slot = i;
        g->squad_lookup[members[i].index] =
            (unsigned char)(group * TOY_GAME_SQUAD_MEMBERS + i + 1);
    }
    out->index = group; out->generation = s->generation;
    return 1;
}

int toy_game_squad_release(struct toy_game *g, struct toy_game_squad_handle handle)
{
    struct toy_game_squad *s = actor_squad_handle(g, handle);
    int i;
    if (!s) return 0;
    for (i = 0; i < s->count; ++i) if (s->members[i].active) {
        int index = s->members[i].identity.index;
        if (index >= 0 && index < TOY_GAME_MAX_ACTORS &&
            g->squad_lookup[index] == handle.index * TOY_GAME_SQUAD_MEMBERS + i + 1)
            g->squad_lookup[index] = 0;
    }
    memset(s, 0, sizeof(*s)); return 1;
}

int toy_game_squad_append(struct toy_game *g,struct toy_game_squad_handle handle,
    struct toy_game_actor_identity identity)
{
    struct toy_game_squad *s=actor_squad_handle(g,handle);
    struct toy_game_actor *a;
    int ordinal;
    if(!s || s->count>=TOY_GAME_SQUAD_MEMBERS || !actor_squad_identity(g,identity))return 0;
    a=&g->actors[identity.index];
    if(g->squad_lookup[identity.index] || a->kind!=TOY_GAME_ACTOR_AI ||
       a->faction!=s->faction || a->base_core || a->developer_only ||
       a->animation_demo || a->hit_test_dummy || a->ai_stationary)return 0;
    ordinal=s->count++;
    s->members[ordinal].identity=identity;s->members[ordinal].active=1;
    s->members[ordinal].slot=ordinal;
    g->squad_lookup[identity.index]=(unsigned char)(handle.index*TOY_GAME_SQUAD_MEMBERS+ordinal+1);
    return 1;
}

int toy_game_squad_assault(struct toy_game *g, struct toy_game_squad_handle handle,
    int x, int z)
{
    struct toy_game_squad *s = actor_squad_handle(g, handle);
    if (!s) return 0;
    s->mode = TOY_GAME_SQUAD_ASSAULT; s->x = x; s->z = z; s->refresh_ms = 0;
    return 1;
}

int toy_game_squad_follow(struct toy_game *g, struct toy_game_squad_handle handle,
    struct toy_game_actor_identity anchor)
{
    struct toy_game_squad *s = actor_squad_handle(g, handle);
    if (!s || !actor_squad_identity(g, anchor) ||
        g->actors[anchor.index].faction != s->faction) return 0;
    s->mode = TOY_GAME_SQUAD_FOLLOW; s->anchor = anchor; s->refresh_ms = 0;
    s->anchor_x=g->actors[anchor.index].x;s->anchor_z=g->actors[anchor.index].z;
    {
        int sy=g->actors[anchor.index].sy,cy=g->actors[anchor.index].cy;
        int length=isqrt((long long)sy*sy+(long long)cy*cy);
        if(length){s->sy=sy*1024/length;s->cy=cy*1024/length;}
    }
    return 1;
}

static int actor_squad_idle_goal(struct toy_game *g, int actor_index, int *x, int *z)
{
    int lookup, gi, ordinal;
    struct toy_game_squad *s;
    struct toy_game_squad_member *m;
    if (!g || !x || !z || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS) return 0;
    lookup = g->squad_lookup[actor_index];
    if (!lookup) return 0;
    gi = (lookup - 1) / TOY_GAME_SQUAD_MEMBERS;
    ordinal = (lookup - 1) % TOY_GAME_SQUAD_MEMBERS;
    s = &g->squads[gi]; m = &s->members[ordinal];
    g->squad_profile.resolve_checks++;
    if (!s->active || s->mode == TOY_GAME_SQUAD_IDLE ||
        s->map_generation != g->navigation_generation || !m->active || !m->goal_valid ||
        !actor_squad_identity(g, m->identity) ||
        g->actors[actor_index].faction != s->faction ||
        actor_squad_abs(g->actors[actor_index].ground_y-m->y)>TOY_CONFIG_GROUND_STEP_HEIGHT ||
        !actor_squad_eligible(&g->actors[actor_index])) return 0;
    if (s->mode == TOY_GAME_SQUAD_FOLLOW &&
        (g->actors[s->anchor.index].faction != s->faction ||
         g->actors[s->anchor.index].state != TOY_GAME_ACTOR_ALIVE ||
         g->actors[s->anchor.index].hp <= 0)) return 0;
    *x = m->x; *z = m->z; return 1;
}

int toy_game_squad_actor_goal(struct toy_game *g, int actor_index, int *x, int *z)
{
    struct toy_game_squad *s;
    int lookup;
    if (!g || !x || !z || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS)
        return 0;
    lookup = g->squad_lookup[actor_index];
    if (!lookup || lookup > TOY_GAME_SQUAD_GROUPS * TOY_GAME_SQUAD_MEMBERS) return 0;
    s = &g->squads[(lookup - 1) / TOY_GAME_SQUAD_MEMBERS];
    /* Public callers may query between ticks, after an anchor slot is reused. */
    if (s->mode == TOY_GAME_SQUAD_FOLLOW && !actor_squad_identity(g, s->anchor))
        return 0;
    return actor_squad_idle_goal(g, actor_index, x, z);
}

static void actor_squad_offset(const struct toy_game_squad *s, int slot,
    int x, int z, int *out_x, int *out_z)
{
    static const int side[5] = {0,-720,720,-1440,1440};
    static const int back[5] = {0,-720,-720,-1440,-1440};
    *out_x = x + (int)(((long long)side[slot] * s->cy +
                       (long long)back[slot] * s->sy) / 1024);
    *out_z = z + (int)(((long long)back[slot] * s->cy -
                       (long long)side[slot] * s->sy) / 1024);
}

static int actor_squad_probe(struct toy_game *g, int x0, int z0, int y,
    int x, int z, int *end_y)
{
    struct toy_game_ground_query ground;
    if (g->squad_profile.candidate_probes >= ACTOR_SQUAD_PROBES) return 0;
    g->squad_profile.candidate_probes++;
    ground = toy_game_query_ground(g, x, z, TOY_GAME_PLAYER_RADIUS, y);
    if ((g->primitives && !ground.has_support) ||
        actor_squad_abs(ground.support_y-y) > TOY_CONFIG_GROUND_STEP_HEIGHT ||
        toy_game_position_blocked_at_height(g,x,z,TOY_GAME_PLAYER_RADIUS,ground.support_y) ||
        !toy_game_short_connection(g,x0,z0,x,z,TOY_GAME_PLAYER_RADIUS,y)) return 0;
    *end_y = ground.support_y; return 1;
}

static void actor_squad_trail(struct toy_game_squad *s, const struct toy_game_actor *a)
{
    int next = 0;
    if (s->trail_count) {
        const struct toy_game_squad_point *p=&s->trail[s->trail_head];
        long long dx=(long long)a->x-p->x, dz=(long long)a->z-p->z;
        if (dx*dx+dz*dz < ACTOR_SQUAD_SPACING*ACTOR_SQUAD_SPACING) return;
        next=(s->trail_head+1)%TOY_GAME_SQUAD_TRAIL;
    }
    s->trail_head=next;
    s->trail[next].x=a->x; s->trail[next].z=a->z; s->trail[next].y=a->ground_y;
    if(s->trail_count<TOY_GAME_SQUAD_TRAIL)s->trail_count++;
}

/* Actual leader breadcrumbs only. Probe from the neighboring recorded point,
 * never a long leader-to-tail chord or an invented point through a doorway. */
static int actor_squad_column_point(const struct toy_game_squad *s,
    const struct toy_game_actor *leader, int slot,
    int *x0,int *z0,int *y,int *x,int *z)
{
    int remaining=slot*ACTOR_SQUAD_SPACING, px=leader->x,pz=leader->z;
    int py=leader->ground_y,i;
    for(i=0;i<s->trail_count;i++) {
        const struct toy_game_squad_point *p=&s->trail[
            (s->trail_head-i+TOY_GAME_SQUAD_TRAIL)%TOY_GAME_SQUAD_TRAIL];
        long long dx=(long long)p->x-px,dz=(long long)p->z-pz;
        int length=isqrt(dx*dx+dz*dz);
        if(length && remaining<=length) {
            *x0=px;*z0=pz;*y=py;
            *x=px+(int)(dx*remaining/length);*z=pz+(int)(dz*remaining/length);
            return 1;
        }
        remaining-=length;px=p->x;pz=p->z;py=p->y;
    }
    return 0;
}

static void actor_squad_refresh(struct toy_game *g,struct toy_game_squad *s)
{
    struct toy_game_actor *leader=&g->actors[s->members[s->leader].identity.index];
    int i, fits=1, local_y, was_column=s->column;
    int goal_x[5],goal_z[5],goal_y[5],valid[5]={0};
    if(s->mode==TOY_GAME_SQUAD_FOLLOW) {
        const struct toy_game_actor *anchor=&g->actors[s->anchor.index];
        long long dx=(long long)anchor->x-s->anchor_x,dz=(long long)anchor->z-s->anchor_z;
        int length=isqrt(dx*dx+dz*dz);
        if(length>=256) {
            s->sy=(int)(dx*1024/length);s->cy=(int)(dz*1024/length);
            s->anchor_x=anchor->x;s->anchor_z=anchor->z;
        }
        s->x=anchor->x-s->sy*1600/1024;s->z=anchor->z-s->cy*1600/1024;
    } else {
        long long dx=(long long)s->x-leader->x,dz=(long long)s->z-leader->z;
        int length=isqrt(dx*dx+dz*dz);
        if(length>ACTOR_SQUAD_SPACING) {
            s->sy=(int)(dx*1024/length);s->cy=(int)(dz*1024/length);
        }
    }
    /* In column, alternate actual breadcrumb goal updates with complete
     * local+destination wedge tests. Both fit samples must succeed twice;
     * proving only the leader's local fit can oscillate against an end wall. */
    /* Nonzero column phases: 1 updates breadcrumbs, 2 checks wedge fit. */
    if(s->column==1) {
        s->column=2;
        for(i=0;i<s->count;i++)if(s->members[i].eligible) {
            struct toy_game_squad_member *m=&s->members[i];
            int x=s->x,z=s->z,x0=x,z0=z,y=leader->ground_y,end_y;
            if(i!=s->leader &&
                !actor_squad_column_point(s,leader,m->slot,&x0,&z0,&y,&x,&z))continue;
            if(actor_squad_probe(g,x0,z0,y,x,z,&end_y)) {
                if(!m->goal_valid || m->x!=x || m->z!=z || m->y!=end_y) {
                    m->x=x;m->z=z;m->y=end_y;m->goal_valid=1;
                    g->squad_profile.goal_changes++;
                }
            } else if(!m->goal_valid)g->squad_profile.no_valid_goal++;
        }
        return;
    }
    if(s->column)s->column=1;
    /* At most one local and one destination candidate per eligible member. */
    for(i=0;i<s->count;i++)if(s->members[i].eligible) {
        int x,z;
        actor_squad_offset(s,s->members[i].slot,leader->x,leader->z,&x,&z);
        if(!actor_squad_probe(g,leader->x,leader->z,leader->ground_y,x,z,&local_y))fits=0;
    }
    for(i=0;i<s->count;i++)if(s->members[i].eligible) {
        actor_squad_offset(s,s->members[i].slot,s->x,s->z,&goal_x[i],&goal_z[i]);
        valid[i]=actor_squad_probe(g,s->x,s->z,leader->ground_y,
            goal_x[i],goal_z[i],&goal_y[i]);
        if(!valid[i])fits=0;
    }
    if(!fits) {
        s->fit_streak=0;
        if(!s->column){s->column=1;g->squad_profile.column_entries++;}
    } else if(s->column && ++s->fit_streak>=2) {
        s->column=0;s->fit_streak=0;g->squad_profile.column_exits++;
    }
    /* Existing column goals survive a failed or first fit sample. */
    if(was_column && s->column)return;
    for(i=0;i<s->count;i++)if(s->members[i].eligible && valid[i]) {
        struct toy_game_squad_member *m=&s->members[i];
        if(!m->goal_valid || m->x!=goal_x[i] || m->z!=goal_z[i] || m->y!=goal_y[i]) {
            m->x=goal_x[i];m->z=goal_z[i];m->y=goal_y[i];m->goal_valid=1;
            g->squad_profile.goal_changes++;
        }
    }
}

static void actor_squad_tick(struct toy_game *g, int dt_ms)
{
    int gi,i,inspected,eligible_count;
    memset(&g->squad_profile, 0, sizeof(g->squad_profile));
    if(g->state!=TOY_GAME_PLAYING || dt_ms<=0)return;
    for(gi=0;gi<TOY_GAME_SQUAD_GROUPS;gi++) {
        struct toy_game_squad *s=&g->squads[gi];
        int first=-1;
        if(!s->active)continue;
        if(s->map_generation!=g->navigation_generation) {
            struct toy_game_squad_handle h={gi,s->generation};
            toy_game_squad_release(g,h);continue;
        }
        s->refresh_ms-=dt_ms;
        for(i=0;i<s->count;i++) {
            struct toy_game_squad_member *m=&s->members[i];
            const struct toy_game_actor *a;
            m->eligible=0;
            if(!m->active)continue;
            g->squad_profile.membership_checks++;
            if(!actor_squad_identity(g,m->identity) ||
                g->actors[m->identity.index].faction!=s->faction ||
                g->actors[m->identity.index].state==TOY_GAME_ACTOR_DEAD) {
                int index=m->identity.index;
                if(index>=0 && index<TOY_GAME_MAX_ACTORS &&
                    g->squad_lookup[index]==gi*TOY_GAME_SQUAD_MEMBERS+i+1)
                    g->squad_lookup[index]=0;
                m->active=m->goal_valid=0;continue;
            }
            a=&g->actors[m->identity.index];m->eligible=actor_squad_eligible(a);
            if(m->eligible && first<0)first=i;
        }
        if(s->mode==TOY_GAME_SQUAD_FOLLOW) {
            g->squad_profile.anchor_checks++;
            if(!actor_squad_identity(g,s->anchor) ||
                g->actors[s->anchor.index].faction!=s->faction ||
                g->actors[s->anchor.index].state!=TOY_GAME_ACTOR_ALIVE ||
                g->actors[s->anchor.index].hp<=0)first=-1;
        }
        if(s->leader<0 || !s->members[s->leader].eligible || first<0) {
            if(s->leader!=first) {
                if(first>=0) {
                    int old_center=-1,new_slot=s->members[first].slot;
                    for(i=0;i<s->count;i++)if(s->members[i].slot==0)old_center=i;
                    if(old_center>=0) {
                        s->members[old_center].slot=new_slot;
                        s->members[old_center].goal_valid=0;
                    }
                    s->members[first].slot=0;s->members[first].goal_valid=0;
                }
                s->leader=first;s->trail_count=0;s->fit_streak=0;
                if(s->column)s->column=1;
                g->squad_profile.leader_changes++;
            }
        }
        if(s->leader>=0)actor_squad_trail(s,
            &g->actors[s->members[s->leader].identity.index]);
        if(first<0)for(i=0;i<s->count;i++)s->members[i].goal_valid=0;
    }
    for(inspected=0;inspected<TOY_GAME_SQUAD_GROUPS &&
        g->squad_profile.refreshes<2;inspected++) {
        struct toy_game_squad *s=&g->squads[g->squad_cursor];
        g->squad_cursor=(g->squad_cursor+1)%TOY_GAME_SQUAD_GROUPS;
        if(!s->active || s->mode==TOY_GAME_SQUAD_IDLE ||
            s->leader<0 || s->refresh_ms>0)continue;
        eligible_count=0;
        for(i=0;i<s->count;i++)eligible_count+=s->members[i].eligible;
        if(eligible_count*2>ACTOR_SQUAD_PROBES-g->squad_profile.candidate_probes)continue;
        g->squad_profile.refreshes++;
        s->refresh_ms=ACTOR_SQUAD_CADENCE;
        actor_squad_refresh(g,s);
    }
}
