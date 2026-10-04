/* Outpost command table: presentation state and map-file previews. */
#define RF_TABLE_MAPS 4
#define RF_TABLE_RECTS 192
struct rf_table_rect { int x0, z0, x1, z1; unsigned int color; };
struct rf_table_map {
    enum rasterfall_world_id world;
    const char *name;
    int min_x, max_x, min_z, max_z;
    int count, available;
    struct rf_table_rect rects[RF_TABLE_RECTS];
};
struct rf_table_ui {
    int open, near, selected, pointer_x, pointer_y;
    char message[192];
    struct rf_table_map maps[RF_TABLE_MAPS];
};
static struct rf_table_ui rf_table;
static int rf_showcase_near;
static int rf_device_table_request(int index,int deploy);

static int rf_showcase_button_near(const struct rasterfall_session *session,int x,int z)
{
    return rf_lab_near(session,x,z);
}

static int rf_table_field(const char *line, const char *key, int *out)
{
    char pattern[32];
    const char *p;
    char *end;
    snprintf(pattern, sizeof(pattern), " %s=", key);
    p = strstr(line, pattern);
    if (!p) return 0;
    p += strlen(pattern);
    *out = (int)strtol(p, &end, 10);
    return end != p;
}

static void rf_table_read_map(struct rf_table_map *map)
{
    uint32_t size = 0;
    unsigned char *blob = toy_asset_load_file(rasterfall_world_map_path(map->world), &size);
    char *copy, *line;
    if (!blob || !size || size > 4 * 1024 * 1024) {
        if (blob) free(blob);
        return;
    }
    copy = malloc((size_t)size + 1);
    if (!copy) { free(blob); return; }
    memcpy(copy, blob, size);
    copy[size] = 0;
    free(blob);
    line = copy;
    while (*line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        if (!strncmp(line, "world ", 6)) {
            if (rf_table_field(line, "min_x", &map->min_x) &&
                rf_table_field(line, "max_x", &map->max_x) &&
                rf_table_field(line, "min_z", &map->min_z) &&
                rf_table_field(line, "max_z", &map->max_z) &&
                map->max_x > map->min_x && map->max_z > map->min_z)
                map->available = 1;
        } else if (((!strncmp(line, "surface ", 8) &&
                     (strstr(line, " kind=ground ") ||
                      strstr(line, " kind=floor ") ||
                      strstr(line, " kind=platform "))) ||
                    (!strncmp(line, "collision ", 10) &&
                     strstr(line, " shape=box "))) &&
                   map->count < RF_TABLE_RECTS) {
            struct rf_table_rect *r = &map->rects[map->count];
            const char *c = strstr(line, " material=");
            char *end;
            if (!c) c = strstr(line, " color=");
            if (rf_table_field(line, "min_x", &r->x0) &&
                rf_table_field(line, "max_x", &r->x1) &&
                rf_table_field(line, "min_z", &r->z0) &&
                rf_table_field(line, "max_z", &r->z1)) {
                r->color = c ? (unsigned int)strtoul(strchr(c, '=') + 1, &end, 16) : 0x627786;
                map->count++;
            }
        }
        if (!next) break;
        line = next;
    }
    free(copy);
}

static void rf_table_init(void)
{
    memset(&rf_table, 0, sizeof(rf_table));
    snprintf(rf_table.message,sizeof(rf_table.message),"选择地图后部署；终端使用 table maps 查看目标");
    rf_table.maps[0].world = RASTERFALL_WORLD_OUTPOST;
    rf_table.maps[0].name = "OUTPOST";
    rf_table.maps[1].world = RASTERFALL_WORLD_CAMPAIGN_01;
    rf_table.maps[1].name = "CAMPAIGN 01";
    rf_table.maps[2].world = RASTERFALL_WORLD_RETURN_TO_WHU_V0;
    rf_table.maps[2].name = "RETURN TO WHU";
    rf_table.maps[3].world = RASTERFALL_WORLD_FRONTIER_STATION_01;
    rf_table.maps[3].name = "边缘站点 01";
    for (int i = 0; i < RF_TABLE_MAPS; ++i)
        rf_table_read_map(&rf_table.maps[i]);
}

