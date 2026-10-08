/* Input/presentation owner. No simulation work is performed by draw callbacks. */
#undef game
#include "rf_tactical_lab.h"
static struct {
    int open, dropdown, hover, px, py, values[8], world;
    uint64_t generation;
    char message[192];
    char seed[11], seed_before_edit[11];
    int seed_edit, seed_replace;
} rf_tac_ui;
#if RF_TACTICAL_INTERACTIVE
static const char *rf_tac_policy_names[]={"简单（机械策略）","普通（协同策略）","旧简单推进","旧机械策略","旧 Utility","旧 Beam"};
static const int rf_tac_solver_ids[]={RF_TAC_M0,RF_TAC_M0,RF_TAC_SIMPLE,RF_TAC_MECHANICAL,RF_TAC_UTILITY,RF_TAC_BEAM};
static const int rf_tac_budget_options[]={128,2048,RF_AI_DEFAULT_BUDGET};
static const char *rf_tac_weapon_names[]={"步枪 / Rifle","冲锋枪 / SMG"};
static const char *rf_tac_mode_names[]={"单发","三发点射","连续射击"};
static const char *rf_tac_target_names[]={"全身战士假人","半身战士假人","探头战士假人"};
static int rf_tac_seed_value(unsigned *out)
{
    unsigned value=0;
    if(!rf_tac_ui.seed[0])return 0;
    for(int i=0;rf_tac_ui.seed[i];++i) {
        unsigned digit=(unsigned)(rf_tac_ui.seed[i]-'0');
        if(digit>9 || value>(4294967295u-digit)/10u)return 0;
        value=value*10u+digit;
    }
    *out=value;return 1;
}
static int rf_tac_seed_check(void)
{
    unsigned seed;
    if(rf_tac_seed_value(&seed))return 1;
    snprintf(rf_tac_ui.message,sizeof(rf_tac_ui.message),"种子须为 0 到 4294967295 的十进制整数（最多 10 位）");
    return 0;
}

