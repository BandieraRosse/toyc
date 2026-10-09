/* Presentation orchestration. Commands and story state have independent owners. */
static void rf_player_input_filter(struct rf_game_runtime *r,struct rf_input_frame *in)
{
    for(int i=0;i<RF_INPUT_PHYSICAL_KEY_COUNT;++i) {
        if(!in->physical_down[i])r->ui_suppressed_physical[i]=0;
        if(r->ui_suppressed_physical[i])in->physical_down[i]=in->physical_pressed[i]=0;
    }
    for(int i=0;i<RF_INPUT_KEY_COUNT;++i) {
        if(!in->key_down[i])r->ui_suppressed_keys[i]=0;
        if(r->ui_suppressed_keys[i])in->key_down[i]=in->key_pressed[i]=0;
    }
    r->ui_suppressed_mouse&=in->mouse_buttons;
    in->mouse_buttons&=~r->ui_suppressed_mouse;
}
static void rf_player_block_input(struct rf_game_runtime *r,struct rf_input_frame *in,
    unsigned char *keys,unsigned char *physical,struct toy_window_events *events)
{
    for(int i=0;i<RF_INPUT_PHYSICAL_KEY_COUNT;++i)r->ui_suppressed_physical[i]|=in->physical_down[i];
    for(int i=0;i<RF_INPUT_KEY_COUNT;++i)r->ui_suppressed_keys[i]|=in->key_down[i];
    r->ui_suppressed_mouse|=in->mouse_buttons;
    memset(in->key_down,0,sizeof(in->key_down));memset(in->key_pressed,0,sizeof(in->key_pressed));
    memset(in->physical_down,0,sizeof(in->physical_down));memset(in->physical_pressed,0,sizeof(in->physical_pressed));
    memset(keys,0,RF_INPUT_KEY_COUNT);memset(physical,0,RF_INPUT_PHYSICAL_KEY_COUNT);
    in->mouse_buttons=0;in->relative_x=in->relative_y=0;in->wheel_y=0;
    if(events){events->button_pressed=0;events->relative_x=events->relative_y=0;}
}
static struct rf_player_result rf_player_request(struct rf_game_runtime *r,int op,int value,int x,int z)
{
    struct rf_player_request a={0};a.operation=op;a.value=value;a.x=x;a.z=z;
    a.world_generation=r->session->scene_local.world_generation;
    a.serial=r->session->game_state.weaver.job_serial;
    a.session_revision=r->story.session_revision;a.node_revision=r->story.node_revision;
    return rf_player_execute(r,&a,r->player_ui.mode==RF_PLAYER_UI_EXPERIMENT?
        RF_COMMAND_PERMISSION_ADMIN:RF_COMMAND_PERMISSION_USER);
}
static int rf_player_object_facing(const struct rf_game_runtime *r,const char *id)
{
    const struct rf_map_runtime_object *object=rf_map_runtime_find_object(&r->session->map_ops.runtime,id);
    const struct toy_game_actor *player=toy_game_local_player_actor_const(&r->session->game_state);
    if(!object||!player)return 0;
    int64_t dx=(int64_t)object->x-player->x,dz=(int64_t)object->z-player->z;
    int distance=isqrt(dx*dx+dz*dz);
    if(abs(object->y-player->ground_y)>RASTERFALL_RFU_PER_METER)return 0;
    return distance<96 || dx*r->camera.sy+dz*r->camera.cy>=(int64_t)distance*512;
}
static void rf_player_hud_fill(struct rf_game_runtime *r,struct rasterfall_hud_state *h,int width,int height)
{
    struct rf_player_ui_view *v=&h->player_ui_view;
    const struct toy_game_actor *grid_player=toy_game_local_player_actor_const(&r->session->game_state);
    r->player_ui.grid_focus_y=r->rts_active && r->rts.focus_floor>=0?
        r->rts.floors[r->rts.focus_floor].floor_y:(grid_player?grid_player->ground_y:0);
    rf_player_ui_prepare(&r->player_ui,&r->session->level,&r->session->game_state,
        (unsigned)r->session->scene_local.world_generation);
    h->player_ui=&r->player_ui;v->bindings=&game_bindings;
    v->hints=r->player_controls.hints;
    v->phase_visible=r->session->world_id!=RASTERFALL_WORLD_OUTPOST||rf_combat_lab.running;
    v->modal=rf_tac_ui.open || r->console.open||rf_weaver_terminal.open||rf_render_terminal.open||rf_table.open;
    v->region_name=r->session->world_id==RASTERFALL_WORLD_OUTPOST?"前哨站":"行动区域";
    v->rts_active=r->rts_active;v->rts_selected=r->rts_selected;v->rts_follow_player=r->rts_follow_player;
    v->rts=&r->rts;v->rts_camera=&r->render_camera;
    if(r->rts_active)rf_rts_portraits_prepare(&r->rts,&r->session->game_state);
    v->rts_move_active=r->session->rts_move_active;v->rts_move_x=r->session->rts_move_x;v->rts_move_z=r->session->rts_move_z;v->rts_move_y=r->session->rts_move_y;
    v->camera_x=r->render_camera.x;v->camera_z=r->render_camera.z;v->camera_sy=r->camera.sy;v->camera_cy=r->camera.cy;
    v->map_sight_count=0;
    if(r->rts_active && width>1 && height>1) {
        const struct camera *c=&r->render_camera;
        for(int i=0;i<4;++i) {
            int sx=(i==1 || i==2)?width:0,sy=i>=2?height:0,y;
            int *x=&v->map_sight_x[i],*z=&v->map_sight_z[i];
            if(!rts_surface_pick(&r->session->level,c,width,height,sx,sy,
                r->session->air_walls_enabled,x,&y,z)) {
                double vx=(double)(sx-width/2)/(width*3/4);
                double vy=(double)(height/2-sy)/(width*3/4);
                double dy=(vy*c->pitch_cy+c->pitch_sy)/1024.0;
                double wz=(-vy*c->pitch_sy+c->pitch_cy)/1024.0;
                double distance=(RASTERFALL_WORLD_GROUND_Y-c->y)/dy;
                *x=c->x+(int)(distance*(vx*c->cy+wz*c->sy)/1024.0);
                *z=c->z+(int)(distance*(-vx*c->sy+wz*c->cy)/1024.0);
            }
        }
        v->map_sight_count=4;
    } else {
        int reach=RASTERFALL_RFU_PER_METER*30;
        v->map_sight_count=3;
        v->map_sight_x[0]=r->camera.x;v->map_sight_z[0]=r->camera.z;
        for(int i=1;i<3;++i) {
            int side=i==1?-1:1;
            v->map_sight_x[i]=r->camera.x+(int)((long long)reach*(r->camera.sy+side*r->camera.cy*2/3)/1024);
            v->map_sight_z[i]=r->camera.z+(int)((long long)reach*(r->camera.cy-side*r->camera.sy*2/3)/1024);
        }
    }
    v->objective_title=r->story.task.title;v->objective_detail=r->story.task.hint;
    v->objective_active=r->story.task.target_valid;v->objective_x=r->story.task.x;
    v->objective_y=r->story.task.y;v->objective_z=r->story.task.z;
    if(r->session->world_id==RASTERFALL_WORLD_FRONTIER_STATION_01) {
        const struct rf_frontier_mission *m=&r->session->frontier;
        v->region_name="边缘站点 01";v->objective_active=0;
        if(m->config.core_objective && m->phase!=RF_FRONTIER_SECURED && m->phase!=RF_FRONTIER_FAILED) {
            const struct rf_frontier_entity *enemy=&m->cores[1],*base=&m->cores[0];
            const struct toy_game *g=&r->session->game_state;
            snprintf(v->mission_title,sizeof(v->mission_title),"摧毁敌方基地核心：%d / 500",
                enemy->active?g->actors[enemy->index].hp:0);
            snprintf(v->mission_detail,sizeof(v->mission_detail),"敌方基地中央红色核心 · 我方基地 %d / 500",
                base->active?g->actors[base->index].hp:0);
            v->objective_active=1;v->objective_x=m->config.enemy_core.x;
            v->objective_z=m->config.enemy_core.z;v->objective_y=m->config.enemy_core.y;
        } else if(m->phase==RF_FRONTIER_ASSAULT)
            snprintf(v->mission_title,sizeof(v->mission_title),"攻占：初始守军 %d",m->guards_alive);
        else if(m->phase==RF_FRONTIER_PREPARE)
            snprintf(v->mission_title,sizeof(v->mission_title),"整备：%d 秒后增援",(45000-m->phase_ms+999)/1000);
        else if(m->phase==RF_FRONTIER_COUNTERATTACK)
            snprintf(v->mission_title,sizeof(v->mission_title),"反击：敌军 %d / 待入 %d",m->enemies_alive,m->pending_spawns);
        else snprintf(v->mission_title,sizeof(v->mission_title),"%s",m->phase==RF_FRONTIER_SECURED?
            "行动完成：敌方核心已摧毁":m->phase==RF_FRONTIER_FAILED?"行动失败：可重试或返回":"准备行动");
        if(!m->config.core_objective || m->phase==RF_FRONTIER_SECURED || m->phase==RF_FRONTIER_FAILED)
            snprintf(v->mission_detail,sizeof(v->mission_detail),"车间%s 仓库%s 能源%s",
            m->captured[0]?"●":"○",m->captured[1]?"●":"○",m->captured[2]?"●":"○");
        v->objective_title=v->mission_title;v->objective_detail=v->mission_detail;
        for(int i=0;i<3;++i) {
            const struct rf_frontier_facility_config *f=&m->config.facilities[i];
            struct rf_minimap_marker mark={0};mark.id=1100+i;mark.kind=RF_MINIMAP_DEVICE;
            mark.x=f->point.x;mark.z=f->point.z;
            snprintf(mark.label,sizeof(mark.label),"%s%s",i==0?"车间":i==1?"仓库":"能源",m->captured[i]?"●":"○");
            rf_minimap_marker_set(&r->player_ui.minimap,&mark);
        }
    }
    v->command_feedback=r->player_controls.last.message;
    v->comms_visible=r->story.active_story&&!r->story.collapsed;
    if(rf_weaver_terminal.near){v->interaction_title="网格编织机";v->interaction_action="打开设备";}
    else if(rf_render_terminal.near){v->interaction_title="渲染控制终端";v->interaction_action="打开设备";}
    else if(rf_table.near){v->interaction_title="前哨站指挥桌";v->interaction_action="部署行动";}
    struct rf_player_facility_query facility;
    rf_player_frontier_query(r,&facility);
    if(facility.available && (!facility.captured || facility.index!=0)) {
        v->interaction_title=facility.name;v->interaction_action=facility.action;
        long long dx=(long long)facility.x-r->camera.x,dz=(long long)facility.z-r->camera.z;
        v->interaction_distance_rfu=(int)isqrt(dx*dx+dz*dz);
    }
    {
        const char *id=rf_weaver_terminal.near?"mesh_weaver":rf_render_terminal.near?"main_terminal":rf_table.near?"command_table":NULL;
        const struct rf_map_runtime_object *object=id?rf_map_runtime_find_object(&r->session->map_ops.runtime,id):NULL;
        int x=0,z=0,valid=0;
        if(object){x=object->x;z=object->z;valid=1;}
        else if(r->session->highlight_index>=0 && r->session->highlight_index<r->session->item_count) {
            x=r->session->items[r->session->highlight_index].x;z=r->session->items[r->session->highlight_index].z;valid=1;
        }
        if(valid) {
            double dx=(double)x-r->camera.x,dz=(double)z-r->camera.z;
            v->interaction_distance_rfu=(int)sqrt(dx*dx+dz*dz);
        }
    }
    const struct rf_map_runtime_object *m=rf_map_runtime_find_object(&r->session->map_ops.runtime,"mesh_weaver");
    if(m) {
        struct rf_minimap_marker mark={0};mark.id=1001;mark.kind=RF_MINIMAP_DEVICE;
        mark.x=m->x;mark.y=m->y;mark.z=m->z;snprintf(mark.label,sizeof(mark.label),"网格编织机");
        rf_minimap_marker_set(&r->player_ui.minimap,&mark);
    }else rf_minimap_marker_remove(&r->player_ui.minimap,1001);
}

