#include "tlibc_everything.h"
#include "rf_game_lifecycle.h"
#include "rf_device_commands.h"

static struct rf_player_result device_result(int code,const char *message)
{
    struct rf_player_result out={0};out.code=code;
    snprintf(out.message,sizeof(out.message),"%s",message);return out;
}
void rf_device_service_init(struct rf_device_service *service)
{ memset(service,0,sizeof(*service));service->pending_world=-1; }

static struct rf_player_result deploy_allowed(const struct rf_game_runtime *r)
{
    const struct rasterfall_session *s=r?r->session:NULL;
    const struct toy_game_actor *p=s?toy_game_local_player_actor_const(&s->game_state):NULL;
    const struct rf_map_runtime_object *table=s?rf_map_runtime_find_object(&s->map_ops.runtime,"command_table"):NULL;
    if(!s || !table || s->world_id!=RASTERFALL_WORLD_OUTPOST)
        return device_result(RF_PLAYER_UNAVAILABLE,"部署只能从前哨站指挥桌发起");
    if(r->net.mode!=RASTERFALL_NET_OFF)
        return device_result(RF_PLAYER_PERMISSION,"地图部署当前仅支持离线会话");
    if(s->game_state.state!=TOY_GAME_PLAYING || !p || !p->active ||
        p->state!=TOY_GAME_ACTOR_ALIVE || p->control_disabled)
        return device_result(RF_PLAYER_UNAVAILABLE,"角色目前无法部署");
    if((int64_t)p->x<table->x-1250LL || (int64_t)p->x>table->x+1250LL ||
       (int64_t)p->z<table->z-1900LL || (int64_t)p->z>table->z-850LL ||
       (int64_t)p->ground_y+p->airborne_y<table->y-750LL ||
       (int64_t)p->ground_y+p->airborne_y>table->y+750LL)
        return device_result(RF_PLAYER_RANGE,"请靠近指挥桌南侧操作区后部署");
    return device_result(RF_PLAYER_OK,"可以部署");
}
void rf_device_query(const struct rf_game_runtime *r,struct rf_device_query *q)
{
    struct rf_player_result allowed;
    memset(q,0,sizeof(*q));q->pending_world=-1;
    if(!r || !r->session || !r->device_service.query) {
        snprintf(q->reason,sizeof(q->reason),"设备服务尚未就绪");return;
    }
    r->device_service.query(r->device_service.context,q);
    q->feature_count=q->feature_count<0?0:q->feature_count>RF_DEVICE_FEATURE_CAP?RF_DEVICE_FEATURE_CAP:q->feature_count;
    q->map_count=q->map_count<0?0:q->map_count>RF_DEVICE_MAP_CAP?RF_DEVICE_MAP_CAP:q->map_count;
    q->bound=1;q->world_generation=r->session->scene_local.world_generation;
    q->pending_world=r->device_service.pending_world;
    q->table_present=rf_map_runtime_find_object(&r->session->map_ops.runtime,"command_table")!=NULL;
    allowed=deploy_allowed(r);q->table_near=allowed.code==RF_PLAYER_OK;
    q->deployable=allowed.code==RF_PLAYER_OK && q->pending_world<0;
    snprintf(q->reason,sizeof(q->reason),"%s",q->pending_world>=0?"已有部署请求等待处理":allowed.message);
}
struct rf_player_result rf_device_execute(struct rf_game_runtime *r,
    const struct rf_device_request *a,enum rf_command_permission_level permission)
{
    struct rf_device_query q;
    struct rf_player_result out=device_result(RF_PLAYER_OK,"操作完成");
    if(!r || !a || !r->session)return device_result(RF_PLAYER_UNAVAILABLE,"当前没有可用会话");
    if(permission<RF_COMMAND_PERMISSION_USER || permission>RF_COMMAND_PERMISSION_SUPER)
        out=device_result(RF_PLAYER_PERMISSION,"命令权限无效");
    else {
        rf_device_query(r,&q);
        if(!q.bound || !r->device_service.apply)out=device_result(RF_PLAYER_UNAVAILABLE,"设备服务尚未就绪");
        else if(a->operation==RF_DEVICE_RENDER_SET) {
            if(a->index<0 || a->index>=q.feature_count || a->value<0 || a->value>1)
                out=device_result(RF_PLAYER_INVALID,"渲染功能或开关值无效");
            else if(q.features[a->index].support>=RF_DEVICE_UNSUPPORTED)
                out=device_result(RF_PLAYER_UNAVAILABLE,q.features[a->index].support==RF_DEVICE_UNIMPLEMENTED?
                    "功能尚未实现，设置未改变":"当前渲染器不支持，设置未改变");
            else if(!q.features[a->index].adjustable && a->value!=q.features[a->index].enabled)
                out=device_result(RF_PLAYER_UNAVAILABLE,"基础渲染管线默认启用，当前不能关闭");
            else if(q.features[a->index].enabled!=a->value &&
                r->device_service.apply(r->device_service.context,a->operation,a->index,a->value)<0)
                out=device_result(RF_PLAYER_UNAVAILABLE,"渲染设置未能应用");
        } else if(a->operation==RF_DEVICE_TABLE_SELECT || a->operation==RF_DEVICE_TABLE_DEPLOY) {
            if(a->world_generation!=q.world_generation)out=device_result(RF_PLAYER_STALE,"世界已变化，请刷新后重试");
            else if(a->index<0 || a->index>=q.map_count)out=device_result(RF_PLAYER_INVALID,"地图不存在");
            else if(a->operation==RF_DEVICE_TABLE_SELECT) {
                if(r->device_service.apply(r->device_service.context,a->operation,a->index,0)<0)
                    out=device_result(RF_PLAYER_UNAVAILABLE,"地图选择未能应用");
            } else if((out=deploy_allowed(r)).code==RF_PLAYER_OK) {
                if(r->device_service.pending_world>=0)out=device_result(RF_PLAYER_BUSY,"已有部署请求等待处理");
                else if(!q.maps[a->index].available)out=device_result(RF_PLAYER_UNAVAILABLE,"地图文件不可用");
                else {
                    r->device_service.pending_world=q.maps[a->index].world;
                    r->device_service.pending_generation=q.world_generation;
                    out=device_result(RF_PLAYER_OK,"部署请求已接受");
                }
            }
        } else out=device_result(RF_PLAYER_INVALID,"设备操作不存在");
    }
    r->device_service.last=out;return out;
}
int rf_device_take_deploy(struct rf_game_runtime *r)
{
    struct rf_player_result allowed;
    struct rf_device_query q;
    int world,available=0;
    if(!r || r->device_service.pending_world<0)return -1;
    world=r->device_service.pending_world;
    rf_device_query(r,&q);r->device_service.pending_world=-1;
    allowed=deploy_allowed(r);
    if(!r->session || r->device_service.pending_generation!=r->session->scene_local.world_generation)
        allowed=device_result(RF_PLAYER_STALE,"部署请求已过时，请刷新后重试");
    for(int i=0;i<q.map_count;++i)if(q.maps[i].world==world && q.maps[i].available)available=1;
    if(allowed.code==RF_PLAYER_OK && !available)allowed=device_result(RF_PLAYER_UNAVAILABLE,"地图文件不可用");
    r->device_service.last=allowed;
    return allowed.code==RF_PLAYER_OK?world:-1;
}
static void device_line(struct rf_command_output *o,const char *text)
{rf_command_output_write(o,RF_COMMAND_OUTPUT_NORMAL,text);}
int rf_device_terminal_command(const struct rf_command_context *ctx,
    struct rf_command_output *o,int argc,char **argv)
{
    struct rf_game_runtime *r=ctx?ctx->game_runtime:NULL;
    struct rf_device_query q;struct rf_device_request a={0};struct rf_player_result out;
    const char *group=argc>0?argv[0]:"",*sub=argc>1?argv[1]:"status";
    char line[512];int valid=0;
    if(!r || !r->session){rf_command_output_write(o,RF_COMMAND_OUTPUT_ERROR,"runtime unavailable");return -1;}
    rf_device_query(r,&q);a.world_generation=q.world_generation;
    if(!strcmp(group,"render")) {
        if(argc==1 || (argc==2 && !strcmp(sub,"status"))) {
            snprintf(line,sizeof(line),"renderer=%s bound=%d",q.scene_backend?"gpu-scene":"cpu",q.bound);device_line(o,line);
            for(int i=0;i<q.feature_count;++i) {
                const struct rf_device_feature *f=&q.features[i];
                snprintf(line,sizeof(line),"%s=%d support=%s adjustable=%d / %s",f->id,f->enabled,
                    f->support==RF_DEVICE_FORMAL?"formal":f->support==RF_DEVICE_EXPERIMENTAL?"experimental":
                    f->support==RF_DEVICE_UNSUPPORTED?"unsupported":"unimplemented",f->adjustable,f->name);
                device_line(o,line);
            }
            device_line(o,"render set <feature-id> 0|1");return 0;
        }
        if(argc==4 && !strcmp(sub,"set") && (!strcmp(argv[3],"0") || !strcmp(argv[3],"1")))
            for(int i=0;i<q.feature_count;++i)if(!strcmp(argv[2],q.features[i].id)) {
                a.operation=RF_DEVICE_RENDER_SET;a.index=i;a.value=argv[3][0]-'0';valid=1;break;
            }
    } else if(!strcmp(group,"table")) {
        if(argc==1 || (argc==2 && (!strcmp(sub,"status") || !strcmp(sub,"maps")))) {
            snprintf(line,sizeof(line),"table present=%d near=%d deployable=%d selected=%d pending=%d / %s",
                q.table_present,q.table_near,q.deployable,q.selected_map,q.pending_world,q.reason);device_line(o,line);
            for(int i=0;i<q.map_count;++i) {
                snprintf(line,sizeof(line),"%s world=%d available=%d / %s",q.maps[i].id,q.maps[i].world,q.maps[i].available,q.maps[i].name);device_line(o,line);
            }
            device_line(o,"table select|deploy <map-id>");return 0;
        }
        if(argc==3 && (!strcmp(sub,"select") || !strcmp(sub,"deploy")))
            for(int i=0;i<q.map_count;++i)if(!strcmp(argv[2],q.maps[i].id)) {
                a.operation=!strcmp(sub,"select")?RF_DEVICE_TABLE_SELECT:RF_DEVICE_TABLE_DEPLOY;a.index=i;valid=1;break;
            }
    } else if(!strcmp(group,"devices") && (argc==1 || (argc==2 && !strcmp(sub,"status")))) {
        const char *ids[]={"main_terminal","command_table","mesh_weaver","mesh_weaver_rf1"};
        for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);++i) {
            const struct rf_map_runtime_object *object=rf_map_runtime_find_object(&r->session->map_ops.runtime,ids[i]);
            if(object)snprintf(line,sizeof(line),"%s present=1 x=%d y=%d z=%d",ids[i],object->x,object->y,object->z);
            else snprintf(line,sizeof(line),"%s present=0",ids[i]);
            device_line(o,line);
        }
        return 0;
    }
    if(!valid){rf_command_output_write(o,RF_COMMAND_OUTPUT_ERROR,"invalid arguments; run the command group without arguments for help");return -1;}
    out=rf_device_execute(r,&a,ctx->permission_level);
    snprintf(line,sizeof(line),"[%d] %s",out.code,out.message);
    rf_command_output_write(o,out.code?RF_COMMAND_OUTPUT_ERROR:RF_COMMAND_OUTPUT_NORMAL,line);return out.code?-1:0;
}