static int rf_tac_field_count(int kind){return kind==1?7:5;}
static int rf_tac_option_count(int kind,int field)
{
    static const int arena[]={6,6,3,2,0,2,3},range[]={2,3,9,3,0};
    return kind==1?arena[field]:range[field];
}
static const char *rf_tac_field_name(int kind,int field)
{
    static const char *a[]={"A 方策略","B 方策略","每方人数","双方武器","射击种子","攻守岗位","搜索预算"};
    static const char *r[]={"测试武器","AI 射击模式","AI 标靶距离","目标暴露","随机种子"};
    return kind==1?a[field]:r[field];
}
static void rf_tac_option_name(int kind,int field,int value,char *out,int cap)
{
    if(kind==1) {
        if(field<2)snprintf(out,cap,"%s",rf_tac_policy_names[value]);
        else if(field==2)snprintf(out,cap,"%d 人",value+4);
        else if(field==3)snprintf(out,cap,"%s",rf_tac_weapon_names[value]);
        else if(field==4)snprintf(out,cap,"%s",rf_tac_ui.seed);
        else if(field==5)snprintf(out,cap,"%s",value?"A 防守 / B 进攻":"A 进攻 / B 防守");
        else snprintf(out,cap,"%d 工作单位",rf_tac_budget_options[value]);
    } else {
        if(field==0)snprintf(out,cap,"%s",rf_tac_weapon_names[value]);
        else if(field==1)snprintf(out,cap,"%s",rf_tac_mode_names[value]);
        else if(field==2)snprintf(out,cap,"%d 米",rf_range_distances[value]);
        else if(field==3)snprintf(out,cap,"%s",rf_tac_target_names[value]);
        else snprintf(out,cap,"%s",rf_tac_ui.seed);
    }
}
static void rf_tac_panel(int w,int h,int *x,int *y,int *pw,int *ph)
{*pw=w<1100?w-32:1060;*ph=h<720?h-32:650;*x=(w-*pw)/2;*y=(h-*ph)/2;}
static int rf_tac_hit(int w,int h,int kind,int px,int py)
{
    int x,y,pw,ph;rf_tac_panel(w,h,&x,&y,&pw,&ph);
    int fw=(pw-72)/2,row=48;
    if(rf_tac_ui.dropdown>=0) {
        int n=rf_tac_option_count(kind,rf_tac_ui.dropdown);
        int top=y+104+rf_tac_ui.dropdown*row;
        if(top+n*32>y+ph-70)top=y+ph-70-n*32;
        if(px>=x+24 && px<x+24+fw && py>=top && py<top+n*32)return 100+(py-top)/32;
        return -1;
    }
    for(int i=0;i<rf_tac_field_count(kind);++i)
        if(px>=x+24 && px<x+24+fw && py>=y+86+i*row && py<y+126+i*row)return i;
    for(int i=0;i<7;++i) {
        int bw=(pw-58)/7,bx=x+24+i*(bw+2);
        if(px>=bx && px<bx+bw && py>=y+ph-94 && py<y+ph-54)return 20+i;
    }
    return -1;
}
static int rf_tac_apply_settings(struct rasterfall_session *s)
{
    struct rf_tactical_lab *lab=s->tactical;int *v=rf_tac_ui.values;
    unsigned seed;
    if(!rf_tac_seed_value(&seed))return 0;
    if(lab->kind==1) {
        struct rf_tac_policy policies[2];
        for(int t=0;t<2;++t) {
            rf_tac_policy_default(&policies[t],rf_tac_solver_ids[v[t]]);
            if(v[t]==1){policies[t].algorithm=rf_ai_algorithm_find("normal");
                rf_ai_config_default(policies[t].algorithm,&policies[t].ai);}
            policies[t].budget=rf_tac_budget_options[v[6]];
        }
        lab->seed=seed;
        if(!rf_tac_match_reset(&lab->match,100,lab->seed,v[2]+4,v[3],policies))return 0;
        for(int t=0;t<2;++t)rf_tac_command(&lab->match.world,t,(t==v[5])?RF_TAC_ATTACK:RF_TAC_DEFEND,lab->match.map.objective,lab->match.map.objective_radius);
    } else {
        lab->seed=seed;
        rf_range_reset(&lab->range,v[0],v[1],v[2],v[3],lab->seed);
        if(lab->range.game!=&s->game_state && !rf_range_bind(&lab->range,&s->game_state))return 0;
    }
    lab->accumulator_us=0;lab->pending_fire=lab->pending_reload=0;
    memset(lab->presented_shots,0,sizeof(lab->presented_shots));lab->presented_marks=0;return 1;
}
static int rf_tac_ui_input(struct rf_game_runtime *r,int width,int height,
    int px,int py,int click,int cancel,int interact,const struct toy_window_events *events)
{
    struct rf_tactical_lab *lab=r->session->tactical;
    if(!lab){rf_tac_ui.open=0;return 0;}
    if(rf_tac_ui.world!=(int)r->session->world_id || rf_tac_ui.generation!=r->session->scene_local.world_generation) {
        memset(&rf_tac_ui,0,sizeof(rf_tac_ui));rf_tac_ui.dropdown=-1;rf_tac_ui.hover=-1;
        rf_tac_ui.world=r->session->world_id;rf_tac_ui.generation=r->session->scene_local.world_generation;
        snprintf(rf_tac_ui.seed,sizeof(rf_tac_ui.seed),"%u",lab->seed);
        if(lab->kind==1){rf_tac_ui.values[1]=0;rf_tac_ui.values[6]=2;}
        else {rf_tac_ui.values[1]=2;rf_tac_ui.values[2]=3;}
    }
    const struct toy_game_actor *p=toy_game_local_player_actor_const(&r->session->game_state);
    int terminal_z=lab->kind==1?-14284:-1996;
    int near=p && abs(p->x)<1600 && abs(p->z-terminal_z)<2200;
    if(!rf_tac_ui.open) {
        if(lab->kind==1 && r->rts_active && click && px>=width-310 && px<width-24 && py>=150 && py<150+lab->match.world.squad_size*2*26) {
            lab->selected=(py-150)/26;rasterfall_audio_ui(&r->audio,RF_UI_SOUND_CLICK);return 1;
        }
        if(interact && (near || r->rts_active || lab->player_firing)) {
            rf_tac_ui.open=1;rf_tac_ui.dropdown=-1;rf_tac_ui.hover=-1;
            rasterfall_audio_ui(&r->audio,RF_UI_SOUND_CONFIRM);return 1;
        }
        return 0;
    }
    rf_tac_ui.px=px;rf_tac_ui.py=py;
    if(cancel) {
        if(rf_tac_ui.seed_edit) {
            memcpy(rf_tac_ui.seed,rf_tac_ui.seed_before_edit,sizeof(rf_tac_ui.seed));
            rf_tac_ui.seed_edit=rf_tac_ui.seed_replace=0;
        } else if(rf_tac_ui.dropdown>=0)rf_tac_ui.dropdown=-1;
        else rf_tac_ui.open=0;
        rasterfall_audio_ui(&r->audio,RF_UI_SOUND_EXIT);return 1;
    }
    if(rf_tac_ui.seed_edit)for(int i=0;i<events->key_event_count;++i) {
        const struct toy_key_event *e=&events->key_events[i];
        if(!e->pressed)continue;
        if(e->key==KEY_ENTER) {
            if(rf_tac_seed_check())rf_tac_ui.seed_edit=rf_tac_ui.seed_replace=0;
            break;
        }
        int len=(int)strlen(rf_tac_ui.seed);
        if(e->key==KEY_BACKSPACE) {
            if(rf_tac_ui.seed_replace)rf_tac_ui.seed[0]=0;
            else if(len)rf_tac_ui.seed[len-1]=0;
            rf_tac_ui.seed_replace=0;
        } else if(e->key>=KEY_1 && e->key<=KEY_0) {
            if(rf_tac_ui.seed_replace)len=0;
            rf_tac_ui.seed_replace=0;
            if(len<10) {
                rf_tac_ui.seed[len]=e->key==KEY_0?'0':(char)('1'+e->key-KEY_1);
                rf_tac_ui.seed[len+1]=0;
            } else {
                snprintf(rf_tac_ui.message,sizeof(rf_tac_ui.message),"种子最多 10 位，范围 0 到 4294967295");
                continue;
            }
        } else continue;
        snprintf(rf_tac_ui.message,sizeof(rf_tac_ui.message),"种子范围 0 到 4294967295 / Enter 完成 / Esc 撤销编辑");
    }
    int hit=rf_tac_hit(width,height,lab->kind,px,py);
    if(hit!=rf_tac_ui.hover) {
        if(hit>=0)rasterfall_audio_ui(&r->audio,RF_UI_SOUND_HOVER);
        rf_tac_ui.hover=hit;
    }
    if(!click)return 1;
    if(rf_tac_ui.dropdown>=0) {
        if(hit>=100) {
            int index=rf_tac_ui.dropdown,value=hit-100;
            int changed=rf_tac_ui.values[index]!=value;
            rf_tac_ui.values[index]=value;
            rasterfall_audio_ui(&r->audio,changed?RF_UI_SOUND_CHANGE:RF_UI_SOUND_CLICK);
            snprintf(rf_tac_ui.message,sizeof(rf_tac_ui.message),"配置将在开始新局或重置时应用");
        } else rasterfall_audio_ui(&r->audio,RF_UI_SOUND_CLICK);
        rf_tac_ui.dropdown=-1;rf_tac_ui.hover=-1;return 1;
    }
    if(hit==4) {
        if(!rf_tac_ui.seed_edit)memcpy(rf_tac_ui.seed_before_edit,rf_tac_ui.seed,sizeof(rf_tac_ui.seed));
        rf_tac_ui.seed_edit=rf_tac_ui.seed_replace=1;
        snprintf(rf_tac_ui.message,sizeof(rf_tac_ui.message),"直接输入替换种子 / Backspace 删除 / Enter 完成 / Esc 撤销编辑");
        rasterfall_audio_ui(&r->audio,RF_UI_SOUND_CLICK);return 1;
    }
    rf_tac_ui.seed_edit=rf_tac_ui.seed_replace=0;
    if(hit>=0 && hit<8){rf_tac_ui.dropdown=hit;rasterfall_audio_ui(&r->audio,RF_UI_SOUND_CLICK);return 1;}
    if(hit>=20) {
        int action=hit-20,ok=1;
        if(action==0 || action==2) {
            if(!rf_tac_seed_check())return 1;
            if((lab->match.ready || lab->range.game) && !rf_tactical_lab_export(lab)) {
                snprintf(rf_tac_ui.message,sizeof(rf_tac_ui.message),"统计导出失败，已保留当前对局");
                rasterfall_audio_ui(&r->audio,RF_UI_SOUND_CLICK);
                return 1;
            }
            ok=rf_tac_apply_settings(r->session);
            if(ok) {if(lab->kind==1)lab->match.running=action==0;else lab->range.running=action==0;}
            snprintf(rf_tac_ui.message,sizeof(rf_tac_ui.message),"%s",ok?(action==0?"新局已开始":"已重置，等待开始"):"配置加载失败");
        } else if(action==1) {
            if(!lab->match.ready && !lab->range.game)return 1;
            if(lab->kind==1)lab->match.running=!lab->match.running && !lab->match.world.finished;
            else lab->range.running=!lab->range.running;
        } else if(action==3) {
            if(lab->kind==1) {
                r->rts_active=1;rasterfall_session_set_rts(r->session,1);
                r->rts_camera_x=0;r->rts_camera_z=-3000;r->rts_camera_distance=38000;r->rts_follow_player=0;
            } else {
                r->rts_active=0;rasterfall_session_set_rts(r->session,0);
                /* Fixed firing line makes displayed distances exact. */
                struct toy_game_actor *player=toy_game_local_player_actor(&r->session->game_state);
                player->x=(lab->range.lane-4)*1536;player->z=0;
                player->move_velocity_x=0;player->move_velocity_z=0;
                lab->player_firing=1;
                r->camera.x=player->x;r->camera.z=0;r->camera.sy=0;r->camera.cy=1024;
                r->camera.pitch_sy=0;r->camera.pitch_cy=1024;
            }
            rf_tac_ui.open=0;
        } else if(action==4) {
            ok=rf_tactical_lab_export(lab);
            snprintf(rf_tac_ui.message,sizeof(rf_tac_ui.message),"%s %.150s",ok?"已导出":"导出失败",lab->output);
        } else if(action==5) {
            rf_tac_match_destroy(&lab->match);rf_range_destroy(&lab->range);
            lab->player_firing=0;lab->range.running=0;
            toy_game_set_primitives(&r->session->game_state,r->session->level.primitives,
                r->session->level.primitive_count,r->session->level.room_limit);
            snprintf(rf_tac_ui.message,sizeof(rf_tac_ui.message),"实验组已销毁，可以重新创建");
        } else rf_tac_ui.open=0;
        rasterfall_audio_ui(&r->audio,action==6?RF_UI_SOUND_EXIT:RF_UI_SOUND_CONFIRM);
    }
    return 1;
}
static void rf_tac_ui_draw(struct rasterfall_canvas *c,const struct rf_game_runtime *r)
{
    struct rf_tactical_lab *lab=r->session->tactical;if(!lab)return;
    char line[192];
    if(!rf_tac_ui.open) {
        rasterfall_canvas_rect(c,24,80,560,54,0x101C2C,225);
        snprintf(line,sizeof(line),"%s   E  控制终端 / RTS 中随时打开",lab->kind==1?"AI 策略对抗场":"武器靶场");
        rasterfall_canvas_text(c,36,90,line,0x9FE8C8);
        if(lab->kind==1)snprintf(line,sizeof(line),"%.1fs  %s  A/B 策略 %d/%d",lab->match.world.time_ms/1000.0,
            lab->match.world.finished?"对局结束":lab->match.running?"运行中":"已暂停",lab->match.policies[0].solver,lab->match.policies[1].solver);
        else {int shots=0,hits=0;for(int i=0;i<RF_RANGE_LANES;++i){shots+=lab->range.stats[1][i].shots;hits+=lab->range.stats[1][i].hits;}
            snprintf(line,sizeof(line),"自由试射  发射 %d  命中 %d  弹药 %d / R 换弹 / E 终端",shots,hits,lab->range.weapons[1].ammo);}
        rasterfall_canvas_text(c,36,112,line,0xD6F3F3);
        if(lab->kind==1 && r->rts_active) {
            const struct rf_tac_world *w=&lab->match.world;
            int sx=c->width-310;
            for(int i=0;i<w->squad_size*2;++i) {
                const struct rf_tac_unit *u=&w->units[(i/w->squad_size)*RF_TAC_MAX_SQUAD+i%w->squad_size];
                rasterfall_canvas_rect(c,sx,150+i*26,286,24,i==lab->selected?0x315A68:0x172B39,235);
                snprintf(line,sizeof(line),"%c-%d  HP %.0f  回避 %.0f  %s",'A'+u->team,i%w->squad_size+1,u->hp,u->evasion,u->alive?"":"阵亡");
                rasterfall_canvas_text(c,sx+8,154+i*26,line,u->team?0xF1B48D:0x81DDDC);
            }
        }
        return;
    }
    int x,y,pw,ph;rf_tac_panel(c->width,c->height,&x,&y,&pw,&ph);
    int fw=(pw-72)/2,sx=x+fw+48;
    rasterfall_canvas_rect(c,0,0,c->width,c->height,0x060C15,150);
    rasterfall_canvas_rect(c,x-2,y-2,pw+4,ph+4,0x4AC8CC,255);
    rasterfall_canvas_rect(c,x,y,pw,ph,0x101C2C,250);
    rasterfall_canvas_text(c,x+24,y+22,lab->kind==1?"TACTICAL / AI 策略对抗控制":"RANGE / 武器测试控制",0xD6F3F3);
    rasterfall_canvas_text(c,x+24,y+48,"点击种子直接输入，其他字段展开选项 / Esc 返回 / 更改配置后开始新局",0x88B9C3);
    for(int i=0;i<rf_tac_field_count(lab->kind);++i) {
        int ry=y+86+i*48,active=rf_tac_ui.hover==i || rf_tac_ui.dropdown==i || (i==4 && rf_tac_ui.seed_edit);
        rasterfall_canvas_rect(c,x+24,ry,fw,40,active?0x315A68:0x203543,255);
        if(active)rasterfall_canvas_rect(c,x+24,ry,3,40,0x7DE9D7,255);
        rasterfall_canvas_text(c,x+36,ry+12,rf_tac_field_name(lab->kind,i),0x88B9C3);
        rf_tac_option_name(lab->kind,i,rf_tac_ui.values[i],line,sizeof(line));
        if(i==4 && rf_tac_ui.seed_edit) {
            if(rf_tac_ui.seed_replace)rasterfall_canvas_rect(c,x+178,ry+7,132,26,0x376572,255);
            else snprintf(line,sizeof(line),"%s_",rf_tac_ui.seed);
        }
        rasterfall_canvas_text(c,x+180,ry+12,line,0xE3F8F4);
        if(i!=4)rasterfall_canvas_text(c,x+fw+5,ry+12,"v",0x9FE8C8);
    }
    rasterfall_canvas_text(c,x+36,y+86+rf_tac_field_count(lab->kind)*48,"种子：0 到 4294967295，最多 10 位数字",0x88B9C3);
    rasterfall_canvas_text(c,sx,y+86,"实时结果 / 当前对局",0x9FE8C8);
    if(lab->kind==1) {
        struct rf_tac_world *w=&lab->match.world;
        snprintf(line,sizeof(line),"%.2fs  %s  HASH %08x",w->time_ms/1000.0,w->finished?"结束":lab->match.running?"运行":"暂停",rf_tac_hash(w));
        rasterfall_canvas_text(c,sx,y+118,line,0xD6F3F3);
        for(int t=0;t<2;++t) {
            int shots=0,hits=0,alive=0;float damage=0;
            for(int i=0;i<w->squad_size;++i){const struct rf_tac_unit *u=&w->units[t*RF_TAC_MAX_SQUAD+i];shots+=u->shots;hits+=u->hits;alive+=u->alive;damage+=u->damage;}
            snprintf(line,sizeof(line),"%c 方  存活 %d/%d  命中 %d/%d",'A'+t,alive,w->squad_size,hits,shots);
            rasterfall_canvas_text(c,sx,y+158+t*100,line,t?0xF1B48D:0x81DDDC);
            snprintf(line,sizeof(line),"生命伤害 %.1f  求解工作 %d  预测步 %d",damage,lab->match.plans[t].evaluations,lab->match.plans[t].prediction_steps);
            rasterfall_canvas_text(c,sx,y+184+t*100,line,0x88B9C3);
        }
        if(w->finished){snprintf(line,sizeof(line),"胜方 %s  /  %s",w->winner==0?"A":w->winner==1?"B":"平局",w->orders[0].captured||w->orders[1].captured?"占领完成":"歼灭或时限");rasterfall_canvas_text(c,sx,y+375,line,0x9FE8C8);}
        rasterfall_canvas_text(c,sx,y+410,"地图：种子 100 单层据点 / 双方同一时刻决策",0x88B9C3);
    } else {
        for(int s=0;s<2;++s) {
            struct rf_range_stats *v=&lab->range.stats[s][lab->range.lane];
            snprintf(line,sizeof(line),"%s / %dm  命中 %d / %d",s?"玩家":"AI",rf_range_distances[lab->range.lane],v->hits,v->shots);
            rasterfall_canvas_text(c,sx,y+122+s*96,line,s?0x81DDDC:0xF1B48D);
            snprintf(line,sizeof(line),"命中率 %.1f%%  头部 %d  击杀 %d",v->shots?100.0*v->hits/v->shots:0,v->heads,v->kills);
            rasterfall_canvas_text(c,sx,y+148+s*96,line,0xD6F3F3);
            if(v->kills)snprintf(line,sizeof(line),"伤害 %.1f  DPS %.1f  完成TTK %.2fs",v->damage,
                v->elapsed_ms?v->damage*1000.0/v->elapsed_ms:0,v->ttk_total_ms/1000.0/v->kills);
            else snprintf(line,sizeof(line),"伤害 %.1f  DPS %.1f  TTK --",v->damage,
                v->elapsed_ms?v->damage*1000.0/v->elapsed_ms:0);
            rasterfall_canvas_text(c,sx,y+173+s*96,line,0x88B9C3);
        }
        rasterfall_canvas_text(c,sx,y+352,"弹着分布 / 当前距离",0x9FE8C8);
        rasterfall_canvas_rect(c,sx,y+378,fw,100,0x172B39,255);
        int count=lab->range.mark_count<RF_RANGE_MARKS?lab->range.mark_count:RF_RANGE_MARKS;
        for(int i=0;i<count;++i){const struct rf_range_mark *m=&lab->range.marks[i];if(m->lane!=lab->range.lane)continue;
            int mx=sx+fw/2+(int)(m->x*80),my=y+428-(int)(m->y*48);
            if(mx>sx && mx<sx+fw-3 && my>y+379 && my<y+475)rasterfall_canvas_rect(c,mx,my,3,3,m->shooter?0x81DDDC:0xF1B48D,255);}
    }
    const char *buttons[]={"创建并开始","暂停 / 继续","重建","观战","导出结果","销毁","退出"};
    for(int i=0;i<7;++i){int bw=(pw-58)/7,bx=x+24+i*(bw+2);rasterfall_canvas_rect(c,bx,y+ph-94,bw,40,rf_tac_ui.hover==20+i?0x376572:0x254553,255);
        rasterfall_canvas_text(c,bx+12,y+ph-81,(i==3 && lab->kind==2)?"玩家试射":buttons[i],0xE3F8F4);}
    rasterfall_canvas_text(c,x+24,y+ph-34,rf_tac_ui.message,0x9FE8C8);
    if(rf_tac_ui.dropdown>=0) {
        int f=rf_tac_ui.dropdown,n=rf_tac_option_count(lab->kind,f),top=y+104+f*48;
        if(top+n*32>y+ph-70)top=y+ph-70-n*32;
        rasterfall_canvas_rect(c,x+22,top-2,fw+4,n*32+4,0x65B9BE,255);
        for(int i=0;i<n;++i){int selected=rf_tac_ui.values[f]==i;rf_tac_option_name(lab->kind,f,i,line,sizeof(line));
            rasterfall_canvas_rect(c,x+24,top+i*32,fw,32,rf_tac_ui.hover==100+i?0x3B6974:0x1C3343,255);
            rasterfall_canvas_text(c,x+34,top+i*32+8,selected?">":" ",0x9FE8C8);
            rasterfall_canvas_text(c,x+55,top+i*32+8,line,selected?0x9FE8C8:0xE3F8F4);}
    }
    rasterfall_canvas_rect(c,rf_tac_ui.px-5,rf_tac_ui.py,11,1,0xFFFFFF,255);
    rasterfall_canvas_rect(c,rf_tac_ui.px,rf_tac_ui.py-5,1,11,0xFFFFFF,255);
}

#else
static int rf_tac_ui_input(struct rf_game_runtime *r,int w,int h,int x,int y,int click,int cancel,int interact,const struct toy_window_events *events)
{(void)r;(void)w;(void)h;(void)x;(void)y;(void)click;(void)cancel;(void)interact;(void)events;return 0;}
static void rf_tac_ui_draw(struct rasterfall_canvas *c,const struct rf_game_runtime *r){(void)c;(void)r;}
#endif
#define game (session.game_state)