static void rf_player_weaver_action(struct rf_game_runtime *r,int hit,
    struct rasterfall_console *console)
{
    const struct toy_mesh_weaver *w=&r->session->game_state.weaver;
    if(hit==RF_WEAVER_HIT_START && w->phase==TOY_WEAVER_READY)hit=RF_WEAVER_HIT_COLLECT;
    if(hit>=RF_WEAVER_HIT_BLUEPRINT_BASE) {
        rf_weaver_submit(RF_PLAYER_WEAVER_SELECT,hit-RF_WEAVER_HIT_BLUEPRINT_BASE,0);
        rf_weaver_terminal.confirm_replace=0;return;
    }
    switch(hit) {
    case RF_WEAVER_HIT_START:rf_weaver_submit(RF_PLAYER_WEAVER_START,0,0);break;
    case RF_WEAVER_HIT_COLLECT: {
        struct rf_player_weaver_query q;rf_player_weaver_query(r,&q);
        rf_weaver_submit(RF_PLAYER_WEAVER_COLLECT,
            rf_weaver_terminal.confirm_replace?rf_weaver_terminal.confirm_replace-2:q.replace_weapon,
            rf_weaver_terminal.confirm_replace!=0);
        if(r->player_controls.last.code==RF_PLAYER_CONFIRM_REPLACE)
            rf_weaver_terminal.confirm_replace=q.replace_weapon+2;
        else rf_weaver_terminal.confirm_replace=0;
        break;
    }
    case RF_WEAVER_HIT_POWER:rf_weaver_submit(RF_PLAYER_WEAVER_POWER,!w->supply.power_on,0);break;
    case RF_WEAVER_HIT_CPU:rf_weaver_submit(RF_PLAYER_WEAVER_CPU,!w->supply.cpu_on,0);break;
    case RF_WEAVER_HIT_X1:rf_weaver_submit(RF_PLAYER_WEAVER_X1,!w->supply.x1_on,0);break;
    case RF_WEAVER_HIT_DETAILS:r->player_controls.details=!r->player_controls.details;break;
    case RF_WEAVER_HIT_MINIMIZE:rf_weaver_terminal.open=0;rf_weaver_terminal.minimized=1;break;
    case RF_WEAVER_HIT_CLOSE:rf_weaver_terminal.open=0;rf_weaver_terminal.minimized=0;break;
    case RF_WEAVER_HIT_TERMINAL:console->open=1;console->was_paused=0;
        if(!console->terminal.input[0])rf_terminal_session_set_input(&console->terminal,"weaver status");
        break;
    case RF_WEAVER_HIT_ROTATE_LEFT:r->player_controls.preview_yaw=(r->player_controls.preview_yaw+345)%360;break;
    case RF_WEAVER_HIT_ROTATE_RIGHT:r->player_controls.preview_yaw=(r->player_controls.preview_yaw+15)%360;break;
    default:break;
    }
}
static void rf_player_weaver_input(struct rf_game_runtime *r,struct rasterfall_console *console,
    const struct rf_input_frame *in,const struct toy_window_events *events,int width,int height)
{
    struct rf_player_weaver_rects rects;
    int focus=rf_weaver_terminal.row,order[32],count=0,at=0,hit=0;
    rf_player_weaver_layout(&rects,width,height,&r->player_ui);
    for(unsigned i=0;i<RF_WEAVER_BLUEPRINT_COUNT;++i)order[count++]=RF_WEAVER_HIT_BLUEPRINT_BASE+(int)i;
    for(int i=1;i<RF_WEAVER_HIT_COUNT;++i)if(i!=RF_WEAVER_HIT_COLLECT)order[count++]=i;
    for(int i=0;i<count;++i)if(order[i]==focus)at=i;
    if(action_pressed(in,RF_ACTION_CANCEL)){rf_weaver_terminal.open=0;rf_weaver_terminal.minimized=0;return;}
    if(action_pressed(in,RF_ACTION_UI_DOWN)||action_pressed(in,RF_ACTION_TEXT_NEXT_FIELD))at=(at+1)%count;
    if(action_pressed(in,RF_ACTION_UI_UP))at=(at+count-1)%count;
    rf_weaver_terminal.row=order[at];
    if(action_pressed(in,RF_ACTION_UI_LEFT))hit=RF_WEAVER_HIT_ROTATE_LEFT;
    if(action_pressed(in,RF_ACTION_UI_RIGHT))hit=RF_WEAVER_HIT_ROTATE_RIGHT;
    if(action_pressed(in,RF_ACTION_CONFIRM))hit=rf_weaver_terminal.row;
    if(events->button_pressed && events->button==BTN_LEFT) {
        hit=rf_player_weaver_hit(&rects,in->pointer_x,in->pointer_y);
        if(hit)rf_weaver_terminal.row=hit;
    }
    if(hit)rf_player_weaver_action(r,hit,console);
}

