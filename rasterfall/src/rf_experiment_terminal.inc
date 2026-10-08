/* Draft settings are UI-owned; applied groups and their entities are session-owned. */
static struct {
    int open,dropdown,hover,px,py,preset,animation,mode,edit,replace,category,gpu,scroll;
    char time[6],before[6],message[192];
    struct rasterfall_session *session;
} rf_exp_ui={.dropdown=-1,.hover=-1,.time="0"};
static const char *rf_exp_presets[]={"Block 动作组","职业装备组","三种身体对照","武器 3 × 5","RF-C01 动作组","感染体家族","真实 AI 待机","真实 AI 往返","真实 AI 实弹射击"};
static const char *rf_exp_actions[]={"待机","原地行走","射击","换弹","受击","倒下","复苏","推搡","近战","投掷"};
static const int rf_exp_animation_ids[]={TOY_GAME_ANIM_IDLE,TOY_GAME_ANIM_MOVE,TOY_GAME_ANIM_FIRE,
    TOY_GAME_ANIM_RELOAD,TOY_GAME_ANIM_HIT,TOY_GAME_ANIM_DEATH,TOY_GAME_ANIM_REVIVE,
    TOY_GAME_ANIM_SHOVE,TOY_GAME_ANIM_MELEE,TOY_GAME_ANIM_THROW};
