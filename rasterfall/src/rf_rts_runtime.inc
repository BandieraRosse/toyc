static void rf_rts_move_request(struct rf_game_runtime *r,int x,int y,int z)
{
    struct rf_player_request request={0};request.operation=RF_PLAYER_RTS_MOVE;
    request.x=x;request.y=y;request.z=z;request.height_active=1;
    request.world_generation=r->session->scene_local.world_generation;
    rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER);
}
static void rf_rts_station_overview(struct rf_game_runtime *r,int width,int height)
{
    const struct rf_map_runtime_region *area=rf_map_runtime_find_region(
        &r->session->map_ops.runtime,"frontier_station_bounds");
    if(!area || width<=0 || height<=0)return;
    struct rf_ui_layout ui;rf_ui_layout_resolve(&ui,&r->player_ui,width,height,1);
    int top=ui.phase.y+ui.phase.h+ui.gap+30;
    int bottom=ui.groups.y>top?ui.groups.y:height;
    int usable=bottom-top-24;if(usable<height/3)usable=height/3;
    int distance=(area->bounds.max_z-area->bounds.min_z)*(width*3/4)/usable+6000;
    int across=(area->bounds.max_x-area->bounds.min_x)*3/4+6000;
    if(distance<across)distance=across;
    if(distance>RTS_CAMERA_MAX_DISTANCE)distance=RTS_CAMERA_MAX_DISTANCE;
    r->rts_camera_distance=distance;r->rts_follow_player=0;
    r->rts_camera_x=(area->bounds.min_x+area->bounds.max_x)/2;
    r->rts_camera_z=(area->bounds.min_z+area->bounds.max_z)/2-distance*90/1020-
        (height/2-(top+bottom)/2)*distance/(width*3/4);
}
static void rf_rts_floor_focus(struct rf_game_runtime *r,int index,int manual)
{
    struct rf_rts_state *s=&r->rts;
    if(index<0) {
        if(s->focus_floor>=0) {
            r->rts_camera_x=s->saved_x;r->rts_camera_z=s->saved_z;
            r->rts_camera_distance=s->saved_distance;
        }
        s->focus_floor=-1;s->floor_manual=0;r->rts_follow_player=0;return;
    }
    if(index>=s->floor_count)return;
    const struct rf_map_runtime_region *f=&s->floors[index];
    if(s->focus_floor<0) {
        s->saved_x=r->rts_camera_x;s->saved_z=r->rts_camera_z;
        s->saved_distance=r->rts_camera_distance;
    }
    if(s->focus_floor!=index && !f->is_roof) {
        s->cutaway_floor=index;s->cutaway_amount=0;
    }
    s->focus_floor=index;s->floor_manual=manual;r->rts_follow_player=0;
    r->rts_camera_x=f->bounds.min_x+(f->bounds.max_x-f->bounds.min_x)/2;
    r->rts_camera_z=f->bounds.min_z+(f->bounds.max_z-f->bounds.min_z)/2-
        r->rts_camera_distance*90/1020;
    struct toy_surface *surface=rf_core_surface(r->core);
    if(surface && surface->width>0 && surface->height>0) {
        struct rf_ui_layout ui;
        rf_ui_layout_resolve(&ui,&r->player_ui,surface->width,surface->height,1);
        int top=ui.phase.y+ui.phase.h+ui.gap+30;
        int bottom=ui.groups.y>top?ui.groups.y:surface->height;
        r->rts_camera_z-=(surface->height/2-(top+bottom)/2)*r->rts_camera_distance/
            (surface->width*3/4);
    }
}
static void rf_rts_floor_button(struct rf_game_runtime *r,int button)
{
    if(button==0){rf_rts_floor_focus(r,-1,0);return;}
    if(button<=r->rts.floor_count) {
        int overview=r->rts.focus_floor<0;
        rf_rts_floor_focus(r,button-1,1);
        if(overview) {
            r->rts_camera_distance=24000;
            rf_rts_floor_focus(r,button-1,1);
        }
        return;
    }
    int unit=r->rts.primary;
    if(unit>=0 && rf_rts_member_valid(&r->rts.selected[unit],&r->session->game_state,unit)) {
        const struct toy_game_actor *a=&r->session->game_state.actors[unit];
        int floor=rf_rts_floor_at(&r->rts,a->x,a->ground_y+a->airborne_y,a->z);
        /* Save the overview before adjusting the zoom. */
        rf_rts_floor_focus(r,floor,0);
        r->rts_camera_distance=6000;r->rts_follow_player=1;
    }
}
static void rf_rts_focus_pick(struct rf_game_runtime *r,const struct camera *camera,
    int w,int h,int sx,int sy,int entering)
{
    int x,y,z;
    if(!rts_surface_pick(&r->session->level,camera,w,h,sx,sy,
        r->session->air_walls_enabled,&x,&y,&z))return;
    int floor=rf_rts_floor_at(&r->rts,x,y+900,z);
    if(floor<0)return;
    if(!entering) {
        const char *building=r->rts.floors[floor].building;
        for(int i=0;i<r->rts.floor_count;++i)
            if(!strcmp(r->rts.floors[i].building,building) && r->rts.floors[i].is_roof)floor=i;
    }
    rf_rts_floor_focus(r,floor,entering);
}
static void rf_rts_floor_zoom(struct rf_game_runtime *r,const struct rf_input_frame *input,
    struct camera *camera,int w,int h)
{
    struct rf_rts_state *s=&r->rts;
    if(r->rts_camera_distance>=36000 && s->focus_floor>=0) {
        int distance=r->rts_camera_distance;
        rf_rts_floor_focus(r,-1,0);r->rts_camera_distance=distance;
    } else if(input->wheel_y>0 && r->rts_camera_distance<18000 && !s->floor_manual) {
        if(s->focus_floor<0)rf_rts_focus_pick(r,camera,w,h,input->pointer_x,input->pointer_y,1);
        if(s->focus_floor>=0 && s->floors[s->focus_floor].is_roof) {
            const char *building=s->floors[s->focus_floor].building;
            for(int i=0;i<s->floor_count;++i)if(!strcmp(s->floors[i].building,building)) {
                rf_rts_floor_focus(r,i,0);break;
            }
        }
    }
    if(r->rts_follow_player && s->primary>=0) {
        const struct toy_game_actor *a=&r->session->game_state.actors[s->primary];
        int floor=rf_rts_floor_at(s,a->x,a->ground_y+a->airborne_y,a->z);
        if(floor!=s->focus_floor) {
            int distance=r->rts_camera_distance;
            rf_rts_floor_focus(r,floor,0);r->rts_camera_distance=distance;
        }
        r->rts_follow_player=1;
    }
}
static void rf_rts_cutaway_update(struct rf_game_runtime *r,struct rf_gpu_scene_layers_input *layers)
{
    struct rf_rts_state *s=&r->rts;
    int64_t now=rf_core_clock_now_us(),elapsed=now-s->cutaway_time_us;
    s->cutaway_time_us=now;
    if(elapsed<0)elapsed=0;
    if(elapsed>50000)elapsed=50000;
    float target=r->rts_active && s->focus_floor>=0 && !s->floors[s->focus_floor].is_roof?1:0;
    float step=(float)elapsed/220000.0f;
    if(s->cutaway_amount<target){s->cutaway_amount+=step;if(s->cutaway_amount>target)s->cutaway_amount=target;}
    else {s->cutaway_amount-=step;if(s->cutaway_amount<target)s->cutaway_amount=target;}
    if(!r->rts_active)s->cutaway_amount=0;
    if(s->cutaway_amount>0 && s->cutaway_floor>=0 && s->cutaway_floor<s->floor_count) {
        const struct rf_map_runtime_region *f=&s->floors[s->cutaway_floor];
        layers->cutaway_bounds[0]=f->bounds.min_x;layers->cutaway_bounds[1]=f->bounds.max_x;
        layers->cutaway_bounds[2]=f->bounds.min_z;layers->cutaway_bounds[3]=f->bounds.max_z;
        layers->cutaway_height[0]=RASTERFALL_WORLD_GROUND_Y+f->ceiling_y-2;
        layers->cutaway_height[1]=s->cutaway_amount;
    }
}
static void rf_rts_runtime_input(struct rf_game_runtime *r,struct rf_input_frame *in,
    struct toy_window_events *events,unsigned char *physical,unsigned char *keys,
    struct camera *camera,int width,int height)
{
    struct rf_rts_state *s=&r->rts;
    struct toy_game *g=&r->session->game_state;
    struct rf_ui_layout ui;
    rf_ui_layout_resolve(&ui,&r->player_ui,width,height,1);
    s->pointer_x=in->pointer_x;s->pointer_y=in->pointer_y;
    int pointer_x=events->button_pressed?events->button_x:in->pointer_x;
    int pointer_y=events->button_pressed?events->button_y:in->pointer_y;
    int ctrl=action_down(in,RF_ACTION_CONTROL),add=action_down(in,RF_ACTION_MODIFIER);
    for(int n=0;n<RF_RTS_GROUPS;++n)if(action_pressed(in,RF_ACTION_RTS_GROUP_1+n)) {
        action_consume(in,physical,RF_ACTION_RTS_GROUP_1+n);keys[2+n]=0;
        rf_rts_group(s,g,n,ctrl);s->drag_active=0;
        if(ctrl) {
            snprintf(r->player_controls.last.message,sizeof(r->player_controls.last.message),
                "编组 %d：%d 单位",(n+1)%10,rf_rts_group_count(s,g,n));
            r->session->banner_text=r->player_controls.last.message;r->session->banner_ms=1500;
        }
    }
    int floor_hit=rf_player_ui_floor_hit(&ui,s,pointer_x,pointer_y);
    int hit=rf_player_ui_rts_hit(&ui,s,g,pointer_x,pointer_y);
    int normal=r->player_ui.mode<RF_PLAYER_UI_EXPERIMENT;
    int hover=rf_player_ui_rts_hit(&ui,s,g,in->pointer_x,in->pointer_y);
    int grid_legend=r->player_ui.grid_view && g->grid_enabled && g->grid_valid &&
        rf_ui_rect_contains(rf_player_ui_grid_legend_rect(&ui),pointer_x,pointer_y);
    s->hovered=normal && hover>=RF_PLAYER_UI_HIT_UNIT_BASE?hover-RF_PLAYER_UI_HIT_UNIT_BASE:-1;
    int blocked=normal && (floor_hit>=0 || rf_ui_rect_contains(ui.map,pointer_x,pointer_y) ||
        rf_ui_rect_contains(ui.objective,pointer_x,pointer_y) ||
        rf_ui_rect_contains(ui.selection,pointer_x,pointer_y) ||
        rf_ui_rect_contains(ui.commands,pointer_x,pointer_y) ||
        rf_ui_rect_contains(ui.groups,pointer_x,pointer_y) ||
        rf_ui_rect_contains(ui.portrait,pointer_x,pointer_y) ||
        rf_ui_rect_contains(ui.dock_toggle,pointer_x,pointer_y) ||
        rf_ui_rect_contains(ui.resources,pointer_x,pointer_y) ||
        rf_ui_rect_contains(ui.phase,pointer_x,pointer_y));
    if(normal && r->story.active_story && !r->story.collapsed &&
        !rf_story_subtitle_only(&r->story)) {
        struct rf_player_comms_rects rects;
        rf_player_comms_layout(&rects,width,height,&r->player_ui,r->story.combat,0);
        blocked|=rf_ui_rect_contains(rects.window,pointer_x,pointer_y);
    }
    if(normal && rf_ui_rect_contains(ui.selection,in->pointer_x,in->pointer_y) && in->wheel_y) {
        int maxpage=(rf_rts_count(s,g)-1)/rf_player_ui_rts_page_size(&ui);
        s->page-=in->wheel_y;if(s->page<0)s->page=0;if(s->page>maxpage)s->page=maxpage;
        in->wheel_y=0;
    }
    blocked|=grid_legend;
    if(events->button_pressed && blocked) {
        s->drag_active=0;
        if(events->button==BTN_LEFT) {
            if(floor_hit>=0)rf_rts_floor_button(r,floor_hit);
            int button=rf_player_ui_hit_test(&r->player_ui,width,height,1,pointer_x,pointer_y);
            if(hit>=RF_PLAYER_UI_HIT_UNIT_BASE)rf_rts_select(s,g,hit-RF_PLAYER_UI_HIT_UNIT_BASE,0);
            else if(hit>=RF_PLAYER_UI_HIT_GROUP_BASE && hit<RF_PLAYER_UI_HIT_GROUP_BASE+RF_RTS_GROUPS)
                rf_rts_group(s,g,hit-RF_PLAYER_UI_HIT_GROUP_BASE,ctrl);
            else if(hit==RF_PLAYER_UI_HIT_PAGE_PREV && s->page>0)--s->page;
            else if(hit==RF_PLAYER_UI_HIT_PAGE_NEXT && (s->page+1)*rf_player_ui_rts_page_size(&ui)<rf_rts_count(s,g))++s->page;
            if(button==RF_PLAYER_UI_HIT_STOP && rf_rts_count(s,g)>0)rf_player_request(r,RF_PLAYER_RTS_STOP,0,0,0);
            if(button==RF_PLAYER_UI_HIT_FOLLOW && rf_rts_count(s,g)>0)rf_player_request(r,RF_PLAYER_RTS_FOLLOW,!r->rts_follow_player,0,0);
            if(button==RF_PLAYER_UI_HIT_GRID && g->grid_enabled && g->grid_valid)
                rf_player_request(r,RF_PLAYER_GRID_VIEW,!r->player_ui.grid_view,0,0);
            if(button==RF_PLAYER_UI_HIT_FPS)rf_player_request(r,RF_PLAYER_VIEW,0,0,0);
            if(button==RF_PLAYER_UI_HIT_DOCK)r->player_ui.rts_collapsed=!r->player_ui.rts_collapsed;
        }
        if(rf_ui_rect_contains(ui.map,pointer_x,pointer_y)) {
            struct rf_minimap_view map_view;struct rf_player_ui_view v={0};int x,z;v.rts_active=1;
            rf_player_ui_map_view(&map_view,&r->player_ui,&v,toy_game_local_player_actor_const(g),width,height);
            if(rf_minimap_screen_to_world(&map_view,pointer_x,pointer_y,&x,&z)) {
                if(events->button==BTN_LEFT){r->rts_camera_x=x;r->rts_camera_z=z;r->rts_follow_player=0;}
                else if(events->button==BTN_RIGHT)rf_player_request(r,RF_PLAYER_RTS_MOVE,0,x,z);
            }
        }
        events->button_pressed=0;
    }
    if(events->button_pressed && !blocked) {
        int x,y,z;
        if(events->button==BTN_RIGHT) {
            s->drag_active=0;
            if(rts_surface_pick_floor(&r->session->level,camera,width,height,pointer_x,pointer_y,
                r->session->air_walls_enabled,s->focus_floor>=0?&s->floors[s->focus_floor]:NULL,&x,&y,&z))rf_rts_move_request(r,x,y+900,z);
        } else if(events->button==BTN_LEFT && r->rts_teleport_pending) {
            if(rts_surface_pick_floor(&r->session->level,camera,width,height,pointer_x,pointer_y,
                r->session->air_walls_enabled,s->focus_floor>=0?&s->floors[s->focus_floor]:NULL,&x,&y,&z) &&
                rasterfall_session_rts_teleport_player(r->session,&r->camera,x,y,z)) {
                r->rts_teleport_pending=0;rf_player_request(r,RF_PLAYER_RTS_SELECT,0,0,0);
            }
        } else if(events->button==BTN_LEFT) {
            s->drag_active=1;s->drag_started=0;s->drag_add=add;
            s->drag_x=pointer_x;s->drag_y=pointer_y;
        }
        events->button_pressed=0;
    }
    if(!in->keyboard_focused || r->comms_focus){s->drag_active=0;return;}
    if(s->drag_active) {
        if(abs(s->pointer_x-s->drag_x)>5 || abs(s->pointer_y-s->drag_y)>5)s->drag_started=1;
        if(!(in->mouse_buttons&1)) {
            if(s->drag_started)rf_rts_box(s,g,camera,width,height,s->drag_x,s->drag_y,
                s->pointer_x,s->pointer_y,s->drag_add);
            else if(!blocked) {
                int unit=rf_rts_pick_floor(s,g,camera,width,height,s->pointer_x,s->pointer_y);
                rf_rts_select(s,g,unit,s->drag_add);
                if(unit<0 && !s->drag_add)rf_rts_focus_pick(r,camera,width,height,s->pointer_x,s->pointer_y,0);
            }
            s->drag_active=0;
        }
    }
    r->rts_selected=rf_rts_count(s,g)==1 && s->primary>=0 &&
        &g->actors[s->primary]==toy_game_local_player_actor_const(g)?0:-1;
}