static int rf_player_device_focus[2]={RF_DEVICE_HIT_FEATURE_BASE,RF_DEVICE_HIT_MAP_BASE};
static void rf_player_device_input(struct rf_game_runtime *r,struct rasterfall_console *console,
    const struct rf_input_frame *in,const struct toy_window_events *events,int width,int height,int kind)
{
    struct rf_device_query q;struct rf_player_device_rects rects;
    int order[RF_PLAYER_DEVICE_FEATURE_ROWS+3],count=0,at=0,hit=0;
    rf_device_query(r,&q);rf_player_device_layout(&rects,width,height,&r->player_ui,kind,
        kind==RF_PLAYER_DEVICE_TABLE?q.map_count:q.feature_count);
    static int hovered[2];
    int pointer_hit=rf_player_device_hit(&rects,in->pointer_x,in->pointer_y);
    if(pointer_hit!=hovered[kind]) {
        if(pointer_hit)rasterfall_audio_ui(&r->audio,RF_UI_SOUND_HOVER);
        hovered[kind]=pointer_hit;
    }
    if(kind==RF_PLAYER_DEVICE_RENDER)for(int i=0;i<q.feature_count&&i<RF_PLAYER_DEVICE_FEATURE_ROWS;++i)order[count++]=RF_DEVICE_HIT_FEATURE_BASE+i;
    else {
        for(int i=0;i<q.map_count&&i<RF_PLAYER_DEVICE_MAP_ROWS;++i)order[count++]=RF_DEVICE_HIT_MAP_BASE+i;
        order[count++]=RF_DEVICE_HIT_DEPLOY;
    }
    order[count++]=RF_DEVICE_HIT_TERMINAL;order[count++]=RF_DEVICE_HIT_CLOSE;
    for(int i=0;i<count;++i)if(order[i]==rf_player_device_focus[kind])at=i;
    if(action_pressed(in,RF_ACTION_TEXT_NEXT_FIELD)||action_pressed(in,RF_ACTION_UI_DOWN))at=(at+1)%count;
    if(action_pressed(in,RF_ACTION_UI_UP))at=(at+count-1)%count;
    rf_player_device_focus[kind]=order[at];
    if(action_pressed(in,RF_ACTION_CONFIRM))hit=order[at];
    if(events->button_pressed && events->button==BTN_LEFT) {
        hit=rf_player_device_hit(&rects,in->pointer_x,in->pointer_y);
        if(hit)rf_player_device_focus[kind]=hit;
    }
    if(action_pressed(in,RF_ACTION_CANCEL))hit=RF_DEVICE_HIT_CLOSE;
    if(hit)rasterfall_audio_ui(&r->audio,hit==RF_DEVICE_HIT_CLOSE?RF_UI_SOUND_EXIT:
        hit==RF_DEVICE_HIT_DEPLOY?RF_UI_SOUND_CONFIRM:
        kind==RF_PLAYER_DEVICE_RENDER?RF_UI_SOUND_CHANGE:RF_UI_SOUND_CLICK);
    if(hit==RF_DEVICE_HIT_CLOSE) {
        if(kind==RF_PLAYER_DEVICE_RENDER)rf_render_terminal.open=0;else rf_table.open=0;
    } else if(hit==RF_DEVICE_HIT_TERMINAL) {
        console->open=1;console->was_paused=0;
        if(!console->terminal.input[0])rf_terminal_session_set_input(&console->terminal,
            kind==RF_PLAYER_DEVICE_RENDER?"render status":"table status");
    } else if(kind==RF_PLAYER_DEVICE_RENDER && hit>=RF_DEVICE_HIT_FEATURE_BASE && hit<RF_DEVICE_HIT_FEATURE_BASE+q.feature_count)
        rf_render_terminal_request(hit-RF_DEVICE_HIT_FEATURE_BASE);
    else if(kind==RF_PLAYER_DEVICE_TABLE && hit>=RF_DEVICE_HIT_MAP_BASE && hit<RF_DEVICE_HIT_MAP_BASE+q.map_count)
        rf_device_table_request(hit-RF_DEVICE_HIT_MAP_BASE,0);
    else if(kind==RF_PLAYER_DEVICE_TABLE && hit==RF_DEVICE_HIT_DEPLOY)
        rf_device_table_request(q.selected_map,1);
}

