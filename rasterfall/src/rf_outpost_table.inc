/* Outpost command table: presentation state and map-file previews. */
#define RF_TABLE_MAPS 5
struct rf_table_map {
    enum rasterfall_world_id world;
    const char *name;
    int available;
};
struct rf_table_ui {
    int open, near, selected, pointer_x, pointer_y;
    int dropdown, hover, focus, preview_state, preview_map, preview_requested;
    int preview_percent;
    char message[192];
    struct rf_table_map maps[RF_TABLE_MAPS];
};
static struct rf_table_ui rf_table;
static int rf_showcase_near;
static int rf_device_table_request(int index,int deploy);

static int rf_table_operation_near(const struct rasterfall_session *session,int x,int z)
{
    const struct rf_map_runtime_object *table=rf_map_runtime_find_object(&session->map_ops.runtime,"command_table");
    const struct toy_game_actor *player=toy_game_local_player_actor_const(&session->game_state);
    return table && player && (int64_t)x>=table->x-1250LL && (int64_t)x<=table->x+1250LL &&
        (int64_t)z>=table->z-1900LL && (int64_t)z<=table->z-850LL &&
        (int64_t)player->ground_y+player->airborne_y>=table->y-750LL &&
        (int64_t)player->ground_y+player->airborne_y<=table->y+750LL;
}

static int rf_showcase_button_near(const struct rasterfall_session *session,int x,int z)
{
    return rf_lab_near(session,x,z);
}

static void rf_table_read_map(struct rf_table_map *map)
{
    /* Catalog availability only; geometry is loaded on selection. */
    FILE *file=fopen(rasterfall_world_map_path(map->world),"rb");
    if(file){map->available=1;fclose(file);}
}

static void rf_table_init(void)
{
    memset(&rf_table, 0, sizeof(rf_table));
    snprintf(rf_table.message,sizeof(rf_table.message),"选择目的地后，点击右侧预览画面框开始加载");
    rf_table.maps[0].world = RASTERFALL_WORLD_OUTPOST;
    rf_table.maps[0].name = "OUTPOST";
    rf_table.maps[1].world = RASTERFALL_WORLD_CAMPAIGN_01;
    rf_table.maps[1].name = "CAMPAIGN 01";
    rf_table.maps[2].world = RASTERFALL_WORLD_FRONTIER_STATION_01;
    rf_table.maps[2].name = "边缘站点 01";
    rf_table.maps[3].world=RASTERFALL_WORLD_TACTICAL_ARENA;
    rf_table.maps[3].name="AI 策略对抗场";
    rf_table.maps[4].world=RASTERFALL_WORLD_TACTICAL_RANGE;
    rf_table.maps[4].name="武器靶场";
    for (int i = 0; i < RF_TABLE_MAPS; ++i)
        rf_table_read_map(&rf_table.maps[i]);
#ifndef TOYC_WINDOWS
    rf_table.maps[3].available=rf_table.maps[4].available=0;
    rf_table.preview_state=4;
#endif
}

#include "rf_table_panel.inc"
