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
    int hit=rf_player_ui_rts_hit(&ui,s,g,pointer_x,pointer_y);
    int normal=r->player_ui.mode<RF_PLAYER_UI_EXPERIMENT;
    int hover=rf_player_ui_rts_hit(&ui,s,g,in->pointer_x,in->pointer_y);
    s->hovered=normal && hover>=RF_PLAYER_UI_HIT_UNIT_BASE?hover-RF_PLAYER_UI_HIT_UNIT_BASE:-1;
    int blocked=normal && (rf_ui_rect_contains(ui.map,pointer_x,pointer_y) ||
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
    if(events->button_pressed && blocked) {
        s->drag_active=0;
        if(events->button==BTN_LEFT) {
            int button=rf_player_ui_hit_test(&r->player_ui,width,height,1,pointer_x,pointer_y);
            if(hit>=RF_PLAYER_UI_HIT_UNIT_BASE)rf_rts_select(s,g,hit-RF_PLAYER_UI_HIT_UNIT_BASE,0);
            else if(hit>=RF_PLAYER_UI_HIT_GROUP_BASE && hit<RF_PLAYER_UI_HIT_GROUP_BASE+RF_RTS_GROUPS)
                rf_rts_group(s,g,hit-RF_PLAYER_UI_HIT_GROUP_BASE,ctrl);
            else if(hit==RF_PLAYER_UI_HIT_PAGE_PREV && s->page>0)--s->page;
            else if(hit==RF_PLAYER_UI_HIT_PAGE_NEXT && (s->page+1)*rf_player_ui_rts_page_size(&ui)<rf_rts_count(s,g))++s->page;
            if(button==RF_PLAYER_UI_HIT_STOP)rf_player_request(r,RF_PLAYER_RTS_STOP,0,0,0);
            if(button==RF_PLAYER_UI_HIT_FOLLOW)rf_player_request(r,RF_PLAYER_RTS_FOLLOW,1,0,0);
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
            if(rts_surface_pick(&r->session->level,camera,width,height,pointer_x,pointer_y,
                r->session->air_walls_enabled,&x,&y,&z))rf_player_request(r,RF_PLAYER_RTS_MOVE,0,x,z);
        } else if(events->button==BTN_LEFT && r->rts_teleport_pending) {
            if(rts_surface_pick(&r->session->level,camera,width,height,pointer_x,pointer_y,
                r->session->air_walls_enabled,&x,&y,&z) &&
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
            else if(!blocked)rf_rts_select(s,g,rf_rts_pick(g,camera,width,height,s->pointer_x,s->pointer_y),s->drag_add);
            s->drag_active=0;
        }
    }
    r->rts_selected=rf_rts_count(s,g)==1 && s->primary>=0 &&
        &g->actors[s->primary]==toy_game_local_player_actor_const(g)?0:-1;
}