static int rf_player_comms_input(struct rf_game_runtime *r,struct rasterfall_console *console,struct rf_input_frame *in,
    const struct toy_window_events *events,int width,int height)
{
    int before=r->comms_focus;
    int toggle=action_pressed(in,RF_ACTION_COMMS_FOCUS);
    (void)events;
    if(toggle) {
        r->comms_focus=!r->comms_focus;
        if(r->comms_focus) {rf_story_collapse(&r->story,0);r->chat_scroll=0;}
    }
    if(r->comms_focus) {
        int scroll=in->wheel_y;
        if(action_pressed(in,RF_ACTION_UI_UP))scroll++;
        if(action_pressed(in,RF_ACTION_UI_DOWN))scroll--;
        r->chat_scroll+=scroll;
        if(r->chat_scroll<0)r->chat_scroll=0;
        int scroll_max=rf_player_chat_scroll_max(r,width,height);
        if(r->chat_scroll>scroll_max)r->chat_scroll=scroll_max;
        if(action_pressed(in,RF_ACTION_CANCEL))r->comms_focus=0;
    }
    if(before!=r->comms_focus)rf_core_set_pointer_lock(r->core,!r->comms_focus&&!r->rts_active&&!console->open);
    return before||r->comms_focus||console->open;
}
#ifdef TOYC_WINDOWS
static struct rf_gpu_scene_aux_view rf_player_aux,rf_player_unit_aux;
static void rf_frontier_audit(const struct rf_game_runtime *r,int frame)
{
    if(!getenv("RF_FRONTIER_AUDIT") || frame%10)return;
    const struct rasterfall_session *s=r->session;
    const struct toy_game *g=&s->game_state;
    const struct rf_frontier_mission *m=&s->frontier;
    const struct toy_game_actor *p=toy_game_local_player_actor_const(g);
    struct toy_surface *surface=rf_core_surface(r->core);
    int width=surface?surface->width:0,height=surface?surface->height:0;
    const struct toy_game_slot *slot=&p->slots[p->current_slot];
    __printf("FRONTIER-AUDIT {\"frame\":%d,\"world\":%d,\"world_generation\":%llu,\"mission_id\":%u,\"state\":%d,\"paused\":%d,\"phase\":%d,\"phase_ms\":%d,\"logic_ms\":%d,\"guards_alive\":%d,\"enemies_alive\":%d,\"periodic_spawned\":%d,\"periodic_alive\":%d,\"final_infected_spawned\":%d,\"final_gunners_spawned\":%d,\"pending_spawns\":%d,\"captured\":[%d,%d,%d],\"spawn_failures\":%d,\"spawn_attempts\":%d,\"victory_count\":%d,\"normal_sim\":%d,\"driver\":%d,\"rts\":%d,\"extent\":[%d,%d],\"player\":{\"id\":%d,\"generation\":%u,\"state\":%d,\"hp\":%d,\"x\":%d,\"z\":%d,\"ground_y\":%d,\"weapon\":%d,\"mag\":%d,\"reserve\":%d,\"class_id\":%d,\"max_hp\":%d,\"reloading\":%d,\"reload_ms\":%d},\"camera\":[%d,%d,%d,%d,%d,%d,%d],\"facilities\":[",
        frame,s->world_id,(unsigned long long)s->scene_local.world_generation,m->mission_id,g->state,r->lifecycle_paused,
        m->phase,m->phase_ms,g->combat_time_ms,m->guards_alive,m->enemies_alive,m->periodic_spawned,m->periodic_alive,
        m->final_infected_spawned,m->final_gunners_spawned,m->pending_spawns,m->captured[0],m->captured[1],m->captured[2],
        m->spawn_failures,m->spawn_attempts,m->victory_count,!rf_weaver_diagnostic.enabled,
        rf_weaver_diagnostic.enabled||rf_weaver_performance.mode,r->rts_active,width,height,
        p->actor_id,p->combat_generation,p->state,p->hp,p->x,p->z,p->ground_y,toy_game_actor_current_weapon(p),slot->mag,slot->reserve,
        p->class_id,p->max_hp,p->reloading,p->reload_timer_ms,
        r->render_camera.x,r->render_camera.y,r->render_camera.z,r->render_camera.sy,r->render_camera.cy,
        r->render_camera.pitch_sy,r->render_camera.pitch_cy);
    if(s->world_id==RASTERFALL_WORLD_FRONTIER_STATION_01)for(int n=0;n<3;++n) {
        const struct rf_frontier_facility_config *f=&m->config.facilities[n];
        int x=-1,y=-1;
        rf_rts_project(&r->render_camera,width,height,f->point.x,RASTERFALL_WORLD_GROUND_Y,f->point.z,&x,&y);
        __printf("%s{\"i\":%d,\"id\":\"%s\",\"identity\":%u,\"generation\":%u,\"captured\":%d,\"x\":%d,\"z\":%d,\"sx\":%d,\"sy\":%d,\"near\":%d}",
            n?",":"",n,f->point.id,f->identity,f->generation,m->captured[n],f->point.x,f->point.z,x,y,
            rasterfall_session_frontier_near(s,1)==n);
    }
    __printf("],\"landmarks\":[");int count=0;
    for(int n=0;n<rf_map_runtime_region_count(&s->map_ops.runtime);++n) {
        const struct rf_map_runtime_region *region=rf_map_runtime_region_at(&s->map_ops.runtime,n);
        if(strcmp(region->kind,"mission_route"))continue;
        int wx=region->bounds.min_x+(region->bounds.max_x-region->bounds.min_x)/2;
        int wz=region->bounds.min_z+(region->bounds.max_z-region->bounds.min_z)/2,x=-1,y=-1;
        rf_rts_project(&r->render_camera,width,height,wx,RASTERFALL_WORLD_GROUND_Y,wz,&x,&y);
        __printf("%s{\"id\":\"%s\",\"x\":%d,\"z\":%d,\"sx\":%d,\"sy\":%d}",count++?",":"",region->id,wx,wz,x,y);
    }
    __printf("],\"actors\":[");count=0;
    for(int n=0;n<TOY_GAME_MAX_ACTORS;++n) {
        const struct toy_game_actor *a=&g->actors[n];if(!a->active)continue;
        const struct toy_game_slot *aslot=&a->slots[a->current_slot];
        __printf("%s{\"i\":%d,\"id\":%d,\"generation\":%u,\"faction\":%d,\"class_id\":%d,\"state\":%d,\"hp\":%d,\"max_hp\":%d,\"x\":%d,\"z\":%d,\"moving\":%d,\"fire_seq\":%u,\"command\":%d,\"goal_x\":%d,\"goal_z\":%d,\"weapon\":%d,\"mag\":%d,\"reserve\":%d,\"reloading\":%d,\"reload_ms\":%d,\"target_kind\":%d,\"target_index\":%d,\"target_generation\":%u,\"aim_ms\":%d,\"lost_ms\":%d,\"scan_ms\":%d}",
            count++?",":"",n,a->actor_id,a->combat_generation,a->faction,a->class_id,a->state,a->hp,a->max_hp,a->x,a->z,a->moving,a->fire_seq,a->command_destination_active,a->command_x,a->command_z,
            toy_game_actor_current_weapon(a),aslot->mag,aslot->reserve,a->reloading,a->reload_timer_ms,
            a->combat_target.kind,a->combat_target.index,a->combat_target.generation,
            a->combat_aim_ms,a->combat_lost_ms,a->combat_scan_ms);
    }
    __printf("],\"infected\":[");count=0;
    for(int n=0;n<TOY_GAME_MAX_ENEMIES;++n) {
        const struct toy_game_enemy *e=&g->enemies[n];if(!e->active)continue;
        __printf("%s{\"i\":%d,\"generation\":%u,\"type\":%d,\"hp\":%d,\"x\":%d,\"z\":%d}",count++?",":"",n,e->combat_generation,e->type,e->hp,e->x,e->z);
    }
    __printf("]}\n");fflush(stdout);
}
static void rf_player_audit(const struct rf_game_runtime *r,int frame,const struct rf_gpu_scene_aux_status *video)
{
    rf_frontier_audit(r,frame);
    if(!getenv("RF_UI_AUDIT") || frame%10)return;
    __printf("AUDIO-AUDIT {\"paused\":%d,\"page\":%d,\"selected\":%d,\"range\":%d,\"save_error\":%d,\"volume\":[",
        r->lifecycle_paused,r->pause_menu_page,r->pause_menu_selected,r->audio.settings.range,r->audio.save_error);
    for(int i=0;i<RF_AUDIO_CONTROL_COUNT;++i)__printf("%s%d",i?",":"",r->audio.settings.volume[i]);
    __printf("]}\n");fflush(stdout);
    #if RF_TACTICAL_INTERACTIVE
    if(r->session->tactical) {
        const struct rf_tactical_lab *l=r->session->tactical;
        __printf("TACTICAL-UI-AUDIT {\"kind\":%d,\"open\":%d,\"dropdown\":%d,\"hover\":%d,\"tick\":%d,\"running\":%d,\"shots\":%d,\"player_shots\":%d,\"seed\":%u,\"seed_text\":\"%s\",\"seed_edit\":%d,\"hash\":\"%08x\"}\n",
            l->kind,rf_tac_ui.open,rf_tac_ui.dropdown,rf_tac_ui.hover,l->match.world.tick,
            l->kind==1?l->match.running:l->range.running,l->range.stats[0][l->range.lane].shots,
            l->range.stats[1][l->range.lane].shots,l->seed,rf_tac_ui.seed,rf_tac_ui.seed_edit,
            l->kind==1?rf_tac_hash(&l->match.world):0);
        fflush(stdout);
    }
    #endif
    if(r->rts_active) {
        const struct toy_game *g=&r->session->game_state;
        struct rf_ui_layout ui;int width=0,height=0;
        struct toy_surface *surface=rf_core_surface(r->core);
        if(surface){width=surface->width;height=surface->height;}
        rf_ui_layout_resolve(&ui,&r->player_ui,width,height,1);
        __printf("RTS-AUDIT {\"frame\":%d,\"selected\":%d,\"primary\":%d,\"hovered\":%d,\"drag\":%d,\"video\":%d,\"story_video\":%d,\"extent\":[%d,%d],\"dock\":[%d,%d,%d,%d],\"groups\":[",
            frame,rf_rts_count(&r->rts,g),r->rts.primary,r->rts.hovered,r->rts.drag_active,
            r->rts.video_live,r->ui_video_live,width,height,ui.selection.x,ui.selection.y,ui.selection.w,ui.selection.h);
        for(int n=0;n<RF_RTS_GROUPS;++n)__printf("%s%d",n?",":"",rf_rts_group_count(&r->rts,g,n));
        __printf("],\"floor\":%d,\"cutaway\":%.3f,\"distance\":%d,\"camera\":[%d,%d,%d],\"floor_buttons\":[",
            r->rts.focus_floor,r->rts.cutaway_amount,r->rts_camera_distance,
            r->render_camera.x,r->render_camera.y,r->render_camera.z);
        for(int n=0;n<r->rts.floor_count+2;++n) {
            struct rf_ui_rect rect=rf_player_ui_floor_rect(&ui,&r->rts,n);
            __printf("%s[%d,%d,%d,%d]",n?",":"",rect.x,rect.y,rect.w,rect.h);
        }
        const struct toy_game_actor_layer_search *search=&g->actor_layer_search;
        __printf("],\"command_buttons\":[");
        for(int n=0;n<4;++n) {
            struct rf_ui_rect rect=rf_player_ui_command_rect(&ui,n);
            __printf("%s[%d,%d,%d,%d]",n?",":"",rect.x,rect.y,rect.w,rect.h);
        }
        int grid_counts[7]={0};
        if(r->player_ui.grid_view && g->grid_enabled && g->grid_valid)
            for(int cell=0;cell<g->nav_width*g->nav_height;++cell)grid_counts[r->player_ui.grid_cells[cell]]++;
        __printf("],\"grid_counts\":[%d,%d,%d,%d,%d,%d,%d",grid_counts[0],grid_counts[1],grid_counts[2],
            grid_counts[3],grid_counts[4],grid_counts[5],grid_counts[6]);
        __printf("],\"grid_view\":%d,\"follow\":%d,\"command_hover\":%d,\"grid\":{\"enabled\":%d,\"valid\":%d,\"origin_x\":%d,\"origin_z\":%d,\"width\":%d,\"height\":%d,\"references\":%d,\"buildings\":%d},",
            r->player_ui.grid_view,r->rts_follow_player,rf_player_ui_hit_test(&r->player_ui,width,height,1,r->rts.pointer_x,r->rts.pointer_y),
            g->grid_enabled,g->grid_valid,g->nav_origin,g->nav_origin_z,
            g->nav_width,g->nav_height,g->grid_reference_count,g->grid_building_count);
        __printf("\"nav_cell_size\":%d,\"flow_nodes\":%d,\"nav_search\":{\"active\":%d,\"owner\":%d,\"stage\":%d,\"expanded\":%d,\"queued\":%d,\"slot\":%d,\"best\":%d,\"start\":%d,\"goal\":%d},\"actors\":[",
            g->nav_cell_size,g->flow_count,search->active,search->owner_slot,search->stage,
            search->expanded,search->queued,search->slot,search->best,search->start,search->goal);
        int count=0;
        for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)if(rf_rts_selectable(g,i)) {
            const struct toy_game_actor *a=&g->actors[i];int x=-1,y=-1;
            rf_rts_project(&r->render_camera,width,height,a->x,RASTERFALL_WORLD_GROUND_Y+a->ground_y+
                a->airborne_y+RASTERFALL_HUMAN_HEIGHT_RFU/2,a->z,&x,&y);
            __printf("%s{\"i\":%d,\"id\":%d,\"selected\":%d,\"weapon\":%d,\"x\":%d,\"z\":%d,\"sx\":%d,\"sy\":%d,\"moving\":%d,\"goal_x\":%d,\"goal_z\":%d,\"y\":%d,\"goal_y\":%d,\"airborne_y\":%d,\"airborne_ms\":%d,\"nav_x\":%d,\"nav_z\":%d,\"nav_active\":%d,\"nav_layer_count\":%d,\"nav_layer_cursor\":%d,\"velocity_x\":%d,\"velocity_z\":%d}",
                count++?",":"",i,a->actor_id,rf_rts_member_valid(&r->rts.selected[i],g,i),
                toy_game_actor_current_weapon(a),a->x,a->z,x,y,a->moving,
                a==toy_game_local_player_actor_const(g)?r->session->rts_move_x:a->command_x,
                a==toy_game_local_player_actor_const(g)?r->session->rts_move_z:a->command_z,
                a->ground_y,a==toy_game_local_player_actor_const(g)?r->session->rts_move_y:a->command_y,
                a->airborne_y,a->airborne_ms,a->nav_x,a->nav_z,a->nav_active,
                a->nav_layer_count,a->nav_layer_cursor,a->move_velocity_x,a->move_velocity_z);
        }
        __printf("],\"move_notice_ms\":%d,\"move_cell_failed\":%d}\n",
            r->session->banner_ms,r->session->banner_text &&
            !strncmp(r->session->banner_text,"MOVE FAILED: CELL",17));
    }
    __printf("PLAYER-UI-CHAT {\"frame\":%d,\"open\":%d,\"idle_ms\":%d,\"scroll\":%d,\"history\":%d,\"revision\":%u,\"visible\":%d}\n",
        frame,r->comms_focus,r->chat_idle_ms,r->chat_scroll,r->story.history_count,
        r->story.history_revision,r->comms_focus||(r->story.history_count&&r->chat_idle_ms<10000));
    const struct toy_game_actor *p=toy_game_local_player_actor_const(&r->session->game_state);
    const struct toy_input *input=rf_core_input(r->core);
    const struct toy_mesh_weaver *w=&r->session->game_state.weaver;
    const struct rf_story_camera_entity *c=rf_story_camera(&r->story,r->session);
    struct rf_device_query device;unsigned mask=0;rf_device_query(r,&device);
    for(int i=0;i<device.feature_count;++i)if(device.features[i].enabled)mask|=1u<<i;
    __printf("PLAYER-UI-AUDIT {\"frame\":%d,\"ui_mode\":%d,\"focus\":%d,\"window_focus\":%d,\"terminal\":%d,\"rts\":%d,\"x\":%d,\"z\":%d,\"fire_seq\":%u,\"slot\":%d,\"story\":%d,\"node\":%d,\"sr\":%u,\"nr\":%u,\"link\":%d,\"collapsed\":%d,\"queue\":%d,\"progress_a\":%d,\"progress_b\":%d,\"hold\":%u,\"camera_generation\":%u,\"camera_valid\":%d,\"weaver_panel\":%d,\"selected\":%d,\"phase\":%d,\"progress\":%.6f,\"serial\":%u,\"collected\":%u,\"power\":%d,\"command_code\":%d,\"video\":%d,\"video_frames\":%llu,\"video_cpu_us\":%lld,\"video_gpu_us\":%lld,\"weaver_focus\":%d,\"task_id\":%d,\"task_state\":%d,\"normal_sim\":%d,\"driver\":%d}\n",
        frame,r->player_ui.mode,r->comms_focus,input?input->keyboard_focused:0,r->console.open,r->rts_active,p->x,p->z,p->fire_seq,p->current_slot,
        r->story.active_story,r->story.node_id,r->story.session_revision,r->story.node_revision,r->story.link,
        r->story.collapsed,r->story.queue_count,r->story.progress[0],r->story.progress[1],r->story.hold_token,
        c?c->generation:0,c!=NULL,rf_weaver_terminal.open,r->player_controls.selected_blueprint,w->phase,
        w->progress,w->job_serial,w->collected_count,w->supply.power_on,r->player_controls.last.code,
        video->state,(unsigned long long)video->frames,(long long)video->last_cpu_us,(long long)(video->last_gpu_ms*1000),
        rf_weaver_terminal.row,r->story.task.id,r->story.task.state,!rf_weaver_diagnostic.enabled,
        rf_weaver_diagnostic.enabled||rf_weaver_performance.mode);
    __printf("PLAYER-UI-DEVICE {\"frame\":%d,\"combat\":%d,\"device_code\":%d,\"table_selected\":%d,\"table_pending\":%d,\"render_mask\":%u,\"table_open\":%d,\"table_dropdown\":%d,\"preview_state\":%d,\"preview_map\":%d}\n",
        frame,r->story.combat,r->device_service.last.code,device.selected_map,device.pending_world,mask,
        rf_table.open,rf_table.dropdown,rf_table.preview_state,rf_table.preview_map);
    if(c && c->actor_index>=0 && c->actor_index<TOY_GAME_MAX_ACTORS) {
        const struct toy_game_actor *actor=&r->session->game_state.actors[c->actor_index];
        __printf("PLAYER-UI-REMOTE {\"frame\":%d,\"null_x\":%d,\"null_z\":%d,\"null_actor_id\":%d,\"null_actor_generation\":%u}\n",
            frame,actor->x,actor->z,c->actor_id,c->actor_generation);
    }
    fflush(stdout);
}
static void rf_player_aux_prepare(struct rf_game_runtime *r,int width,int height,uint64_t now,int hidden)
{
    const struct rf_story_camera_entity *camera=rf_story_camera(&r->story,r->session);
    memset(&rf_player_aux,0,sizeof(rf_player_aux));
    memset(&rf_player_unit_aux,0,sizeof(rf_player_unit_aux));
    rf_player_unit_aux.now_us=now;rf_player_unit_aux.refresh_hz=12;
    rf_player_aux.render_width=320;rf_player_aux.render_height=180;rf_player_aux.refresh_hz=12;
    rf_player_aux.now_us=now;
    r->rts.video_requested=0;
    if(camera){rf_player_aux.camera=camera->view;rf_player_aux.shell_visible=1;rf_player_aux.stable_id=101;
        rf_player_aux.generation=camera->generation;}
    if(!hidden && rf_weaver_terminal.open && r->player_ui.mode<RF_PLAYER_UI_EXPERIMENT) {
        struct rf_player_weaver_rects rects;rf_player_weaver_layout(&rects,width,height,&r->player_ui);
        rf_player_aux.kind=RF_GPU_AUX_WEAPON;rf_player_aux.visible=rects.preview.w>0&&rects.preview.h>0;
        rf_player_aux.stable_id=1000+(unsigned)r->player_controls.selected_blueprint;
        rf_player_aux.generation=r->session->scene_local.world_generation;
        rf_player_aux.weapon=rf_weaver_selection()->geometry.weapon;
        rf_player_aux.rotation_degrees=r->player_controls.preview_yaw;
        rf_player_aux.x=rects.preview.x;rf_player_aux.y=rects.preview.y;
        rf_player_aux.width=rects.preview.w;rf_player_aux.height=rects.preview.h;
        if(rects.preview.w>0) {
            unsigned h=320u*(unsigned)rects.preview.h/(unsigned)rects.preview.w;
            rf_player_aux.render_height=h<64?64:h>480?480:h;
        }
    } else if(!hidden && camera && r->story.active_story&&!r->story.collapsed) {
        struct rf_player_comms_rects rects;rf_player_comms_layout(&rects,width,height,&r->player_ui,r->story.combat,0);
        rf_player_aux.kind=RF_GPU_AUX_WORLD;rf_player_aux.visible=1;
        rf_player_aux.x=rects.video.x;rf_player_aux.y=rects.video.y;
        rf_player_aux.width=rects.video.w;rf_player_aux.height=rects.video.h;
        /* Render the portrait view directly; blitting a landscape target into
         * this rectangle would squeeze the actor horizontally. */
        rf_player_aux.render_width=240;rf_player_aux.render_height=360;
    }
    if(!hidden && !rf_weaver_terminal.open && r->rts_active && !r->player_ui.rts_collapsed &&
        r->player_ui.mode<RF_PLAYER_UI_EXPERIMENT && rf_rts_count(&r->rts,&r->session->game_state)) {
        struct rf_ui_layout layout;
        int index=r->rts.primary;
        if(index<0 || index>=TOY_GAME_MAX_ACTORS || !rf_rts_member_valid(&r->rts.selected[index],&r->session->game_state,index))
            index=rf_rts_nth(&r->rts,&r->session->game_state,0);
        const struct toy_game_actor *a=&r->session->game_state.actors[index];
        rf_ui_layout_resolve(&layout,&r->player_ui,width,height,1);
        r->rts.video_requested=1;
        if(r->rts.video_actor_id!=a->actor_id || r->rts.video_generation!=a->combat_generation)
            r->rts.video_live=0;
        r->rts.video_actor_id=a->actor_id;r->rts.video_generation=a->combat_generation;
        rf_player_unit_aux.kind=RF_GPU_AUX_WORLD;rf_player_unit_aux.shell_visible=0;
        rf_player_unit_aux.stable_id=2000u+(unsigned)index;
        rf_player_unit_aux.generation=(r->rts.world_generation<<32)|a->combat_generation;
        /* Fixed actor-local front view; never changes gameplay facing. */
        struct camera *unit_camera=&rf_player_unit_aux.camera;
        memset(unit_camera,0,sizeof(*unit_camera));
        unit_camera->x=a->x+a->sy*720/1024;
        unit_camera->z=a->z+a->cy*720/1024;
        unit_camera->y=RASTERFALL_WORLD_GROUND_Y+a->ground_y+a->airborne_y+
            RASTERFALL_HUMAN_HEIGHT_RFU/2+RASTERFALL_RFU_FROM_CM(35);
        unit_camera->sy=-a->sy;unit_camera->cy=-a->cy;unit_camera->pitch_cy=1024;
        rf_player_unit_aux.visible=layout.video.w>0 && layout.video.h>0;
        rf_player_unit_aux.x=layout.video.x;rf_player_unit_aux.y=layout.video.y;
        rf_player_unit_aux.width=layout.video.w;rf_player_unit_aux.height=layout.video.h;
        rf_player_unit_aux.render_height=360;
        rf_player_unit_aux.render_width=layout.video.h>0?360u*layout.video.w/layout.video.h:0;
        if(!rf_player_unit_aux.visible){r->rts.video_live=0;r->rts.video_state=RF_GPU_AUX_UNAVAILABLE;}
    }
    if(!rf_player_aux.visible)r->ui_video_live=0;
    rf_gpu_scene_enemy_aux_camera(rf_player_aux.visible&&rf_player_aux.kind==RF_GPU_AUX_WORLD?&rf_player_aux.camera:NULL);
    rf_gpu_scene_enemy_unit_camera(rf_player_unit_aux.visible?&rf_player_unit_aux.camera:NULL);
}
#endif
static int rf_player_combat(const struct toy_game *g)
{
    const struct toy_game_actor *p=toy_game_local_player_actor_const(g);
    return p&&(p->damage_flash_ms>0 || p->fire_cooldown_ms>0 ||
        (g->campaign_phase==TOY_GAME_PHASE_HORDE && g->enemies_alive>0));
}
static void rf_player_story_tick(struct rf_game_runtime *r,int dt,int allow_start)
{
    int had_focus=r->comms_focus;
    if(r->net.mode!=RASTERFALL_NET_OFF){rf_story_detach(&r->story,r->session);return;}
    rf_story_frontier_events(&r->story,r->session,
        rf_frontier_mission_drain_events(&r->session->frontier));
    rf_story_update(&r->story,r->session,dt,rf_player_combat(&r->session->game_state),allow_start);
    rf_player_subtitles_update(&r->player_ui,&r->story,dt);
    if(r->chat_history_revision!=r->story.history_revision) {
        r->chat_history_revision=r->story.history_revision;
        r->chat_idle_ms=0;
        r->chat_last_content_us=rf_core_time_us(r->core);
        if(!r->comms_focus)r->chat_scroll=0;
    } else if(r->chat_idle_ms<10000) {
        r->chat_idle_ms=(int)((rf_core_time_us(r->core)-r->chat_last_content_us)/1000);
        if(r->chat_idle_ms>10000)r->chat_idle_ms=10000;
    }
    rf_player_controls_observe(r,dt);
    if(had_focus&&!r->comms_focus)rf_core_set_pointer_lock(r->core,!r->rts_active&&!r->console.open);
    if(r->persistence_retry_ms>0)r->persistence_retry_ms-=dt;
    if(r->player_persistence && r->persistence_retry_ms<=0) {
        int failed=0;
        if(r->story.dirty && rf_story_save(&r->story,r->story_save_path)<0)failed=1;
        if(r->player_controls.settings_dirty && rf_player_settings_save(r,r->ui_settings_path)<0)failed=1;
        if(failed) {
            r->persistence_retry_ms=5000;
            snprintf(r->player_controls.last.message,sizeof(r->player_controls.last.message),"保存失败：检查目录权限，5 秒后重试");
            r->player_controls.last.code=RF_PLAYER_UNAVAILABLE;
        }
    }
}
