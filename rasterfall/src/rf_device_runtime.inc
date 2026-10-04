/* Included after render terminal and command table owners. The bridge only
 * exposes their presentation values; commands own validation and dispatch. */
static struct rf_game_runtime *rf_device_runtime_owner;
static void rf_device_runtime_query(void *context,struct rf_device_query *q)
{
    static const char *feature_ids[]={"outline","shadows","pbr","flashlight","character-material","linear-filter"};
    static const char *map_ids[]={"outpost","campaign","whu","frontier"};
    _Static_assert(sizeof(feature_ids)/sizeof(feature_ids[0])==RF_RENDER_FEATURE_COUNT,"render feature IDs must match catalog");
    _Static_assert(RF_RENDER_FEATURE_COUNT<=RF_DEVICE_FEATURE_CAP,"device feature query capacity");
    _Static_assert(sizeof(map_ids)/sizeof(map_ids[0])==RF_TABLE_MAPS,"map IDs must match catalog");
    _Static_assert(RF_TABLE_MAPS<=RF_DEVICE_MAP_CAP,"device map query capacity");
    (void)context;
    q->scene_backend=rf_render_terminal.scene_backend;
    q->feature_count=RF_RENDER_FEATURE_COUNT;q->map_count=RF_TABLE_MAPS;
    q->selected_map=rf_table.selected;
    for(int i=0;i<q->feature_count;++i) {
        struct rf_device_feature *f=&q->features[i];
        f->id=feature_ids[i];f->name=rf_render_features[i].name;
        f->support=(int)rf_render_feature_support(i);f->adjustable=i!=1&&i!=2;
        f->enabled=i==0?rf_render_terminal.edge_effective:
            i==3?rf_render_terminal.flashlight:
            i==4?rf_gpu_scene_character_material_enabled():
            i==5?rf_gpu_scene_linear_filter_enabled():rf_render_terminal.scene_backend;
    }
    for(int i=0;i<q->map_count;++i) {
        q->maps[i].id=map_ids[i];q->maps[i].name=rf_table.maps[i].name;
        q->maps[i].world=rf_table.maps[i].world;q->maps[i].available=rf_table.maps[i].available;
    }
}
static int rf_device_runtime_apply(void *context,int operation,int index,int value)
{
    (void)context;
    if(operation==RF_DEVICE_RENDER_SET && index>=0 && index<RF_RENDER_FEATURE_COUNT) {
        rf_render_terminal_apply(index,value);return 0;
    }
    if(operation==RF_DEVICE_TABLE_SELECT && index>=0 && index<RF_TABLE_MAPS) {
        rf_table.selected=index;return 0;
    }
    return -1;
}
static void rf_device_runtime_bind(struct rf_game_runtime *runtime)
{
    rf_device_runtime_owner=runtime;
    rf_device_service_init(&runtime->device_service);
    runtime->device_service.context=runtime;
    runtime->device_service.query=rf_device_runtime_query;
    runtime->device_service.apply=rf_device_runtime_apply;
}
static void rf_render_terminal_request(int index)
{
    struct rf_device_query q;struct rf_device_request request={0};
    if(!rf_device_runtime_owner)return;
    rf_device_query(rf_device_runtime_owner,&q);
    if(index<0 || index>=q.feature_count)return;
    request.operation=RF_DEVICE_RENDER_SET;request.index=index;
    request.value=q.features[index].adjustable?!q.features[index].enabled:q.features[index].enabled;
    rf_render_terminal.selected=index;
    rf_device_execute(rf_device_runtime_owner,&request,RF_COMMAND_PERMISSION_USER);
    rf_render_terminal.message=rf_device_runtime_owner->device_service.last.message;
}
static int rf_device_table_request(int index,int deploy)
{
    struct rf_device_request request={0};struct rf_player_result result;
    if(!rf_device_runtime_owner || !rf_device_runtime_owner->session)return -1;
    request.operation=deploy?RF_DEVICE_TABLE_DEPLOY:RF_DEVICE_TABLE_SELECT;request.index=index;
    request.world_generation=rf_device_runtime_owner->session->scene_local.world_generation;
    result=rf_device_execute(rf_device_runtime_owner,&request,RF_COMMAND_PERMISSION_USER);
    if(deploy || result.code)snprintf(rf_table.message,sizeof(rf_table.message),"%s",result.message);
    return result.code?-1:0;
}
