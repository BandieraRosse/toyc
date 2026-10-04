#include "tlibc_everything.h"
#include "rf_player_commands.h"
#include "rf_game_lifecycle.h"
#include "rf_weaver_blueprints_generated.h"

static struct rf_player_result result(int code,const char *message)
{
    struct rf_player_result r={0};r.code=code;
    snprintf(r.message,sizeof(r.message),"%s",message);return r;
}
void rf_player_controls_init(struct rf_player_controls *s)
{
    memset(s,0,sizeof(*s));s->requested_view=-1;s->hints=1;s->crosshair=1;
}
int rf_player_weaver_near(const struct rasterfall_session *s,int x,int z,int facing)
{
    const struct rf_map_runtime_object *m;
    const struct toy_game_actor *p;
    if(!s || !s->game_state.weaver.enabled)return 0;
    p=toy_game_local_player_actor_const(&s->game_state);
    if(!p || !p->active || p->state!=TOY_GAME_ACTOR_ALIVE || p->control_disabled)return 0;
    for(int i=0;i<2;++i) {
        m=rf_map_runtime_find_object(&s->map_ops.runtime,i?"mesh_weaver_rf1":"mesh_weaver");
        if(m) {
            long long dy=(long long)p->ground_y+p->airborne_y-m->y;
            if(dy<-750 || dy>750)continue;
            long long dx=(long long)m->x+(i?130:-292)-x;
            long long dz=(long long)m->z+(i?180:510)-z;
            long long d2=dx*dx+dz*dz, dot=dx*p->sy+dz*p->cy;
            if(d2<=750LL*750 && (!facing || d2<160LL*160 ||
               (dot>0 && dot*dot>=d2*1024LL*1024/4)))return 1;
        }
    }
    return 0;
}
void rf_player_weaver_query(const struct rf_game_runtime *r,struct rf_player_weaver_query *q)
{
    const struct rasterfall_session *s;
    const struct toy_mesh_weaver *w;
    const struct rf_weaver_blueprint *b;
    const struct toy_game_actor *p;
    int index;
    if(!q)return;
    memset(q,0,sizeof(*q));q->estimate_seconds=q->remaining_seconds=-1;q->replace_weapon=-1;
    if(!r || !(s=r->session))return;
    w=&s->game_state.weaver;p=toy_game_local_player_actor_const(&s->game_state);
    index=r->player_controls.selected_blueprint;
    if(index<0 || index>=(int)RF_WEAVER_BLUEPRINT_COUNT)index=0;
    b=&rf_weaver_blueprints[index];q->selected=index;q->weapon=b->geometry.weapon;
    q->world_generation=s->scene_local.world_generation;q->serial=w->job_serial;
    q->available=w->enabled && r->net.mode==RASTERFALL_NET_OFF &&
        rf_map_runtime_find_object(&s->map_ops.runtime,"mesh_weaver")!=NULL;
    q->near=p && rf_player_weaver_near(s,p->x,p->z,0);
    q->phase=w->phase;q->reason=w->pause_reason;q->progress=w->progress;
    q->power_on=w->supply.power_on;q->cpu_on=w->supply.cpu_on;q->x1_on=w->supply.x1_on;
    q->available_kj=w->supply.energy_kj;q->used_kj=w->used_form_kj+w->used_base_kj;
    q->compute=toy_mesh_weaver_compute(w);
    snprintf(q->name,sizeof(q->name),"%s",b->name);snprintf(q->id,sizeof(q->id),"%s",b->id);
    snprintf(q->status,sizeof(q->status),"%s",w->pause_reason?
        toy_mesh_weaver_reason_name(w->pause_reason):toy_mesh_weaver_phase_name(w->phase));
    if(toy_mesh_weaver_cost(&b->geometry,&w->coefficients,&q->cost)==0) {
        q->estimate_seconds=toy_mesh_weaver_estimate(w,&q->cost);
        q->expected_kj=q->cost.form_kj;
        if(q->estimate_seconds>=0)q->expected_kj+=q->estimate_seconds*(w->supply.base_kw+
            (w->supply.cpu_on?w->supply.cpu_kw:0)+(w->supply.cpu_on&&w->supply.x1_on?w->supply.x1_kw:0));
    }
    if(w->phase!=TOY_WEAVER_IDLE && w->phase!=TOY_WEAVER_READY) {
        struct toy_mesh_weaver_cost remain=w->cost;
        remain.work*=1.0-w->progress;remain.form_kj*=1.0-w->progress;
        q->remaining_seconds=toy_mesh_weaver_estimate(w,&remain);
        if(q->remaining_seconds>=0) {
            double phases=(w->coefficients.calibration_ms+w->coefficients.delivery_ms)/1000.0;
            q->remaining_seconds-=phases;
            if(w->phase==TOY_WEAVER_CALIBRATING)q->remaining_seconds+=(w->coefficients.calibration_ms-w->phase_ms)/1000.0;
            if(w->phase==TOY_WEAVER_DELIVERING)q->remaining_seconds=(w->coefficients.delivery_ms-w->phase_ms)/1000.0;
            else q->remaining_seconds+=w->coefficients.delivery_ms/1000.0;
        }
    }
    q->rounds=toy_game_weapon_info(b->geometry.weapon)->mag_size;
    q->can_start=q->available&&q->near&&s->game_state.state==TOY_GAME_PLAYING&&
        w->phase==TOY_WEAVER_IDLE&&b->manufacturable&&b->size_supported&&
        q->cost.storage_bytes<=w->supply.storage_bytes&&q->estimate_seconds>=0&&
        w->supply.power_on&&q->available_kj>0&&q->compute>0&&toy_mesh_weaver_form_power(w)>0;
    q->can_collect=q->available&&q->near&&s->game_state.state==TOY_GAME_PLAYING&&w->phase==TOY_WEAVER_READY;
    /* The weapon slot rules remain owned by Game; report its actual slot. */
    if(p) {
        int weapon=w->phase==TOY_WEAVER_READY?w->blueprint.weapon:b->geometry.weapon;
        int slot=toy_game_weapon_info(weapon)->slot;
        q->replace_weapon=slot>=0 && slot<TOY_GAME_WEAPON_SLOTS?p->slots[slot].weapon:-1;
        if(w->phase==TOY_WEAVER_READY)q->rounds=w->cost.initial_rounds;
    }
}
static int ground_target(const struct rasterfall_session *s,int x,int z)
{
    const struct toy_game_actor *p=toy_game_local_player_actor_const(&s->game_state);
    struct toy_game_ground_query q;
    if(x<-2000000 || x>2000000 || z<-2000000 || z>2000000 || !p)return 0;
    q=toy_game_query_ground(&s->game_state,x,z,RASTERFALL_PLAYER_RADIUS,p->ground_y);
    return q.has_support && !toy_game_position_blocked_at_height(&s->game_state,x,z,
        RASTERFALL_PLAYER_RADIUS,q.support_y);
}
struct rf_player_result rf_player_execute(struct rf_game_runtime *r,
    const struct rf_player_request *a,enum rf_command_permission_level permission)
{
    struct rf_player_result out=result(RF_PLAYER_OK,"操作完成");
    struct rasterfall_session *s;
    struct rf_player_controls *c;
    struct rf_player_weaver_query q;
    const struct toy_game_actor *p;
    int op,reason;
    if(!r || !a || !(s=r->session))return result(RF_PLAYER_UNAVAILABLE,"当前没有可用会话");
    c=&r->player_controls;op=a->operation;
    if(permission<RF_COMMAND_PERMISSION_USER || permission>RF_COMMAND_PERMISSION_SUPER) {
        out=result(RF_PLAYER_PERMISSION,"命令权限无效");goto done;
    }
    p=toy_game_local_player_actor_const(&s->game_state);
    if(op>=RF_PLAYER_WEAVER_SELECT && op<=RF_PLAYER_RTS_STOP) {
        if(r->net.mode!=RASTERFALL_NET_OFF)out=result(RF_PLAYER_PERMISSION,"此操作当前仅支持离线会话");
        else if(a->world_generation!=s->scene_local.world_generation)out=result(RF_PLAYER_STALE,"世界已变化，请刷新后重试");
        else if(s->game_state.state!=TOY_GAME_PLAYING || !p || !p->active ||
            p->state!=TOY_GAME_ACTOR_ALIVE || p->control_disabled)
            out=result(RF_PLAYER_UNAVAILABLE,"角色目前无法执行此操作");
        if(out.code)goto done;
    }
    if(op>=RF_PLAYER_WEAVER_SELECT && op<=RF_PLAYER_WEAVER_COLLECT) {
        rf_player_weaver_query(r,&q);
        if(!q.available)out=result(RF_PLAYER_UNAVAILABLE,"设备不存在或已离线");
        else if(!q.near)out=result(RF_PLAYER_RANGE,"请靠近网格编织机或关联工作站");
        else if((op==RF_PLAYER_WEAVER_START || op==RF_PLAYER_WEAVER_COLLECT) && a->serial!=q.serial)
            out=result(RF_PLAYER_STALE,"制造任务已变化，请刷新后重试");
        if(out.code)goto done;
    }
    if((op==RF_PLAYER_DIALOG_COLLAPSE || op==RF_PLAYER_DIALOG_CLOSE) &&
        (a->session_revision!=r->story.session_revision || a->node_revision!=r->story.node_revision)) {
        out=result(RF_PLAYER_STALE,"会话已变化，请刷新后重试");goto done;
    }
    switch(op) {
    case RF_PLAYER_SET_MODE:
        if(a->value<0||a->value>RF_PLAYER_UI_EXPERIMENT)out=result(RF_PLAYER_INVALID,"模式应为 player / terminal / experiment");
        else {r->player_ui.mode=a->value;c->settings_dirty=1;}break;
    case RF_PLAYER_SET_SCALE:
        if(a->value<75||a->value>175)out=result(RF_PLAYER_INVALID,"界面缩放范围为 75–175%");
        else {r->player_ui.scale_percent=a->value;c->settings_dirty=1;}break;
    case RF_PLAYER_SET_OPACITY:
        if(a->value<100||a->value>255)out=result(RF_PLAYER_INVALID,"透明度范围为 100–255");
        else {r->player_ui.theme.panel_alpha=a->value;c->settings_dirty=1;}break;
    case RF_PLAYER_SET_HINTS: case RF_PLAYER_SET_CROSSHAIR:
        if(a->value<0||a->value>1)out=result(RF_PLAYER_INVALID,"请输入 0 或 1");
        else {if(op==RF_PLAYER_SET_HINTS)c->hints=a->value;else c->crosshair=a->value;c->settings_dirty=1;}break;
    case RF_PLAYER_WEAVER_SELECT:
        if(a->value<0||a->value>=(int)RF_WEAVER_BLUEPRINT_COUNT)out=result(RF_PLAYER_INVALID,"蓝图不存在");
        else {
            c->selected_blueprint=a->value;
            if(rf_weaver_blueprints[a->value].manufacturable && rf_weaver_blueprints[a->value].size_supported)
                rf_story_emit(&r->story,RF_STORY_EVENT_BLUEPRINT_VIEW,"mesh_weaver");
        }break;
    case RF_PLAYER_WEAVER_START: {
        if(c->selected_blueprint<0 || c->selected_blueprint>=(int)RF_WEAVER_BLUEPRINT_COUNT) {
            out=result(RF_PLAYER_INVALID,"蓝图选择已失效，请重新选择");break;
        }
        const struct rf_weaver_blueprint *b=&rf_weaver_blueprints[c->selected_blueprint];
        if(!b->manufacturable||!b->size_supported)out=result(RF_PLAYER_INVALID,"蓝图体积或尺寸尚未满足制造条件");
        else {reason=rasterfall_session_weaver_start(s,&b->geometry);
            if(reason)out=result(reason==TOY_WEAVER_BUSY||reason==TOY_WEAVER_OUTPUT_OCCUPIED?RF_PLAYER_BUSY:RF_PLAYER_RESOURCE,toy_mesh_weaver_reason_name(reason));
            else out=result(RF_PLAYER_OK,"制造已开始，关闭窗口后继续运行");}break;
    }
    case RF_PLAYER_WEAVER_POWER:case RF_PLAYER_WEAVER_CPU:case RF_PLAYER_WEAVER_X1: {
        struct toy_mesh_weaver_supply supply=s->game_state.weaver.supply;
        if(a->value<0||a->value>1) {out=result(RF_PLAYER_INVALID,"请输入 0 或 1");break;}
        if(op==RF_PLAYER_WEAVER_POWER)supply.power_on=a->value;
        if(op==RF_PLAYER_WEAVER_CPU)supply.cpu_on=a->value;
        if(op==RF_PLAYER_WEAVER_X1)supply.x1_on=a->value;
        toy_game_weaver_set_supply(&s->game_state,&supply);break;
    }
    case RF_PLAYER_WEAVER_COLLECT:
        if(!q.can_collect)out=result(RF_PLAYER_UNAVAILABLE,"当前没有可领取成品");
        else if(q.replace_weapon>=0 && !a->confirmed)out=result(RF_PLAYER_CONFIRM_REPLACE,"领取将替换对应武器槽；确认后领取");
        else if(a->confirmed && a->value!=q.replace_weapon)out=result(RF_PLAYER_STALE,"武器槽已变化，请重新确认替换");
        else if(!rasterfall_session_weaver_collect(s))out=result(RF_PLAYER_UNAVAILABLE,"领取失败，请刷新设备状态");
        else out=result(RF_PLAYER_OK,"已领取成品与标准弹匣");
        break;
    case RF_PLAYER_RTS_SELECT:
        if(a->value<-1 || a->value>s->flag_count ||
           (a->value>0 && !s->flags[a->value-1].active))out=result(RF_PLAYER_INVALID,"选择对象已失效");
        else r->rts_selected=a->value;
        break;
    case RF_PLAYER_RTS_MOVE:
        if(!r->rts_active || !s->rts_active || r->rts_selected<0)out=result(RF_PLAYER_INVALID,"请进入 RTS 并选择玩家或旗帜小队");
        else if(!ground_target(s,a->x,a->z))out=result(RF_PLAYER_INVALID,"目标地面不可通行");
        else if(r->rts_selected==0)rasterfall_session_rts_move_player(s,a->x,a->z);
        else if(!rasterfall_session_rts_move_flag(s,r->rts_selected-1,a->x,a->z))out=result(RF_PLAYER_STALE,"小队旗帜已失效");
        if(!out.code)out=result(RF_PLAYER_OK,"移动指令已接收（目标标记并非寻路路径）");
        break;
    case RF_PLAYER_RTS_STOP:
        if(!r->rts_active || !s->rts_active)out=result(RF_PLAYER_INVALID,"请先进入 RTS");
        else if(r->rts_selected==0)s->rts_move_active=0;
        else if(r->rts_selected>0)out=result(RF_PLAYER_UNAVAILABLE,"现有小队由旗帜指挥，请移动旗帜设置集结点");
        else out=result(RF_PLAYER_INVALID,"未选择可停止对象");
        break;
    case RF_PLAYER_RTS_FOLLOW:
        if(a->value<0 || a->value>1)out=result(RF_PLAYER_INVALID,"请输入 0 或 1");
        else r->rts_follow_player=a->value;
        break;
    case RF_PLAYER_VIEW:
        if(r->net.mode!=RASTERFALL_NET_OFF)out=result(RF_PLAYER_PERMISSION,"RTS 当前仅支持离线会话");
        else if(a->value<0 || a->value>1)out=result(RF_PLAYER_INVALID,"视角编号无效");
        else c->requested_view=a->value;
        break;
    case RF_PLAYER_DIALOG_ANSWER:
        if(rf_story_answer(&r->story,s,a->session_revision,a->node_revision,a->value)!=1)
            out=result(RF_PLAYER_STALE,"回答已过期或会话不可用");
        break;
    case RF_PLAYER_DIALOG_COLLAPSE:
        if(a->value<0 || a->value>1)out=result(RF_PLAYER_INVALID,"请输入 0 或 1");
        else rf_story_collapse(&r->story,a->value);
        break;
    case RF_PLAYER_DIALOG_CLOSE:rf_story_close(&r->story,s);r->comms_focus=0;break;
    case RF_PLAYER_STORY_REPLAY:case RF_PLAYER_STORY_RESET:
        if(permission<RF_COMMAND_PERMISSION_ADMIN)out=result(RF_PLAYER_PERMISSION,"重播和重置只对实验模式开放");
        else if((op==RF_PLAYER_STORY_REPLAY?rf_story_replay(&r->story,s,a->value):rf_story_reset(&r->story,s,a->value))!=1)
            out=result(RF_PLAYER_INVALID,"剧情编号无效或当前不可重播");
        break;
    default:out=result(RF_PLAYER_INVALID,"未知操作");break;
    }
done:
    if(!out.code)out.affected=1;
    c->last=out;return out;
}