static const char *rf_exp_modes[]={"仅播放指定动画","冻结在采样时刻","正常 AI"};
static int rf_exp_performance_action(struct rf_game_runtime *runtime,int action);
static int rf_exp_combat_action(struct rf_game_runtime *runtime,int action);
static const char *rf_exp_combat_name(int preset);
static void rf_exp_sync(const struct rasterfall_session *s)
{
    const struct rasterfall_experiments *owner=s->experiments;
    rf_labs.paused[RF_LAB_LIGHTING]=owner && owner->groups[1].paused;
    rf_labs.paused[RF_LAB_ELECTRONICS]=owner && owner->groups[2].paused;
    rf_labs.requested[RF_LAB_LIGHTING]=owner && owner->groups[1].active &&
        rf_map_runtime_find_render(&owner->groups[1].map,"rf_light_base_0_0")!=NULL;
    rf_labs.requested[RF_LAB_ELECTRONICS]=owner && owner->groups[2].active &&
        rf_map_runtime_find_object(&owner->groups[2].map,"rf_product_cpu")!=NULL;
    static const char *channels[]={"actor_actions_lab","rf_light_lab","rf_electronics_lab"};
    for(int i=0;i<3;++i) {
        char channel[64],text[160];
        const struct rf_experiment_group *group=owner?&owner->groups[i]:NULL;
        const char *status=group && group->active?(group->paused?"PAUSED":"CREATED"):"EMPTY";
        snprintf(text,sizeof(text),"EXPERIMENT / %s / E CONTROL",status);
        rasterfall_render_terminal_set(channels[i],text);
        snprintf(channel,sizeof(channel),"%s_computer",channels[i]);
        snprintf(text,sizeof(text),"\033MRF / EXPERIMENT\n\033A%s\n\033F[E] CONTROL\n\033MCREATE / PAUSE / DESTROY",status);
        rasterfall_render_terminal_set(channel,text);
    }
}
static int rf_exp_character_lab(int lab)
{
    return lab==RF_LAB_INFECTED_POSES || lab==RF_LAB_INFECTED_WALK ||
        lab==RF_LAB_ACTOR_ACTIONS || lab==RF_LAB_ACTOR_WALK || lab==RF_LAB_MODEL ||
        lab==RF_LAB_RIFLE_CYCLE || lab==RF_LAB_WEAPON_CYCLE || lab==RF_LAB_IDLE_RIFLE;
}
static int rf_exp_create(struct rasterfall_session *s)
{
    if(rf_exp_ui.category) {
        const char *path=rf_exp_ui.category==1?(rf_exp_ui.preset?"rasterfall/assets/maps/experiment_atmosphere.map":
            "rasterfall/assets/maps/experiment_material.map"):(rf_exp_ui.preset?"rasterfall/assets/maps/experiment_weaver.map":
            "rasterfall/assets/maps/experiment_electronics.map");
        int result=rasterfall_experiment_replace(s,rf_exp_ui.category,NULL,0,path);
        if(!result) {
            if(rf_exp_ui.category==1)rf_labs.requested[RF_LAB_LIGHTING]=rf_exp_ui.preset==0;
            else rf_labs.requested[RF_LAB_ELECTRONICS]=rf_exp_ui.preset==0;
        }
        return result;
    }
    const struct rf_map_runtime_region *area=rf_map_runtime_find_region(&s->map_ops.runtime,"actor_actions_lab_area");
    struct rf_experiment_actor_spec specs[RF_EXPERIMENT_ACTORS]={0};
    static const int bodies[]={RASTERFALL_CHARACTER_NONE,RASTERFALL_CHARACTER_RF_RIFLEMAN,RASTERFALL_CHARACTER_RF_C01};
    static const int equipment[]={RASTERFALL_CHARACTER_RF_RIFLEMAN,RASTERFALL_CHARACTER_SQUAD_A_MEDIC,
        RASTERFALL_CHARACTER_SQUAD_A_ENGINEER,RASTERFALL_CHARACTER_SQUAD_A_RECON,
        RASTERFALL_CHARACTER_SQUAD_B_BREACHER,RASTERFALL_CHARACTER_SQUAD_B_HEAVY,
        RASTERFALL_CHARACTER_GUNNER,RASTERFALL_CHARACTER_GUNNER_ELITE};
    static const int counts[]={6,8,3,15,6,12,4,4,5},modes[]={1,2,0};
    int count=counts[rf_exp_ui.preset],time=atoi(rf_exp_ui.time);
    if(!area || !rf_exp_ui.time[0] || time>60000)return -1;
    if(rf_exp_ui.preset>=2 && rf_exp_ui.preset<=4 && !rf_exp_ui.gpu) {
        snprintf(rf_exp_ui.message,sizeof(rf_exp_ui.message),"RF-C01 需要 GPU 场景渲染，请选择 Block 或职业装备组");return -1;
    }
    if(rf_exp_ui.preset>=2 && rf_exp_ui.preset<=4 && rf_exp_ui.animation>2) {
        snprintf(rf_exp_ui.message,sizeof(rf_exp_ui.message),"RF-C01 当前支持待机、行走与射击；其他动作请选择 Block 或职业装备组");
        return -1;
    }
    if(rf_exp_ui.preset==5 && rf_exp_ui.animation>1) {
        snprintf(rf_exp_ui.message,sizeof(rf_exp_ui.message),"感染体指定动画支持待机与行走；实战行为请选择正常 AI");return -1;
    }
    for(int i=0;i<count;++i) {
        struct rf_experiment_actor_spec *a=&specs[i];
        a->character=rf_exp_ui.preset==0?RASTERFALL_CHARACTER_NONE:
            rf_exp_ui.preset==1?equipment[i]:rf_exp_ui.preset==2?bodies[i]:
            rf_exp_ui.preset==3?bodies[i/5]:RASTERFALL_CHARACTER_RF_C01;
        a->weapon=rf_exp_ui.preset==3?TOY_GAME_WEAPON_PISTOL+i%5:TOY_GAME_WEAPON_AK;
        a->animation=rf_exp_animation_ids[rf_exp_ui.animation];a->mode=modes[rf_exp_ui.mode];
        a->time_ms=time;a->x=area->origin_x-3600+(i%5)*1800;
        a->z=area->origin_z+2400-(i/5)*2100;a->cy=1024;
        if(rf_exp_ui.preset==5)a->enemy_kind=1+i%TOY_GAME_ENEMY_TYPE_COUNT;
        if(rf_exp_ui.preset>=6) {
            a->character=i==0?RASTERFALL_CHARACTER_NONE:equipment[i-1];a->mode=0;
            a->x=area->origin_x-2700+i*1800;a->z=area->origin_z;
            if(i==4){a->target=1;a->x=area->origin_x;a->z+=2800;}
        }
    }
    int result=rasterfall_experiment_replace(s,0,specs,count,NULL);
    if(!result)s->experiments->groups[0].behavior=rf_exp_ui.preset>=6?rf_exp_ui.preset-5:0;
    return result;
}
static void rf_exp_panel(int w,int h,int *x,int *y,int *pw,int *ph)
{*pw=w<1100?w-32:1060;*ph=h<650?h-32:590;*x=(w-*pw)/2;*y=(h-*ph)/2;}
static void rf_exp_diagnostic(struct rasterfall_session *s,const char *view)
{
    if(!view || s->world_id!=RASTERFALL_WORLD_OUTPOST)return;
    int all=getenv("RF_LABS_ALL") && getenv("RF_LABS_ALL")[0]=='1';
    for(int i=0;i<RF_LAB_COUNT;++i)if(rf_labs.requested[i] || (all && i==RF_LAB_LIGHTING)) {
        rf_exp_ui.category=i==RF_LAB_LIGHTING?1:i==RF_LAB_ELECTRONICS?2:0;
        rf_exp_ui.preset=rf_exp_ui.category?0:i==RF_LAB_MODEL?4:i==RF_LAB_WEAPON_CYCLE?3:
            i==RF_LAB_RIFLE_CYCLE?2:i==RF_LAB_IDLE_RIFLE?6:(i==RF_LAB_INFECTED_POSES || i==RF_LAB_INFECTED_WALK)?5:1;
        rf_exp_ui.animation=i==RF_LAB_ACTOR_WALK || i==RF_LAB_INFECTED_WALK?1:0;
        rf_exp_ui.mode=1;
        if(rf_exp_create(s)<0)__fprintf(2,"EXPERIMENT diagnostic create failed: %s\n",view);
    }
    if(!strncmp(view,"weaver",6) || !strncmp(view,"mesh-weaver",11)) {
        rf_exp_ui.category=2;rf_exp_ui.preset=1;rf_exp_create(s);
    }
    if(!strncmp(view,"atmosphere",10)) {
        rf_exp_ui.category=1;rf_exp_ui.preset=1;rf_exp_create(s);
    }
    if(getenv("RF_EXPERIMENT_UI")) {
        rf_exp_ui.open=1;rf_exp_ui.session=s;rf_exp_ui.category=0;
        rf_exp_ui.preset=1;rf_exp_ui.dropdown=-1;
    }
}
static int rf_exp_options(int field){return field==0?(rf_exp_ui.category==4?21:rf_exp_ui.category==3?6:rf_exp_ui.category?2:9):field==1?10:3;}
static const char *rf_exp_option(int field,int value)
{
    if(field==0 && rf_exp_ui.category==4)return rf_exp_combat_name(value);
    if(field==0 && rf_exp_ui.category==1)return value?"建筑与天空光照":"材质与动态灯";
    if(field==0 && rf_exp_ui.category==2)return value?"网格编织机与供电":"RF 电子组件与整机";
    if(field==0 && rf_exp_ui.category==3) {
        static const char *names[]={"空场基线","64 名感染体","24 个组件","感染体与组件","真实战斗","枪手混战"};
        return names[value];
    }
    return field==0?rf_exp_presets[value]:field==1?rf_exp_actions[value]:rf_exp_modes[value];
}
static int rf_exp_hit(int w,int h,int px,int py)
{
    int x,y,pw,ph;rf_exp_panel(w,h,&x,&y,&pw,&ph);int fw=(pw-72)/2;
    if(rf_exp_ui.dropdown>=0) {
        int n=rf_exp_options(rf_exp_ui.dropdown),top=y+104+rf_exp_ui.dropdown*48;
        if(n>8)n=8;
        if(top+n*32>y+ph-110)top=y+ph-110-n*32;
        return px>=x+24 && px<x+24+fw && py>=top && py<top+n*32?100+rf_exp_ui.scroll+(py-top)/32:-1;
    }
    for(int i=0;i<(rf_exp_ui.category?1:4);++i)if(px>=x+24 && px<x+24+fw && py>=y+86+i*48 && py<y+126+i*48)return i;
    int buttons=rf_exp_ui.category==4?4:rf_exp_ui.category==3?3:5;
    for(int i=0;i<buttons;++i) {
        int bw=(pw-56)/buttons,bx=x+24+i*(bw+2);
        if(px>=bx && px<bx+bw && py>=y+ph-94 && py<y+ph-54)return 20+i;
    }
    return -1;
}
static int rf_exp_input(struct rf_game_runtime *r,int w,int h,int px,int py,int click,int cancel,
                        const struct toy_window_events *events)
{
    if(!rf_exp_ui.open)return 0;
    if(rf_exp_ui.session!=r->session || r->session->world_id!=RASTERFALL_WORLD_OUTPOST) {
        rf_exp_ui.open=0;return 1;
    }
    rf_exp_ui.px=px;rf_exp_ui.py=py;
    if(cancel) {
        if(rf_exp_ui.edit){strcpy(rf_exp_ui.time,rf_exp_ui.before);rf_exp_ui.edit=0;}
        else if(rf_exp_ui.dropdown>=0)rf_exp_ui.dropdown=-1;
        else rf_exp_ui.open=0;
        rasterfall_audio_ui(&r->audio,RF_UI_SOUND_EXIT);return 1;
    }
    if(rf_exp_ui.edit)for(int i=0;i<events->key_event_count;++i) {
        const struct toy_key_event *e=&events->key_events[i];if(!e->pressed)continue;
        int len=(int)strlen(rf_exp_ui.time);
        if(e->key==KEY_ENTER){rf_exp_ui.edit=0;break;}
        if(e->key==KEY_BACKSPACE) {
            if(rf_exp_ui.replace)rf_exp_ui.time[0]=0;
            else if(len)rf_exp_ui.time[len-1]=0;
            rf_exp_ui.replace=0;
        } else if(e->key>=KEY_1 && e->key<=KEY_0) {
            if(rf_exp_ui.replace)len=0;
            rf_exp_ui.replace=0;
            if(len<5){rf_exp_ui.time[len]=e->key==KEY_0?'0':(char)('1'+e->key-KEY_1);rf_exp_ui.time[len+1]=0;}
        }
    }
    if(rf_exp_ui.dropdown>=0 && events->wheel_y) {
        int max=rf_exp_options(rf_exp_ui.dropdown)-8;if(max<0)max=0;
        rf_exp_ui.scroll-=events->wheel_y;
        if(rf_exp_ui.scroll<0)rf_exp_ui.scroll=0;
        if(rf_exp_ui.scroll>max)rf_exp_ui.scroll=max;
    }
    int hit=rf_exp_hit(w,h,px,py);
    if(hit!=rf_exp_ui.hover){if(hit>=0)rasterfall_audio_ui(&r->audio,RF_UI_SOUND_HOVER);rf_exp_ui.hover=hit;}
    if(!click)return 1;
    rasterfall_audio_ui(&r->audio,RF_UI_SOUND_CLICK);
    if(rf_exp_ui.dropdown>=0) {
        if(hit>=100) {
            int *value=rf_exp_ui.dropdown==0?&rf_exp_ui.preset:rf_exp_ui.dropdown==1?&rf_exp_ui.animation:&rf_exp_ui.mode;
            *value=hit-100;rasterfall_audio_ui(&r->audio,RF_UI_SOUND_CHANGE);
        }
        rf_exp_ui.dropdown=-1;return 1;
    }
    if(hit==3){strcpy(rf_exp_ui.before,rf_exp_ui.time);rf_exp_ui.edit=rf_exp_ui.replace=1;}
    else if(hit>=0 && hit<3){rf_exp_ui.edit=0;rf_exp_ui.dropdown=hit;rf_exp_ui.scroll=0;}
    else if(hit>=20) {
        struct rasterfall_session *s=r->session;int result=0;
        rf_exp_ui.edit=0;rf_exp_ui.message[0]=0;
        if(rf_exp_ui.category==4)result=rf_exp_combat_action(r,hit-20);
        else if(rf_exp_ui.category==3)result=rf_exp_performance_action(r,hit-20);
        else if(hit==20 || hit==22)result=rf_exp_create(s);
        else if(hit==21)result=rasterfall_experiment_pause(s,rf_exp_ui.category,s->experiments?!s->experiments->groups[rf_exp_ui.category].paused:1);
        else if(hit==23) {
            result=rasterfall_experiment_replace(s,rf_exp_ui.category,NULL,0,NULL);
            if(!result && rf_exp_ui.category)rf_labs.requested[rf_exp_ui.category==1?RF_LAB_LIGHTING:RF_LAB_ELECTRONICS]=0;
        }
        else rf_exp_ui.open=0;
        if(!rf_exp_ui.message[0])snprintf(rf_exp_ui.message,sizeof(rf_exp_ui.message),"%s",
            result<0?"操作未完成：检查参数与可用实例容量；原实验组保留":"操作完成");
    }
    return 1;
}
static void rf_exp_draw(struct rasterfall_canvas *c,const struct rf_game_runtime *r)
{
    int x,y,pw,ph;rf_exp_panel(c->width,c->height,&x,&y,&pw,&ph);
    int fw=(pw-72)/2;char line[160];
    const struct rf_experiment_group *group=r->session->experiments && rf_exp_ui.category<3?&r->session->experiments->groups[rf_exp_ui.category]:NULL;
    rasterfall_canvas_rect(c,0,0,c->width,c->height,0x060C15,150);
    rasterfall_canvas_rect(c,x-2,y-2,pw+4,ph+4,0x4AC8CC,255);
    rasterfall_canvas_rect(c,x,y,pw,ph,0x101C2C,250);
    rasterfall_canvas_text(c,x+24,y+22,rf_exp_ui.category==1?"EXPERIMENT / 光照与材质":
        rf_exp_ui.category==2?"EXPERIMENT / 电子与制造":rf_exp_ui.category==4?"EXPERIMENT / 战斗负载":rf_exp_ui.category==3?"EXPERIMENT / 性能":"EXPERIMENT / 角色与动画",0xD6F3F3);
    rasterfall_canvas_text(c,x+24,y+48,"配置在创建或重建时应用 / Esc 返回",0x88B9C3);
    const char *names[]={"实验组","指定动画","运行方式","采样毫秒"};
    int values[]={rf_exp_ui.preset,rf_exp_ui.animation,rf_exp_ui.mode};
    for(int i=0;i<(rf_exp_ui.category?1:4);++i) {
        int ry=y+86+i*48,active=rf_exp_ui.hover==i || rf_exp_ui.dropdown==i || (i==3 && rf_exp_ui.edit);
        rasterfall_canvas_rect(c,x+24,ry,fw,40,active?0x315A68:0x203543,255);
        rasterfall_canvas_text(c,x+36,ry+12,names[i],0x88B9C3);
        rasterfall_canvas_text(c,x+160,ry+12,i==3?rf_exp_ui.time:rf_exp_option(i,values[i]),0xE3F8F4);
    }
    if(!rf_exp_ui.category)rasterfall_canvas_text(c,x+36,y+292,"采样范围 0 到 60000 毫秒，点击直接输入",0x88B9C3);
    if(rf_exp_ui.category==4)rasterfall_canvas_text(c,x+36,y+292,"展开后滚轮浏览全部预设",0x88B9C3);
    int sx=x+fw+48;
    rasterfall_canvas_text(c,sx,y+86,"场内实验组",0x9FE8C8);
    snprintf(line,sizeof(line),"%s / %d 个角色",group&&group->active?(group->paused?"已暂停":"已创建"):"空场",group?group->count:0);
    if(rf_exp_ui.category)snprintf(line,sizeof(line),"%s",group&&group->active?"已创建":"空场 / 待创建");
    rasterfall_canvas_text(c,sx,y+122,line,0xD6F3F3);
    rasterfall_canvas_text(c,sx,y+166,rf_exp_ui.category?"展品与实体碰撞同步创建和销毁":"角色使用正式模型、武器和身体碰撞",0x88B9C3);
    rasterfall_canvas_text(c,sx,y+200,rf_exp_ui.category==3?"固定负载 10 秒，实战 30 秒；Esc 取消":rf_exp_ui.category==2?"创建机器后可在机器终端控制制造":"冻结保留当前姿态；销毁释放场内实例",0x88B9C3);
    const char *buttons[]={"创建 / 替换","暂停 / 继续","重建","销毁","退出"};
    const char *perf_buttons[]={"开始采样","查看结果","退出"};
    const char *combat_buttons[]={"创建并开始","销毁","查看结果","退出"};
    int button_count=rf_exp_ui.category==4?4:rf_exp_ui.category==3?3:5;
    for(int i=0;i<button_count;++i) {
        int bw=(pw-56)/button_count,bx=x+24+i*(bw+2);
        rasterfall_canvas_rect(c,bx,y+ph-94,bw,40,rf_exp_ui.hover==20+i?0x376572:0x254553,255);
        rasterfall_canvas_text(c,bx+12,y+ph-81,rf_exp_ui.category==4?combat_buttons[i]:rf_exp_ui.category==3?perf_buttons[i]:buttons[i],0xE3F8F4);
    }
    rasterfall_canvas_text(c,x+24,y+ph-34,rf_exp_ui.message,0x9FE8C8);
    if(rf_exp_ui.dropdown>=0) {
        int f=rf_exp_ui.dropdown,n=rf_exp_options(f),top=y+104+f*48;
        if(n>8)n=8;
        if(top+n*32>y+ph-110)top=y+ph-110-n*32;
        rasterfall_canvas_rect(c,x+22,top-2,fw+4,n*32+4,0x65B9BE,255);
        for(int i=0;i<n;++i) {
            rasterfall_canvas_rect(c,x+24,top+i*32,fw,32,rf_exp_ui.hover==100+rf_exp_ui.scroll+i?0x3B6974:0x1C3343,255);
            rasterfall_canvas_text(c,x+38,top+i*32+8,rf_exp_option(f,rf_exp_ui.scroll+i),0xE3F8F4);
        }
    }
    rasterfall_canvas_rect(c,rf_exp_ui.px-5,rf_exp_ui.py,11,1,0xFFFFFF,255);
    rasterfall_canvas_rect(c,rf_exp_ui.px,rf_exp_ui.py-5,1,11,0xFFFFFF,255);
}