struct device_test_state { int enabled,selected,writes; };
static void device_test_query(void *context,struct rf_device_query *q)
{
    const struct device_test_state *s=context;
    q->scene_backend=1;q->feature_count=3;q->map_count=2;q->selected_map=s->selected;
    q->features[0]=(struct rf_device_feature){"flashlight","Flashlight",RF_DEVICE_EXPERIMENTAL,s->enabled,1};
    q->features[1]=(struct rf_device_feature){"pbr","PBR",RF_DEVICE_EXPERIMENTAL,1,0};
    q->features[2]=(struct rf_device_feature){"outline","Outline",RF_DEVICE_UNIMPLEMENTED,0,1};
    q->maps[0]=(struct rf_device_map){"campaign","Campaign",RASTERFALL_WORLD_CAMPAIGN_01,1};
    q->maps[1]=(struct rf_device_map){"frontier","Frontier",RASTERFALL_WORLD_FRONTIER_STATION_01,0};
}
static int device_test_apply(void *context,int operation,int index,int value)
{
    struct device_test_state *s=context;(void)index;s->writes++;
    if(operation==RF_DEVICE_RENDER_SET)s->enabled=value;
    else if(operation==RF_DEVICE_TABLE_SELECT)s->selected=index;
    else return -1;
    return 0;
}
int rf_device_commands_logic_test(void)
{
    struct rf_game_runtime *r=tlibc_malloc(sizeof(*r));
    struct rasterfall_session *s=tlibc_malloc(sizeof(*s));
    struct device_test_state state={0};
    struct rf_device_request a={RF_DEVICE_RENDER_SET,0,1,4};
    struct rf_device_query q;struct rf_device_service service_before;
    struct toy_game_actor *p;int checks=0,status=-1;
#define DEVICE_CHECK(condition) do { if(!(condition)) { \
    __printf("DEVICE-COMMANDS failed line=%d\n",__LINE__);goto done; } checks++; } while(0)
    if(r)memset(r,0,sizeof(*r));
    if(s)memset(s,0,sizeof(*s));
    DEVICE_CHECK(r && s);r->session=s;s->world_id=RASTERFALL_WORLD_OUTPOST;
    s->scene_local.world_generation=4;toy_game_init(&s->game_state,5);s->game_state.state=TOY_GAME_PLAYING;
    DEVICE_CHECK(rf_map_runtime_load(&s->map_ops.runtime,"rasterfall/assets/maps/outpost.map")==0);
    p=toy_game_local_player_actor(&s->game_state);p->x=0;p->z=-1160;p->ground_y=0;p->airborne_y=0;
    p->active=1;p->state=TOY_GAME_ACTOR_ALIVE;p->control_disabled=0;
    rf_device_service_init(&r->device_service);r->device_service.context=&state;
    r->device_service.query=device_test_query;r->device_service.apply=device_test_apply;
    service_before=r->device_service;rf_device_query(r,&q);
    DEVICE_CHECK(q.deployable && q.table_near && !memcmp(&service_before,&r->device_service,sizeof(service_before)) && !state.writes);
    r->net.mode=RASTERFALL_NET_CLIENT;p->x=99999;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_OK && state.enabled && state.writes==1);
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_OK && state.writes==1);
    a.index=1;a.value=0;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_UNAVAILABLE && state.writes==1);
    a.index=2;a.value=1;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_UNAVAILABLE && state.writes==1);
    a.index=0;a.operation=RF_DEVICE_TABLE_DEPLOY;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_PERMISSION);
    r->net.mode=RASTERFALL_NET_OFF;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_RANGE);
    p->x=0;p->airborne_y=1000;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_RANGE);
    p->airborne_y=0;a.world_generation=3;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_STALE);
    a.world_generation=4;a.index=1;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_UNAVAILABLE);
    a.index=0;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_OK);
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_BUSY);
    s->scene_local.world_generation=5;
    DEVICE_CHECK(rf_device_take_deploy(r)==-1 && r->device_service.last.code==RF_PLAYER_STALE);
    a.world_generation=5;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_OK);
    p->z=1000;
    DEVICE_CHECK(rf_device_take_deploy(r)==-1 && r->device_service.last.code==RF_PLAYER_RANGE);
    p->z=-1160;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_OK);
    DEVICE_CHECK(rf_device_take_deploy(r)==RASTERFALL_WORLD_CAMPAIGN_01 && rf_device_take_deploy(r)==-1);
    s->world_id=RASTERFALL_WORLD_CAMPAIGN_01;
    DEVICE_CHECK(rf_device_execute(r,&a,RF_COMMAND_PERMISSION_USER).code==RF_PLAYER_UNAVAILABLE);
    DEVICE_CHECK(rf_device_execute(r,&a,(enum rf_command_permission_level)99).code==RF_PLAYER_PERMISSION);
    status=0;
done:
    if(s)rf_map_runtime_unload(&s->map_ops.runtime);
    tlibc_free(s);tlibc_free(r);
    __printf("DEVICE-COMMANDS %s checks=%d readonly/render-idempotence/deploy-authority/stale\n",status?"FAIL":"PASS",checks);
#undef DEVICE_CHECK
    return status;
}
