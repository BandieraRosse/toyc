/* RTS command dock. All rectangles are shared with input hit testing. */
static unsigned rts_health_color(const struct rf_ui_theme *t,const struct toy_game_actor *a)
{ return a->hp<=a->max_hp/4?t->danger:a->hp<=a->max_hp/2?t->warning:t->success; }

static void rts_ui_line(struct rasterfall_canvas *c,int x0,int y0,int x1,int y1,unsigned color,int alpha)
{
    /* Clip before stepping so off-screen world targets have bounded cost. */
    double t0=0,t1=1,dx=(double)x1-x0,dy=(double)y1-y0;
    double p[4]={-dx,dx,-dy,dy},q[4]={x0,c->width-1-x0,y0,c->height-1-y0};
    for(int i=0;i<4;++i) {
        if(p[i]==0){if(q[i]<0)return;continue;}
        double t=q[i]/p[i];
        if(p[i]<0){if(t>t1)return;if(t>t0)t0=t;}
        else {if(t<t0)return;if(t<t1)t1=t;}
    }
    x1=x0+(int)(t1*dx);y1=y0+(int)(t1*dy);
    x0+=(int)(t0*dx);y0+=(int)(t0*dy);
    int ax=abs(x1-x0),ay=-abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,e=ax+ay;
    int start=x0,row=y0,last=x0;
    for(;;) {
        if(y0!=row) {
            rasterfall_canvas_rect(c,ui_min(start,last),row,abs(last-start)+1,1,color,alpha);
            start=x0;row=y0;
        }
        if(x0==x1 && y0==y1)break;
        last=x0;
        int e2=2*e;if(e2>=ay){e+=ay;x0+=sx;}if(e2<=ax){e+=ax;y0+=sy;}
    }
    rasterfall_canvas_rect(c,ui_min(start,x0),row,abs(x0-start)+1,1,color,alpha);
}
static void rts_ui_outline(struct rasterfall_canvas *c,struct rf_ui_rect r,unsigned color,int alpha)
{
    rasterfall_canvas_rect(c,r.x,r.y,r.w,1,color,alpha);
    rasterfall_canvas_rect(c,r.x,r.y+r.h-1,r.w,1,color,alpha);
    rasterfall_canvas_rect(c,r.x,r.y,1,r.h,color,alpha);
    rasterfall_canvas_rect(c,r.x+r.w-1,r.y,1,r.h,color,alpha);
}
static struct rf_ui_rect rts_group_rect(const struct rf_ui_layout *l,int n)
{
    struct rf_ui_rect r=l->groups;int left=r.w*n/RF_RTS_GROUPS,right=r.w*(n+1)/RF_RTS_GROUPS;
    r.x+=left;r.w=right-left-ui_max(2,ui_px(3,l->scale_milli));return r;
}
static int rts_columns(const struct rf_ui_layout *l)
{ return ui_max(1,(l->selection.w-2*l->padding)/ui_max(1,ui_px(80,l->scale_milli))); }
static int rts_rows(const struct rf_ui_layout *l)
{ return ui_max(1,(l->selection.h-ui_px(49,l->scale_milli))/ui_max(1,ui_px(46,l->scale_milli))); }
int rf_player_ui_rts_page_size(const struct rf_ui_layout *l)
{ return rts_columns(l)*rts_rows(l); }
static struct rf_ui_rect rts_card_rect(const struct rf_ui_layout *l,int n)
{
    int cols=rts_columns(l),rows=rts_rows(l),gap=ui_max(2,ui_px(3,l->scale_milli));
    int w=(l->selection.w-2*l->padding)/cols;
    int h=(l->selection.h-ui_px(49,l->scale_milli))/rows;
    return ui_rect(l->selection.x+l->padding+(n%cols)*w,
        l->selection.y+ui_px(27,l->scale_milli)+(n/cols)*h,w-gap,h-gap);
}
static struct rf_ui_rect rts_page_rect(const struct rf_ui_layout *l,int next)
{
    int w=ui_px(30,l->scale_milli),h=ui_px(20,l->scale_milli);
    return ui_rect(l->selection.x+l->selection.w-l->padding-w*(next?1:2),
        l->selection.y+l->selection.h-h-2,w-2,h);
}
static int rts_page(const struct rf_ui_layout *l,const struct rf_rts_state *s,const struct toy_game *g)
{ return ui_clamp(s->page,0,ui_max(0,(rf_rts_count(s,g)-1)/rf_player_ui_rts_page_size(l))); }
int rf_player_ui_rts_hit(const struct rf_ui_layout *l,const struct rf_rts_state *s,
    const struct toy_game *g,int x,int y)
{
    if(!s || l->selection.w<=0)return RF_PLAYER_UI_HIT_NONE;
    for(int i=0;i<RF_RTS_GROUPS;++i)
        if(rf_ui_rect_contains(rts_group_rect(l,i),x,y))return RF_PLAYER_UI_HIT_GROUP_BASE+i;
    int count=rf_rts_count(s,g),size=rf_player_ui_rts_page_size(l),page=rts_page(l,s,g);
    if(count>1) {
        if(count>size)for(int i=0;i<2;++i)
            if(rf_ui_rect_contains(rts_page_rect(l,i),x,y))return RF_PLAYER_UI_HIT_PAGE_PREV+i;
        for(int i=0;i<size && page*size+i<count;++i)
            if(rf_ui_rect_contains(rts_card_rect(l,i),x,y))
                return RF_PLAYER_UI_HIT_UNIT_BASE+rf_rts_nth(s,g,page*size+i);
    }
    return RF_PLAYER_UI_HIT_NONE;
}
static void rts_wire(struct rasterfall_canvas *c,struct rf_ui_rect r,const struct toy_game_actor *a,unsigned color)
{
    const unsigned char *mask=rf_rts_portrait(a);
    if(!mask || r.w<1 || r.h<1)return;
    int w=ui_min(r.w,r.h*RF_RTS_PORTRAIT_W/RF_RTS_PORTRAIT_H);
    r.x+=(r.w-w)/2;r.w=w;
    /* Downsampling uses the entire source footprint so fine mesh edges do not
     * disappear when the same portrait becomes a small multi-selection icon. */
    for(int y=0;y<r.h;++y) {
        int run=-1,opacity=0;
        for(int x=0;x<=r.w;++x) {
            int on=0;
            if(x<r.w) {
                int ax=x*RF_RTS_PORTRAIT_W/r.w,bx=ui_max(ax+1,(x+1)*RF_RTS_PORTRAIT_W/r.w);
                int ay=y*RF_RTS_PORTRAIT_H/r.h,by=ui_max(ay+1,(y+1)*RF_RTS_PORTRAIT_H/r.h);
                for(int yy=ay;yy<by;++yy)for(int xx=ax;xx<bx;++xx)
                    on=ui_max(on,mask[yy*RF_RTS_PORTRAIT_W+xx]);
            }
            if(on!=opacity) {
                if(run>=0)rasterfall_canvas_rect(c,r.x+run,r.y+y,x-run,1,color,opacity);
                run=on?x:-1;opacity=on;
            }
        }
    }
}
static void rts_unit_details(struct rasterfall_canvas *c,struct rf_ui_rect r,
    const struct toy_game_actor *a,const struct rf_ui_theme *t,int scale,int compact)
{
    struct toy_game_capabilities caps;char line[32];
    int weapon=toy_game_actor_current_weapon(a),step=ui_px(20,scale),row=0;
    toy_game_actor_capabilities(a,weapon,&caps);
    /* Rows are data-driven; new statistics do not change the panel topology. */
    char rows[6][160];
    snprintf(rows[row++],160,"%s",a->name[0]?a->name:"PLAYER");
    snprintf(rows[row++],160,"%s",weapon>=0?toy_game_weapon_name(weapon):"未装备武器");
    snprintf(rows[row++],160,"生命 %d / %d",ui_max(0,a->hp),a->max_hp);
    if(!compact) {
        snprintf(rows[row++],160,"回避 %d / %d",ui_max(0,(a->evasion.reserve_milli+999)/1000),caps.evasion_capacity);
        int slot=ui_clamp(a->current_slot,0,TOY_GAME_WEAPON_SLOTS-1);
        if(a->slots[slot].reserve==TOY_GAME_AMMO_INFINITE)snprintf(line,sizeof(line),"无限");
        else snprintf(line,sizeof(line),"%d",a->slots[slot].reserve);
        snprintf(rows[row++],160,"弹药 %d / %s",a->slots[slot].mag,line);
    }
    snprintf(rows[row++],160,"%s",a->state==TOY_GAME_ACTOR_DOWNED?"倒地 · 等待救援":
        a->movement_hold_token?"剧情交互中":a->moving?"移动中":a->reloading?"换弹中":"待命");
    for(int i=0;i<row && (i+1)*step<=r.h+step-ui_px(16,scale);++i)
        rf_ui_text(c,ui_rect(r.x,r.y+i*step,r.w,step),rows[i],
            i==2?rts_health_color(t,a):i==0?t->text:t->muted,scale,1);
}
struct rf_ui_rect rf_player_ui_floor_rect(const struct rf_ui_layout *l,const struct rf_rts_state *s,int n)
{
    int extent=l->resources.x+l->resources.w+l->margin;
    int count=s->floor_count+2,width=ui_min(ui_px(560,l->scale_milli),extent-2*l->margin);
    int cell=width/count;
    return ui_rect((extent-width)/2+n*cell,l->phase.y+l->phase.h+l->gap,
        cell-ui_max(2,l->gap/2),ui_px(30,l->scale_milli));
}
int rf_player_ui_floor_hit(const struct rf_ui_layout *l,const struct rf_rts_state *s,int x,int y)
{
    if(!s || !s->floor_count)return -1;
    for(int n=0;n<s->floor_count+2;++n)
        if(rf_ui_rect_contains(rf_player_ui_floor_rect(l,s,n),x,y))return n;
    return -1;
}
static void rts_floor_panel(struct rasterfall_canvas *c,const struct rasterfall_hud_state *h,
    const struct rf_ui_layout *l)
{
    const struct rf_rts_state *s=h->player_ui_view.rts;
    const struct rf_ui_theme *t=&h->player_ui->theme;
    if(!s || !s->floor_count)return;
    for(int n=0;n<s->floor_count+2;++n) {
        struct rf_ui_rect rect=rf_player_ui_floor_rect(l,s,n);
        const char *text=n==0?"全貌":n==s->floor_count+1?"聚焦单位":s->floors[n-1].floor_name;
        int scale=ui_min(l->text_scale_milli,ui_max(650,(rect.w-8)*1000/
            ui_max(1,rf_ui_font_text_width(text,1000))));
        rf_ui_button(c,rect,t,text,scale,n==s->focus_floor+1,
            n<=s->floor_count || s->primary>=0);
    }
}
static void rts_world_feedback(struct rasterfall_canvas *canvas,const struct rasterfall_hud_state *h,
    const struct rf_ui_layout *l)
{
    const struct rf_rts_state *s=h->player_ui_view.rts;
    const struct camera *camera=h->player_ui_view.rts_camera;
    if(!s || !camera)return;
    rts_floor_panel(canvas,h,l);
    const struct rf_ui_theme *t=&h->player_ui->theme;
    struct rasterfall_canvas c=*canvas;
    /* Never draw tactical world marks over transparent video or dock areas. */
    if(l->selection.w>0)c.height=ui_min(c.height,l->groups.y);
    const struct toy_game_actor *player=toy_game_local_player_actor_const(h->game);
    /* Interior allied markers remain readable in overview and on other
     * floors. Hostile actors do not gain an overlay through solid roofs. */
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        const struct toy_game_actor *a=&h->game->actors[i];
        if(!rf_rts_selectable(h->game,i))continue;
        int floor=rf_rts_floor_at(s,a->x,a->ground_y+a->airborne_y,a->z),sx,sy;
        if(floor<0 || (s->focus_floor>=0 && rf_rts_floor_allows(s,a)))continue;
        if(!rf_rts_project(camera,canvas->width,canvas->height,a->x,
            RASTERFALL_WORLD_GROUND_Y+a->ground_y+RASTERFALL_HUMAN_HEIGHT_RFU/2,a->z,&sx,&sy))continue;
        char label[16];int level=s->floors[floor].floor_level;
        snprintf(label,sizeof(label),s->floors[floor].is_roof?"屋顶":level<0?"B%d":"%dF",level<0?-level:level);
        rasterfall_canvas_rect(&c,sx-4,sy-4,8,8,t->success,s->focus_floor<0?230:110);
        rf_ui_text(&c,ui_rect(sx+7,sy-9,50,18),label,t->muted,750,1);
    }
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        const struct toy_game_actor *a=&h->game->actors[i];
        if(!rf_rts_member_valid(&s->selected[i],h->game,i))continue;
        int y=RASTERFALL_WORLD_GROUND_Y+a->ground_y+12;
        int tx=a->command_x,tz=a->command_z,move=a->command_destination_active;
        if(a==player){move=h->player_ui_view.rts_move_active;tx=h->player_ui_view.rts_move_x;tz=h->player_ui_view.rts_move_z;}
        long long dx=(long long)tx-a->x,dz=(long long)tz-a->z;
        if(move && dx*dx+dz*dz>250LL*250) {
            int ax,ay,bx,by;
            int target_y=a==player?h->player_ui_view.rts_move_y:
                a->command_height_active?a->command_y:a->ground_y;
            struct toy_game_ground_query ground=toy_game_query_ground(h->game,tx,tz,RASTERFALL_PLAYER_RADIUS,target_y);
            int gy=RASTERFALL_WORLD_GROUND_Y+(ground.has_support?ground.support_y:a->ground_y)+14;
            if(rf_rts_project(camera,canvas->width,canvas->height,a->x,y,a->z,&ax,&ay) &&
                rf_rts_project(camera,canvas->width,canvas->height,tx,gy,tz,&bx,&by)) {
                rts_ui_line(&c,ax,ay,bx,by,0x77DFA3,140);
                rts_ui_line(&c,bx-5,by,bx,by-4,0xACFFD0,255);
                rts_ui_line(&c,bx,by-4,bx+5,by,0xACFFD0,255);
                rts_ui_line(&c,bx+5,by,bx,by+4,0xACFFD0,255);
                rts_ui_line(&c,bx,by+4,bx-5,by,0xACFFD0,255);
            }
        }
        for(int n=0;n<32;++n) {
            double t=n*6.283185307179586/32,u=(n+1)*6.283185307179586/32;
            int ax,ay,bx,by,radius=RASTERFALL_PLAYER_RADIUS+100;
            if(rf_rts_project(camera,canvas->width,canvas->height,a->x+(int)(cos(t)*radius),y,a->z+(int)(sin(t)*radius),&ax,&ay) &&
                rf_rts_project(camera,canvas->width,canvas->height,a->x+(int)(cos(u)*radius),y,a->z+(int)(sin(u)*radius),&bx,&by))
                rts_ui_line(&c,ax,ay,bx,by,i==s->hovered?0xDCFFE2:0x4CF586,255);
        }
    }
    if(s->drag_active && s->drag_started) {
        struct rf_ui_rect r=ui_rect(ui_min(s->drag_x,s->pointer_x),ui_min(s->drag_y,s->pointer_y),
            abs(s->pointer_x-s->drag_x)+1,abs(s->pointer_y-s->drag_y)+1);
        rasterfall_canvas_rect(&c,r.x,r.y,r.w,r.h,0x65ECAB,20);rts_ui_outline(&c,r,0x79F5AF,255);
    }
    if(c.failed)canvas->failed=1;
}
static void ui_rts(struct rasterfall_canvas *c,const struct rasterfall_hud_state *h,const struct rf_ui_layout *l)
{
    const struct rf_player_ui_view *v=&h->player_ui_view;
    const struct rf_rts_state *s=v->rts;
    const struct rf_ui_theme *t=&h->player_ui->theme;
    int scale=l->text_scale_milli,pad=l->padding,count=s?rf_rts_count(s,h->game):0;
    struct rf_ui_rect r=l->selection;char line[192],key[24];
    rf_ui_panel(c,r,t,0);
    for(int i=0;i<RF_RTS_GROUPS;++i) {
        int members=s?rf_rts_group_count(s,h->game,i):0;
        struct rf_ui_rect gr=rts_group_rect(l,i);
        snprintf(line,sizeof(line),"%d",members);
        /* Compact strip uses a slightly smaller font while main info keeps
         * the normal UI text size. Group count remains visible at 720p. */
        int group_scale=ui_max(750,scale*3/4),label_width=rf_ui_font_text_width(line,group_scale);
        if(label_width>gr.w-10)group_scale=ui_max(650,(gr.w-10)*group_scale/ui_max(1,label_width));
        rf_ui_button(c,gr,t,line,group_scale,s && s->active_group==i,members>0);
    }
    if(!count)rf_ui_text(c,ui_rect(r.x+pad,r.y+pad,r.w-pad*2,r.h-pad*2),
        "核心指挥\n左键点选 / 拖动框选\nCtrl + 数字：重设编组\n空选时 Ctrl + 数字：清空组",t->muted,scale,4);
    else if(count==1) {
        const struct toy_game_actor *a=&h->game->actors[rf_rts_nth(s,h->game,0)];
        int mw=ui_min(r.w/3,ui_px(105,scale));
        struct rf_ui_rect mesh=ui_rect(r.x+pad,r.y+pad,mw-pad,r.h-pad*2);
        for(int y=mesh.y;y<mesh.y+mesh.h;y+=ui_px(12,scale))
            rasterfall_canvas_rect(c,mesh.x,y,mesh.w,1,t->border,45);
        rts_wire(c,mesh,a,rts_health_color(t,a));
        rasterfall_canvas_rect(c,r.x+mw+pad,r.y+pad,1,r.h-pad*2,t->border,130);
        rts_unit_details(c,ui_rect(r.x+mw+pad*2,r.y+pad,r.w-mw-pad*3,r.h-pad*2),a,t,scale,0);
    } else {
        int size=rf_player_ui_rts_page_size(l),page=rts_page(l,s,h->game);
        snprintf(line,sizeof(line),"已选择 %d 单位",count);
        rf_ui_text(c,ui_rect(r.x+pad,r.y+ui_px(5,scale),r.w-pad*2,ui_px(20,scale)),line,t->text,scale,1);
        for(int n=0;n<size && page*size+n<count;++n) {
            int index=rf_rts_nth(s,h->game,page*size+n);
            const struct toy_game_actor *a=&h->game->actors[index];
            struct rf_ui_rect card=rts_card_rect(l,n);unsigned hp=rts_health_color(t,a);
            rasterfall_canvas_rect(c,card.x,card.y,card.w,card.h,t->panel_raised,240);
            rts_ui_outline(c,card,index==s->hovered?t->selected:index==s->primary?t->accent:t->border,255);
            rts_wire(c,ui_rect(card.x+2,card.y+2,card.w/2,card.h-9),a,hp);
            snprintf(line,sizeof(line),"%d",a->actor_id);
            rf_ui_text(c,ui_rect(card.x+card.w/2,card.y+3,card.w/2-2,ui_px(18,scale)),line,t->muted,ui_max(750,scale*3/4),1);
            const struct toy_game_weapon_info *weapon=toy_game_weapon_info_or_null(toy_game_actor_current_weapon(a));
            rf_ui_text(c,ui_rect(card.x+card.w/2,card.y+ui_px(22,scale),card.w/2-2,ui_px(18,scale)),
                weapon?weapon->short_name:"--",t->text,ui_max(750,scale*3/4),1);
            ui_bar(c,card.x+3,card.y+card.h-5,card.w-6,3,a->hp,a->max_hp,hp);
        }
        snprintf(line,sizeof(line),"%d / %d  ·  点击单位单选",page+1,(count+size-1)/size);
        rf_ui_text(c,ui_rect(r.x+pad,r.y+r.h-ui_px(20,scale),r.w-pad*2-ui_px(65,scale),ui_px(20,scale)),line,t->muted,ui_max(750,scale*3/4),1);
        if(count>size)for(int i=0;i<2;++i)
            rf_ui_button(c,rts_page_rect(l,i),t,i?">":"<",scale,0,i?page*size+size<count:page>0);
    }
    /* Four panels around the video preserve its transparent GPU composite. */
    struct rf_ui_rect p=l->portrait,video=l->video;
    rasterfall_canvas_rect(c,p.x,p.y,p.w,video.y-p.y,t->panel,245);
    rasterfall_canvas_rect(c,p.x,video.y,p.w>0?video.x-p.x:0,video.h,t->panel,245);
    rasterfall_canvas_rect(c,video.x+video.w,video.y,p.x+p.w-video.x-video.w,video.h,t->panel,245);
    rasterfall_canvas_rect(c,p.x,video.y+video.h,p.w,p.y+p.h-video.y-video.h,t->panel,245);
    rts_ui_outline(c,p,t->border,255);
    int camera_scale=ui_max(650,ui_min(scale*3/4,(p.w-8)*1000/ui_max(1,rf_ui_font_text_width("单位镜头",1000))));
    if(!s || !s->video_live || !s->video_requested) {
        rasterfall_canvas_rect(c,video.x,video.y,video.w,video.h,t->panel_raised,255);
        rf_ui_text(c,ui_rect(video.x+3,video.y+video.h/2-ui_px(18,scale),video.w-6,ui_px(54,scale)),
            !count?"未选择":s && s->video_requested?(s->video_state==3?"镜头不可用":"连接中"):"镜头不可用",
            t->muted,camera_scale,3);
    }
    r=l->commands;rf_ui_panel(c,r,t,0);
    int command_scale=ui_min(scale,ui_max(1000,r.w*1000/230));
    rf_ui_text(c,ui_rect(r.x+pad,r.y+pad,r.w-pad*2,ui_px(18,scale)),
        r.w<ui_px(200,scale)?"右键移动":"右键移动 · 数字选组",t->muted,command_scale,1);
    ui_label(v,RF_ACTION_RTS_STOP,key,sizeof(key));snprintf(line,sizeof(line),"[%s] 停止移动",key);
    rf_ui_button(c,ui_rts_button(l,0),t,line,command_scale,0,count>0);
    ui_label(v,RF_ACTION_RTS_FOLLOW,key,sizeof(key));snprintf(line,sizeof(line),"[%s] 跟随单位",key);
    rf_ui_button(c,ui_rts_button(l,1),t,line,command_scale,v->rts_follow_player,1);
    ui_label(v,RF_ACTION_COMMAND_MODE,key,sizeof(key));snprintf(line,sizeof(line),"[%s] 返回 FPS",key);
    rf_ui_button(c,ui_rts_button(l,2),t,line,command_scale,0,1);
    if(s && s->hovered>=0 && rf_rts_member_valid(&s->selected[s->hovered],h->game,s->hovered) && count>1) {
        int w=ui_min(ui_px(270,scale),c->width-2*l->margin),height=ui_px(108,scale);
        struct rf_ui_rect tip=ui_rect(ui_clamp(s->pointer_x+14,l->margin,c->width-l->margin-w),
            ui_max(l->margin,l->groups.y-height-l->gap),w,height);
        rf_ui_panel(c,tip,t,1);
        rts_unit_details(c,ui_rect(tip.x+pad,tip.y+pad,tip.w-2*pad,tip.h-pad),&h->game->actors[s->hovered],t,scale,1);
    }
}