static void notice(struct rf_player_controls *c,unsigned serial,int kind,const char *text)
{
    for(int i=0;i<c->notice_count;++i)if(c->notices[i].serial==serial && c->notices[i].kind==kind){c->notices[i].count++;c->notice_ms=4500;return;}
    if(c->notice_count<RF_PLAYER_NOTICE_CAP)c->notice_count++;
    for(int i=c->notice_count-1;i>0;--i)c->notices[i]=c->notices[i-1];
    c->notices[0].serial=serial;c->notices[0].kind=kind;c->notices[0].count=1;
    snprintf(c->notices[0].text,sizeof(c->notices[0].text),"%s",text);c->notice_ms=4500;
}
void rf_player_controls_observe(struct rf_game_runtime *r,int dt)
{
    struct rf_player_controls *c=&r->player_controls;
    const struct toy_mesh_weaver *w=&r->session->game_state.weaver;
    if(c->notice_ms>0)c->notice_ms=c->notice_ms>dt?c->notice_ms-dt:0;
    if(c->observed_world!=r->session->scene_local.world_generation) {
        c->observed_world=r->session->scene_local.world_generation;c->observed_phase=w->phase;
        c->observed_job=w->job_serial;c->observed_collected=w->collected_count;c->observed_pause=w->pause_reason;return;
    }
    if(w->phase==TOY_WEAVER_READY && (c->observed_phase!=TOY_WEAVER_READY || c->observed_job!=w->job_serial))
        notice(c,w->job_serial,1,"制造完成：返回网格编织机领取；详情可在终端查询");
    if(w->phase>TOY_WEAVER_IDLE && w->phase<TOY_WEAVER_READY && w->pause_reason && w->pause_reason!=c->observed_pause)
        notice(c,w->job_serial,2,"制造已暂停：检查设备供电、计算单元与储能；已完成进度保留");
    c->observed_pause=w->pause_reason;
    c->observed_phase=w->phase;c->observed_job=w->job_serial;c->observed_collected=w->collected_count;
}
static int integer(const char *s,int *out)
{
    int sign=1,n=0;if(!s || !out)return 0;
    if(*s=='-'){sign=-1;s++;}if(!*s)return 0;
    while(*s) {
        int digit=*s-'0';
        if(digit<0 || digit>9 || n>(2000000000-digit)/10)return 0;
        n=n*10+digit;s++;
    }
    *out=n*sign;return 1;
}
static int settings_space(char c)
{return c==' ' || c=='\t' || c=='\r' || c=='\n';}
static int settings_decode(const char *text,int values[6])
{
    if(strncmp(text,"RFUI",4) || !settings_space(text[4]))return 0;
    text+=4;
    for(int i=0;i<6;++i) {
        char number[24];int n=0;
        while(settings_space(*text))text++;
        while(*text && !settings_space(*text)) {
            if(n>=(int)sizeof(number)-1)return 0;
            number[n++]=*text++;
        }
        number[n]=0;if(!integer(number,&values[i]))return 0;
    }
    while(settings_space(*text))text++;
    return !*text && values[0]==1 && values[1]>=0 && values[1]<=RF_PLAYER_UI_EXPERIMENT &&
        values[2]>=75 && values[2]<=175 && values[3]>=100 && values[3]<=255 &&
        values[4]>=0 && values[4]<=1 && values[5]>=0 && values[5]<=1;
}
int rf_player_settings_load(struct rf_game_runtime *r,const char *path)
{
    char text[192];int values[6],fd,count=0;
    if(!r || !path || !*path)return -1;
    fd=__openat(AT_FDCWD,path,O_RDONLY,0);
#ifdef TOYC_WINDOWS
    if(fd<0 && errno==ENOENT)return 0;
#else
    if(fd==-ENOENT)return 0;
#endif
    if(fd<0)goto failed;
    while(count<(int)sizeof(text)-1) {
        int n=(int)__read(fd,text+count,sizeof(text)-1-count);
        if(n<0){__close(fd);goto failed;}
        if(!n)break;
        count+=n;
    }
    if(__close(fd)<0 || count>=(int)sizeof(text)-1)goto failed;
    text[count]=0;
    if((int)strlen(text)!=count || !settings_decode(text,values))goto failed;
    r->player_ui.mode=values[1];r->player_ui.scale_percent=values[2];
    r->player_ui.theme.panel_alpha=values[3];r->player_controls.hints=values[4];
    r->player_controls.crosshair=values[5];r->player_controls.persistence_error=0;
    r->player_controls.settings_dirty=0;return 0;
failed:
    r->player_controls.persistence_error=1;return -1;
}
int rf_player_settings_save(struct rf_game_runtime *r,const char *path)
{
    char text[192],temp[512];int n,fd,written=0,values[6];
    if(!r || !path || !*path)return -1;
    if(strlen(path)>sizeof(temp)-6)goto failed;
    n=snprintf(text,sizeof(text),"RFUI 1 %d %d %d %d %d\n",r->player_ui.mode,r->player_ui.scale_percent,
        r->player_ui.theme.panel_alpha,r->player_controls.hints,r->player_controls.crosshair);
    if(n<0 || n>=(int)sizeof(text) || !settings_decode(text,values))goto failed;
    snprintf(temp,sizeof(temp),"%s.tmp",path);
    fd=__openat(AT_FDCWD,temp,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(fd<0)goto failed;
    while(written<n) {
        int count=(int)__write(fd,text+written,n-written);
        if(count<=0){__close(fd);goto failed;}
        written+=count;
    }
    if(__close(fd)<0 || __rename(temp,path)<0)goto failed;
    r->player_controls.persistence_error=0;r->player_controls.settings_dirty=0;return 0;
failed:
    r->player_controls.persistence_error=1;return -1;
}
static void line(struct rf_command_output *o,const char *text)
{rf_command_output_write(o,RF_COMMAND_OUTPUT_NORMAL,text);}
int rf_player_terminal_command(const struct rf_command_context *ctx,
    struct rf_command_output *o,int argc,char **argv)
{
    struct rf_game_runtime *r=ctx?ctx->game_runtime:NULL;
    struct rf_player_request a={0};struct rf_player_result res;
    struct rf_player_weaver_query q;char text[640];int valid=0;
    const char *group=argc>0?argv[0]:"",*sub=argc>1?argv[1]:"status";
    if(!r || !r->session){rf_command_output_write(o,RF_COMMAND_OUTPUT_ERROR,"runtime unavailable");return -1;}
    a.world_generation=r->session->scene_local.world_generation;
    a.serial=r->session->game_state.weaver.job_serial;
    a.session_revision=r->story.session_revision;a.node_revision=r->story.node_revision;
    if(!strcmp(group,"ui")) {
        if(!strcmp(sub,"status")) {
            snprintf(text,sizeof(text),"mode=%s scale=%d opacity=%d hints=%d crosshair=%d",
                rf_player_ui_mode_name(r->player_ui.mode),r->player_ui.scale_percent,
                r->player_ui.theme.panel_alpha,r->player_controls.hints,r->player_controls.crosshair);line(o,text);
            line(o,"ui mode player|terminal|experiment; ui scale 75..175; ui opacity 100..255");
            line(o,"ui hints 0|1; ui crosshair 0|1");return 0;
        }
        if(argc==3 && !strcmp(sub,"mode")) {
            a.operation=RF_PLAYER_SET_MODE;
            if(!strcmp(argv[2],"player")){a.value=RF_PLAYER_UI_PLAYER;valid=1;}
            if(!strcmp(argv[2],"terminal")){a.value=RF_PLAYER_UI_TERMINAL;valid=1;}
            if(!strcmp(argv[2],"experiment")){a.value=RF_PLAYER_UI_EXPERIMENT;valid=1;}
        } else if(argc==3 && integer(argv[2],&a.value)) {
            valid=1;
            if(!strcmp(sub,"scale"))a.operation=RF_PLAYER_SET_SCALE;
            else if(!strcmp(sub,"opacity"))a.operation=RF_PLAYER_SET_OPACITY;
            else if(!strcmp(sub,"hints"))a.operation=RF_PLAYER_SET_HINTS;
            else if(!strcmp(sub,"crosshair"))a.operation=RF_PLAYER_SET_CROSSHAIR;
            else valid=0;
        }
    } else if(!strcmp(group,"weaver")) {
        rf_player_weaver_query(r,&q);
        if(!strcmp(sub,"status")) {
            snprintf(text,sizeof(text),"%s %s near=%d serial=%u phase=%s progress=%.1f%%",
                q.id,q.name,q.near,q.serial,q.status,q.progress*100);line(o,text);
            snprintf(text,sizeof(text),"estimate=%.2fs remaining=%.2fs energy=%.2f/%.2fkJ work=%.1f power=%d cpu=%d x1=%d",
                q.estimate_seconds,q.remaining_seconds,q.expected_kj,q.available_kj,q.cost.work,q.power_on,q.cpu_on,q.x1_on);line(o,text);
            snprintf(text,sizeof(text),"replace=%s rounds=%d can_start=%d can_collect=%d",
                q.replace_weapon>=0?toy_game_weapon_name(q.replace_weapon):"empty",q.rounds,q.can_start,q.can_collect);line(o,text);
            line(o,"weaver list|select ID|start|power 0/1|cpu 0/1|x1 0/1|collect [confirm]");return 0;
        }
        if(!strcmp(sub,"list")) {
            for(unsigned i=0;i<RF_WEAVER_BLUEPRINT_COUNT;++i){const struct rf_weaver_blueprint *b=&rf_weaver_blueprints[i];
                snprintf(text,sizeof(text),"%s %s %s",b->id,b->name,b->manufacturable&&b->size_supported?"available":b->unsupported_reason);line(o,text);}return 0;
        }
        if(argc==3 && !strcmp(sub,"select")) {
            for(unsigned i=0;i<RF_WEAVER_BLUEPRINT_COUNT;++i)if(!strcmp(argv[2],rf_weaver_blueprints[i].id)){
                a.operation=RF_PLAYER_WEAVER_SELECT;a.value=(int)i;valid=1;break;}
        } else if(argc==2 && !strcmp(sub,"start")){a.operation=RF_PLAYER_WEAVER_START;valid=1;}
        else if((argc==2||argc==3)&&!strcmp(sub,"collect")) {
            a.operation=RF_PLAYER_WEAVER_COLLECT;a.confirmed=argc==3&&!strcmp(argv[2],"confirm");
            a.value=q.replace_weapon;
            valid=argc==2||a.confirmed;
        } else if(argc==3 && integer(argv[2],&a.value)) {
            valid=1;
            if(!strcmp(sub,"power"))a.operation=RF_PLAYER_WEAVER_POWER;
            else if(!strcmp(sub,"cpu"))a.operation=RF_PLAYER_WEAVER_CPU;
            else if(!strcmp(sub,"x1"))a.operation=RF_PLAYER_WEAVER_X1;
            else valid=0;
        }
    } else if(!strcmp(group,"rts")) {
        if(!strcmp(sub,"status")) {
            snprintf(text,sizeof(text),"view=%s selected=%d follow=%d moving=%d target=%d,%d",r->rts_active?"RTS":"FPS",
                r->rts_selected,r->rts_follow_player,r->session->rts_move_active,r->session->rts_move_x,r->session->rts_move_z);line(o,text);
            line(o,"rts view fps|rts; rts select -1|0|flag; rts move X Z; rts stop; rts follow 0|1");return 0;
        }
        if(argc==3 && !strcmp(sub,"view") && (!strcmp(argv[2],"fps")||!strcmp(argv[2],"rts"))) {
            a.operation=RF_PLAYER_VIEW;a.value=!strcmp(argv[2],"rts");valid=1;
        } else if(argc==3 && !strcmp(sub,"select") && integer(argv[2],&a.value)){a.operation=RF_PLAYER_RTS_SELECT;valid=1;}
        else if(argc==4 && !strcmp(sub,"move") && integer(argv[2],&a.x)&&integer(argv[3],&a.z)){a.operation=RF_PLAYER_RTS_MOVE;valid=1;}
        else if(argc==2 && !strcmp(sub,"stop")){a.operation=RF_PLAYER_RTS_STOP;valid=1;}
        else if(argc==3 && !strcmp(sub,"follow") && integer(argv[2],&a.value)){a.operation=RF_PLAYER_RTS_FOLLOW;valid=1;}
    } else if(!strcmp(group,"comms")) {
        const struct rf_story_node *n=rf_story_current_node(&r->story);
        if(!strcmp(sub,"status")) {
            snprintf(text,sizeof(text),"session=%u node_revision=%u story=%d node=%d collapsed=%d link=%d",
                r->story.session_revision,r->story.node_revision,r->story.active_story,r->story.node_id,r->story.collapsed,r->story.link);line(o,text);
            if(n){line(o,n->line);for(int i=0;i<n->choice_count;++i){snprintf(text,sizeof(text),"%d: %s",i+1,n->choices[i].text);line(o,text);}}
            line(o,"comms answer N SESSION NODE_REV (0=continue); hide|resume|close|history");
            if(ctx->permission_level>=RF_COMMAND_PERMISSION_ADMIN)line(o,"experiment: comms replay 101|102; comms reset 0|101|102");
            return 0;
        }
        if(!strcmp(sub,"history")) {
            for(int i=r->story.history_count-1;i>=0&&o->count<RF_COMMAND_OUTPUT_MAX_LINES;--i){
                const struct rf_story_history_entry *h=&r->story.history[i];const struct rf_story_node *past=rf_story_find_node(h->node_id);
                if(past){
                    if(h->choice>=0 && h->choice<past->choice_count)
                        snprintf(text,sizeof(text),"YOU: %s",past->choices[h->choice].text);
                    else snprintf(text,sizeof(text),"%s: %s",past->speaker,past->line);
                    line(o,text);}}return 0;
        }
        if(argc==5 && !strcmp(sub,"answer")) {
            int sr,nr;if(integer(argv[2],&a.value)&&integer(argv[3],&sr)&&integer(argv[4],&nr)&&sr>0&&nr>0){
                a.operation=RF_PLAYER_DIALOG_ANSWER;a.value--;a.session_revision=(unsigned)sr;a.node_revision=(unsigned)nr;valid=1;}
        } else if(argc==2 && (!strcmp(sub,"hide")||!strcmp(sub,"resume"))){a.operation=RF_PLAYER_DIALOG_COLLAPSE;a.value=!strcmp(sub,"hide");valid=1;}
        else if(argc==2 && !strcmp(sub,"close")){a.operation=RF_PLAYER_DIALOG_CLOSE;valid=1;}
        else if(argc==3 && integer(argv[2],&a.value)) {
            if(!strcmp(sub,"replay")){a.operation=RF_PLAYER_STORY_REPLAY;valid=1;}
            if(!strcmp(sub,"reset")){a.operation=RF_PLAYER_STORY_RESET;valid=1;}
        }
    } else if(!strcmp(group,"task")) {
        snprintf(text,sizeof(text),"task=%d state=%d target=%s valid=%d",r->story.task.id,r->story.task.state,
            r->story.task.target_id,r->story.task.target_valid);line(o,text);
        if(r->story.task.title)line(o,r->story.task.title);
        if(r->story.task.hint)line(o,r->story.task.hint);
        return 0;
    } else if(!strcmp(group,"notices")) {
        for(int i=0;i<r->player_controls.notice_count;++i)line(o,r->player_controls.notices[i].text);
        if(!r->player_controls.notice_count)line(o,"没有通知记录");
        return 0;
    }
    if(!valid){rf_command_output_write(o,RF_COMMAND_OUTPUT_ERROR,"invalid arguments; run the command group without arguments for help");return -1;}
    res=rf_player_execute(r,&a,ctx->permission_level);
    snprintf(text,sizeof(text),"[%d] %s",res.code,res.message);
    rf_command_output_write(o,res.code?RF_COMMAND_OUTPUT_ERROR:RF_COMMAND_OUTPUT_NORMAL,text);return res.code?-1:0;
}

/* Protect transactions and read-only projections, not ephemeral UI geometry. */
int rf_player_commands_logic_test(void)
{
    struct rf_game_runtime *r=tlibc_malloc(sizeof(*r));
    struct rasterfall_session *s=tlibc_malloc(sizeof(*s));
    struct toy_game *saved=tlibc_malloc(sizeof(*saved));
    struct rf_player_weaver_query q;
    struct rf_player_request request={0};
    struct rf_command_context context={0};
    struct rf_command_output output;
    struct toy_mesh_weaver prior;
    struct toy_game_actor *player;
    const struct rf_map_runtime_object *machine;
    int checks=0,status=-1,values[6],pistol=-1,unsupported=-1;
    char *start_args[]={"weaver","start"};
#define PLAYER_CHECK(condition) do { if(!(condition)) { \
    __printf("PLAYER-COMMANDS failed line=%d\n",__LINE__);goto done; } checks++; } while(0)
    if(r)memset(r,0,sizeof(*r));
    if(s)memset(s,0,sizeof(*s));
    PLAYER_CHECK(r && s && saved);
    r->session=s;rf_player_controls_init(&r->player_controls);rf_story_init(&r->story);
    toy_game_init(&s->game_state,17);s->game_state.state=TOY_GAME_PLAYING;
    toy_mesh_weaver_defaults(&s->game_state.weaver);s->game_state.weaver.enabled=1;
    s->scene_local.world_generation=7;
    PLAYER_CHECK(rf_map_runtime_load(&s->map_ops.runtime,"rasterfall/assets/maps/outpost.map")==0);
    machine=rf_map_runtime_find_object(&s->map_ops.runtime,"mesh_weaver");
    PLAYER_CHECK(machine!=NULL);
    player=toy_game_local_player_actor(&s->game_state);
    PLAYER_CHECK(player!=NULL);
    player->active=1;player->state=TOY_GAME_ACTOR_ALIVE;player->control_disabled=0;
    player->x=machine->x-292;player->z=machine->z+510;player->ground_y=machine->y;
    player->airborne_y=0;
    for(unsigned i=0;i<RF_WEAVER_BLUEPRINT_COUNT;++i) {
        if(rf_weaver_blueprints[i].geometry.weapon==TOY_GAME_WEAPON_PISTOL)pistol=(int)i;
        if(!rf_weaver_blueprints[i].manufacturable || !rf_weaver_blueprints[i].size_supported)unsupported=(int)i;
    }
    PLAYER_CHECK(pistol>=0);
    r->player_controls.selected_blueprint=pistol;
    memcpy(saved,&s->game_state,sizeof(*saved));
    rf_player_weaver_query(r,&q);
    PLAYER_CHECK(!memcmp(saved,&s->game_state,sizeof(*saved)) && q.can_start);
    PLAYER_CHECK(unsupported>=0);
    r->story.task.id=RF_STORY_TASK_BLUEPRINT;r->story.task.state=RF_STORY_TASK_RUNNING;
    strcpy(r->story.task.target_id,"mesh_weaver");
    request.operation=RF_PLAYER_WEAVER_SELECT;request.value=unsupported;request.world_generation=7;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_OK &&
        r->story.task.state==RF_STORY_TASK_RUNNING);
    request.value=pistol;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_OK &&
        r->story.task.state==RF_STORY_TASK_DONE);
    request.operation=RF_PLAYER_WEAVER_START;request.world_generation=6;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_STALE);
    PLAYER_CHECK(!memcmp(saved,&s->game_state,sizeof(*saved)));
    request.world_generation=7;r->net.mode=RASTERFALL_NET_CLIENT;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_PERMISSION);
    r->net.mode=RASTERFALL_NET_OFF;player->x+=100000;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_RANGE);
    player->x-=100000;player->ground_y+=2000;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_RANGE);
    player->ground_y-=2000;r->player_controls.selected_blueprint=(int)RF_WEAVER_BLUEPRINT_COUNT;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_INVALID);
    r->player_controls.selected_blueprint=pistol;s->game_state.weaver.supply.storage_bytes=0;
    rf_player_weaver_query(r,&q);PLAYER_CHECK(!q.can_start);
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_RESOURCE);
    s->game_state.weaver.supply=saved->weaver.supply;
    context.game_runtime=r;context.permission_level=RF_COMMAND_PERMISSION_USER;
    rf_command_output_init(&output);
    PLAYER_CHECK(rf_player_terminal_command(&context,&output,2,start_args)==0);
    PLAYER_CHECK(s->game_state.weaver.phase==TOY_WEAVER_CALIBRATING && s->game_state.weaver.job_serial==1);
    prior=s->game_state.weaver;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_STALE);
    PLAYER_CHECK(!memcmp(&prior,&s->game_state.weaver,sizeof(prior)));
    request.serial=1;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_BUSY);
    PLAYER_CHECK(!memcmp(&prior,&s->game_state.weaver,sizeof(prior)));
    toy_game_weaver_update(&s->game_state,60000);rf_player_weaver_query(r,&q);
    PLAYER_CHECK(q.can_collect && q.replace_weapon==TOY_GAME_WEAPON_PISTOL);
    request.operation=RF_PLAYER_WEAVER_COLLECT;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_CONFIRM_REPLACE);
    request.confirmed=1;request.value=TOY_GAME_WEAPON_AK;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_STALE);
    request.value=TOY_GAME_WEAPON_PISTOL;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_OK);
    memcpy(saved,&s->game_state,sizeof(*saved));
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_UNAVAILABLE);
    PLAYER_CHECK(!memcmp(saved,&s->game_state,sizeof(*saved)) && s->game_state.weaver.collected_count==1);
    int slot=toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL)->slot;
    player->slots[slot].weapon=-1;request.operation=RF_PLAYER_WEAVER_START;request.confirmed=0;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_OK);
    toy_game_weaver_update(&s->game_state,60000);rf_player_weaver_query(r,&q);
    PLAYER_CHECK(q.can_collect && q.replace_weapon==-1);
    request.operation=RF_PLAYER_WEAVER_COLLECT;request.serial=q.serial;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_OK);
    request.operation=RF_PLAYER_STORY_RESET;request.value=0;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_PERMISSION);
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_ADMIN).code==RF_PLAYER_OK);
    request.operation=RF_PLAYER_DIALOG_ANSWER;
    PLAYER_CHECK(rf_player_execute(r,&request,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_STALE);
    request.operation=RF_PLAYER_SET_SCALE;request.value=120;
    PLAYER_CHECK(rf_player_execute(r,&request,(enum rf_command_permission_level)99).code==RF_PLAYER_PERMISSION);
    PLAYER_CHECK(settings_decode("RFUI 1 0 100 220 1 1\r\n",values));
    PLAYER_CHECK(!settings_decode("RFUI 1 0 100 220 1 1 junk",values));
    PLAYER_CHECK(!settings_decode("RFUI 1 0 100 220 1",values));
    PLAYER_CHECK(!settings_decode("RFUI 1 0 999999999999999999 220 1 1",values));
    PLAYER_CHECK(!settings_decode("RFUI 1 0 100 220 1 9",values));
    PLAYER_CHECK(!integer("2147483648",&values[0]) && !integer("-",&values[0]));
    status=0;
done:
    if(s)rf_map_runtime_unload(&s->map_ops.runtime);
    tlibc_free(saved);tlibc_free(s);tlibc_free(r);
    __printf("PLAYER-COMMANDS %s checks=%d query-only/authority/stale/repeat/confirmation/settings\n",
        status?"FAIL":"PASS",checks);
#undef PLAYER_CHECK
    return status;
}