static int rf_table_hit(int width, int height, int px, int py)
{
    int x = (width - 820) / 2, y = (height - 500) / 2;
    for (int i = 0; i < RF_TABLE_MAPS; ++i)
        if (px >= x + 25 && px < x + 270 &&
            py >= y + 90 + i * 86 && py < y + 156 + i * 86)
            return i;
    return -1;
}

static void rf_table_draw(struct rasterfall_canvas *canvas)
{
    int x = (canvas->width - 820) / 2, y = (canvas->height - 500) / 2;
    int p_x = x + 305, p_y = y + 92, p_w = 480, p_h = 315;
    struct rf_table_map *map = &rf_table.maps[rf_table.selected];
    rasterfall_canvas_rect(canvas, x - 4, y - 4, 828, 508, 0x4AC8CC, 255);
    rasterfall_canvas_rect(canvas, x, y, 820, 500, 0x101C2C, 255);
    rasterfall_canvas_text(canvas, x + 28, y + 28, "OUTPOST / MISSION MAP", 0xD6F3F3);
    rasterfall_canvas_text(canvas, x + 28, y + 53,
                           "CLICK A MAP TO DEPLOY", 0x88B9C3);
    for (int i = 0; i < RF_TABLE_MAPS; ++i) {
        int row = y + 90 + i * 86;
        rasterfall_canvas_rect(canvas, x + 25, row, 245, 66,
                               i == rf_table.selected ? 0x2A5663 : 0x203543, 255);
        rasterfall_canvas_text(canvas, x + 42, row + 22,
            rf_table.maps[i].name,
            rf_table.maps[i].available ? 0xE3F8F4 : 0x788A91);
    }
    rasterfall_canvas_rect(canvas, p_x, p_y, p_w, p_h, 0x344B59, 255);
    rasterfall_canvas_rect(canvas, p_x + 3, p_y + 3, p_w - 6, p_h - 6, 0x14212C, 255);
    if (map->available) {
        int dx = map->max_x - map->min_x, dz = map->max_z - map->min_z;
        for (int i = 0; i < map->count; ++i) {
            struct rf_table_rect *r = &map->rects[i];
            int rx = p_x + 10 + (int)((long long)(r->x0 - map->min_x) * (p_w - 20) / dx);
            int ry = p_y + 10 + (int)((long long)(map->max_z - r->z1) * (p_h - 20) / dz);
            int rw = (int)((long long)(r->x1 - r->x0) * (p_w - 20) / dx);
            int rh = (int)((long long)(r->z1 - r->z0) * (p_h - 20) / dz);
            if (rw > 0 && rh > 0)
                rasterfall_canvas_rect(canvas, rx, ry, rw, rh, r->color, 255);
        }
        rasterfall_canvas_text(canvas, p_x + 12, p_y + p_h - 27,
                               "NORTH ^  /  MAP FILE PREVIEW", 0xE4F3F3);
    } else
        rasterfall_canvas_text(canvas, p_x + 35, p_y + 140,
                               "MAP FILE UNAVAILABLE", 0xDDA88E);
    rasterfall_canvas_text(canvas, x + 28, y + 455,
                           "MOUSE: SELECT / DEPLOY      ESC: LEAVE TABLE", 0xA7DDE0);
    rasterfall_canvas_text(canvas, x + 28, y + 428,rf_table.message,0xDDA88E);
    rasterfall_canvas_rect(canvas, rf_table.pointer_x - 5, rf_table.pointer_y, 11, 1, 0xFFFFFF, 255);
    rasterfall_canvas_rect(canvas, rf_table.pointer_x, rf_table.pointer_y - 5, 1, 11, 0xFFFFFF, 255);
}
