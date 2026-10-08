/* Grid terminal draft state. Session owns all spawned identities. */
static struct { int type,count,side; } rf_grid_ui={0,1,0};
static const char *rf_grid_types[]={"步枪枪手 / 简单 AI","冲锋枪手 / 简单 AI","普通追击者","重型追击者","快速追击者","Smoker / 拖拽者","Charger / 冲锋者","Tank / 重装体"};
static const int rf_grid_type_ids[]={RF_GRID_GUNNER_RIFLE,RF_GRID_GUNNER_SMG,0,1,2,3,4,5};
static const char *rf_grid_sides[]={"四周边缘","北侧","东侧","南侧","西侧"};
static const int rf_grid_counts[]={1,4,8,16,24};

static void rf_grid_camera(const struct rasterfall_session *s,struct camera *c)
{
    const struct rf_map_runtime_region *area=rf_map_runtime_find_region(&s->map_ops.runtime,"grid_lab_area");
    if(!area)return;
    c->x=(area->bounds.min_x+area->bounds.max_x)/2;
    c->z=area->bounds.max_z+4000;c->y=23000;
    c->sy=0;c->cy=-1024;c->pitch_sy=-772;c->pitch_cy=672;
}

static int rf_grid_hit(int w,int h,int px,int py)
{
    int x,y,pw,ph;rf_exp_panel(w,h,&x,&y,&pw,&ph);
    int cw=(pw-72)/4;
    for(int i=0;i<8;++i) {
        int bx=x+24+(i%4)*(cw+8),by=y+104+(i/4)*58;
        if(px>=bx && px<bx+cw && py>=by && py<by+48)return i;
    }
    int bw=(pw-80)/5;
    for(int i=0;i<5;++i) {
        int bx=x+24+i*(bw+8);
        if(px>=bx && px<bx+bw && py>=y+266 && py<y+306)return 10+i;
        if(px>=bx && px<bx+bw && py>=y+352 && py<y+392)return 20+i;
    }
    bw=(pw-64)/3;
    for(int i=0;i<3;++i) {
        int bx=x+24+i*(bw+8);
        if(px>=bx && px<bx+bw && py>=y+ph-94 && py<y+ph-54)return 30+i;
    }
    return -1;
}

static int rf_grid_input(struct rf_game_runtime *r,int w,int h,int px,int py,int click,int cancel)
{
    rf_exp_ui.px=px;rf_exp_ui.py=py;
    if(cancel){rf_exp_ui.open=0;rasterfall_audio_ui(&r->audio,RF_UI_SOUND_EXIT);return 1;}
    int hit=rf_grid_hit(w,h,px,py);
    if(hit!=rf_exp_ui.hover) {
        if(hit>=0)rasterfall_audio_ui(&r->audio,RF_UI_SOUND_HOVER);
        rf_exp_ui.hover=hit;
    }
    if(!click || hit<0)return 1;
    rasterfall_audio_ui(&r->audio,RF_UI_SOUND_CLICK);
    if(hit<8)rf_grid_ui.type=hit;
    else if(hit<15 && hit>=10)rf_grid_ui.count=hit-10;
    else if(hit<25 && hit>=20)rf_grid_ui.side=hit-20;
    else if(hit==30) {
        int requested=rf_grid_counts[rf_grid_ui.count];
        int made=rasterfall_grid_spawn(r->session,rf_grid_type_ids[rf_grid_ui.type],requested,rf_grid_ui.side);
        if(made<0)snprintf(rf_exp_ui.message,sizeof(rf_exp_ui.message),"无法召唤：当前场地或游戏状态不可用");
        else snprintf(rf_exp_ui.message,sizeof(rf_exp_ui.message),"已召唤 %d / %d；存活 %d。空位不足或容量已满时停止生成。",
            made,requested,rasterfall_grid_alive(r->session));
    } else if(hit==31) {
        int result=rasterfall_experiment_replace(r->session,RF_EXPERIMENT_GRID_GROUP,NULL,0,NULL);
        snprintf(rf_exp_ui.message,sizeof(rf_exp_ui.message),"%s",result<0?"清除失败，请重试":"已清除本终端召唤的敌人");
    } else if(hit==32)rf_exp_ui.open=0;
    return 1;
}

static void rf_grid_button(struct rasterfall_canvas *c,int x,int y,int w,int h,
                           int id,int selected,const char *text)
{
    unsigned color=selected?0x326C6A:rf_exp_ui.hover==id?0x304E63:0x203345;
    rasterfall_canvas_rect(c,x,y,w,h,color,255);
    if(selected)rasterfall_canvas_rect(c,x,y,3,h,0x81E5C4,255);
    rasterfall_canvas_text(c,x+12,y+(h-16)/2,text,selected?0xC5FFEA:0xDAE8EE);
}

static void rf_grid_draw(struct rasterfall_canvas *c,const struct rf_game_runtime *r)
{
    int x,y,pw,ph;rf_exp_panel(c->width,c->height,&x,&y,&pw,&ph);
    char text[128];
    rasterfall_canvas_rect(c,0,0,c->width,c->height,0x060C15,165);
    rasterfall_canvas_rect(c,x-1,y-1,pw+2,ph+2,0x5BA69A,255);
    rasterfall_canvas_rect(c,x,y,pw,ph,0x101C2A,255);
    rasterfall_canvas_rect(c,x,y,pw,4,0x81E5C4,255);
    rasterfall_canvas_text(c,x+24,y+22,"GRID LAB / 网格玩法实验场",0xD9FFF0);
    snprintf(text,sizeof(text),"1 格 = 1 米    /    本场存活 %d / %d    /    Esc 返回",
        rasterfall_grid_alive(r->session),RF_EXPERIMENT_ACTORS);
    rasterfall_canvas_text(c,x+24,y+50,text,0x9BB8C3);
    rasterfall_canvas_text(c,x+24,y+80,"01 / 选择敌人",0x81E5C4);
    int cw=(pw-72)/4;
    for(int i=0;i<8;++i)rf_grid_button(c,x+24+(i%4)*(cw+8),y+104+(i/4)*58,cw,48,
        i,rf_grid_ui.type==i,rf_grid_types[i]);
    rasterfall_canvas_text(c,x+24,y+240,"02 / 每次召唤数量",0x81E5C4);
    int bw=(pw-80)/5;
    for(int i=0;i<5;++i) {
        snprintf(text,sizeof(text),"%d 名",rf_grid_counts[i]);
        rf_grid_button(c,x+24+i*(bw+8),y+266,bw,40,10+i,rf_grid_ui.count==i,text);
    }
    rasterfall_canvas_text(c,x+24,y+326,"03 / 出生边缘",0x81E5C4);
    for(int i=0;i<5;++i)rf_grid_button(c,x+24+i*(bw+8),y+352,bw,40,
        20+i,rf_grid_ui.side==i,rf_grid_sides[i]);
    rasterfall_canvas_text(c,x+24,y+414,"枪手会接近玩家、瞄准射击并换弹；感染体使用各自攻击。",0x9BB8C3);
    rasterfall_canvas_text(c,x+24,y+440,"可以连续追加；清除仅影响本终端召唤的敌人。",0x9BB8C3);
    const char *buttons[]={"召唤敌人","清除本场敌人","返回场地"};
    bw=(pw-64)/3;
    for(int i=0;i<3;++i)rf_grid_button(c,x+24+i*(bw+8),y+ph-94,bw,40,30+i,i==0,buttons[i]);
    rasterfall_canvas_text(c,x+24,y+ph-34,rf_exp_ui.message,0xA6E6C7);
    rasterfall_canvas_rect(c,rf_exp_ui.px-5,rf_exp_ui.py,11,1,0xFFFFFF,255);
    rasterfall_canvas_rect(c,rf_exp_ui.px,rf_exp_ui.py-5,1,11,0xFFFFFF,255);
}
