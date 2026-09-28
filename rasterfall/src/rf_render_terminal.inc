/* Outpost rendering terminal: presentation-only feature catalog and state. */
enum rf_render_support {
    RF_RENDER_FORMAL,
    RF_RENDER_EXPERIMENTAL,
    RF_RENDER_UNSUPPORTED,
    RF_RENDER_UNIMPLEMENTED
};

struct rf_render_feature {
    const char *name;
    const char *detail;
    enum rf_render_support cpu, scene;
};

static const char *rf_render_baseline[] = {
    "相机、天空与画面层级",
    "地图与静态环境",
    "静态世界光照与材质",
    "透明物体与深度顺序",
    "动态物件与交互提示",
    "玩家、队友与武器",
    "感染体与死亡表现",
    "特效与第一人称武器",
    "界面、菜单与地图标记"
};

static const struct rf_render_feature rf_render_features[] = {
    {"模型边线", "仅影响 CPU 模型；并非完整卡通描边",
     RF_RENDER_EXPERIMENTAL, RF_RENDER_UNSUPPORTED},
    {"实时阴影", "动态光源投射的阴影",
     RF_RENDER_UNIMPLEMENTED, RF_RENDER_UNIMPLEMENTED},
    {"物理材质", "基于物理的材质响应",
     RF_RENDER_UNIMPLEMENTED, RF_RENDER_UNIMPLEMENTED},
    {"全局光照 / 环境遮蔽", "间接光照与屏幕空间遮蔽",
     RF_RENDER_UNIMPLEMENTED, RF_RENDER_UNIMPLEMENTED}
};

#define RF_RENDER_FEATURE_COUNT ((int)(sizeof(rf_render_features) / sizeof(rf_render_features[0])))
#define RF_RENDER_BASELINE_COUNT ((int)(sizeof(rf_render_baseline) / sizeof(rf_render_baseline[0])))

struct rf_render_terminal_ui {
    int open, near, page, selected, pointer_x, pointer_y;
    int scene_backend, edge_requested, edge_effective;
    const char *message;
};
static struct rf_render_terminal_ui rf_render_terminal;

static enum rf_render_support rf_render_feature_support(int index)
{
    const struct rf_render_feature *feature = &rf_render_features[index];
    return rf_render_terminal.scene_backend ? feature->scene : feature->cpu;
}

static const char *rf_render_support_name(enum rf_render_support support)
{
    switch (support) {
    case RF_RENDER_FORMAL: return "正式支持";
    case RF_RENDER_EXPERIMENTAL: return "实验支持";
    case RF_RENDER_UNSUPPORTED: return "当前后端不支持";
    default: return "尚未实现";
    }
}

static void rf_render_terminal_init(int scene_backend, int edge_requested)
{
    memset(&rf_render_terminal, 0, sizeof(rf_render_terminal));
    rf_render_terminal.scene_backend = scene_backend != 0;
    rf_render_terminal.edge_requested = !scene_backend && edge_requested != 0;
    rf_render_terminal.edge_effective = rf_render_terminal.edge_requested;
    rf_render_terminal.message = "选择功能查看支持状态";
}

static int rf_render_terminal_near(const struct rasterfall_session *session,
                                   int x, int z)
{
    const struct rf_map_runtime_object *terminal;
    terminal = rf_map_runtime_find_object(&session->map_ops.runtime,
                                          "main_terminal");
    return terminal && x >= terminal->x - 750 && x <= terminal->x + 750 &&
           z >= terminal->z - 850 && z <= terminal->z + 850;
}

static void rf_render_terminal_request(int index)
{
    enum rf_render_support support;
    if (index < 0 || index >= RF_RENDER_FEATURE_COUNT) return;
    rf_render_terminal.selected = index;
    support = rf_render_feature_support(index);
    if (support == RF_RENDER_UNSUPPORTED) {
        rf_render_terminal.message = "当前渲染器不支持，设置未改变";
        return;
    }
    if (support == RF_RENDER_UNIMPLEMENTED) {
        rf_render_terminal.message = "功能尚未实现，设置未改变";
        return;
    }
    if (index == 0) {
        /* This feature has no resources to rebuild; apply before the next frame. */
        rf_render_terminal.edge_requested = !rf_render_terminal.edge_requested;
        rasterfall_render_set_edge_pass(rf_render_terminal.edge_requested);
        rf_render_terminal.edge_effective = rf_render_terminal.edge_requested;
        rf_render_terminal.message = rf_render_terminal.edge_effective ?
            "已开启实验性模型边线" : "已关闭模型边线";
    }
}

static void rf_render_terminal_draw(struct rasterfall_canvas *canvas)
{
    int x = (canvas->width - 820) / 2, y = (canvas->height - 500) / 2;
    char line[112];
    rasterfall_canvas_rect(canvas, x - 4, y - 4, 828, 508, 0x4AC8CC, 255);
    rasterfall_canvas_rect(canvas, x, y, 820, 500, 0x101C2C, 255);
    rasterfall_canvas_text(canvas, x + 28, y + 25,
                           "前哨站 / 渲染控制", 0xD6F3F3);
    snprintf(line, sizeof(line), "当前渲染器：%s",
             rf_render_terminal.scene_backend ? "GPU 场景" : "CPU 软件渲染");
    rasterfall_canvas_text(canvas, x + 28, y + 49, line, 0x88B9C3);
    rasterfall_canvas_rect(canvas, x + 25, y + 78, 370, 36,
                           rf_render_terminal.page == 0 ? 0x2A5663 : 0x203543, 255);
    rasterfall_canvas_rect(canvas, x + 405, y + 78, 390, 36,
                           rf_render_terminal.page == 1 ? 0x2A5663 : 0x203543, 255);
    rasterfall_canvas_text(canvas, x + 42, y + 89, "基础功能", 0xE3F8F4);
    rasterfall_canvas_text(canvas, x + 422, y + 89, "高级功能", 0xE3F8F4);
    if (rf_render_terminal.page == 0) {
        rasterfall_canvas_text(canvas, x + 32, y + 130,
            "CPU 与 GPU 场景的共同默认功能", 0x9FE8C8);
        for (int i = 0; i < RF_RENDER_BASELINE_COUNT; ++i)
            rasterfall_canvas_text(canvas, x + 44, y + 164 + i * 27,
                                   rf_render_baseline[i], 0xD6F3F3);
        rasterfall_canvas_text(canvas, x + 32, y + 421,
            "两种渲染器的观感一致性仍在验收", 0xDDA88E);
    } else {
        for (int i = 0; i < RF_RENDER_FEATURE_COUNT; ++i) {
            int row = y + 129 + i * 67;
            enum rf_render_support support = rf_render_feature_support(i);
            rasterfall_canvas_rect(canvas, x + 25, row, 770, 61,
                i == rf_render_terminal.selected ? 0x2A5663 : 0x203543, 255);
            snprintf(line, sizeof(line), "%s  /  %s",
                     rf_render_features[i].name, rf_render_support_name(support));
            rasterfall_canvas_text(canvas, x + 42, row + 10, line,
                support >= RF_RENDER_UNSUPPORTED ? 0xDDA88E : 0xE3F8F4);
            rasterfall_canvas_text(canvas, x + 42, row + 34,
                                   rf_render_features[i].detail, 0x88B9C3);
            if (i == 0 && support < RF_RENDER_UNSUPPORTED) {
                snprintf(line, sizeof(line), "请求：%s / 生效：%s",
                    rf_render_terminal.edge_requested ? "开" : "关",
                    rf_render_terminal.edge_effective ? "开" : "关");
            } else
                snprintf(line, sizeof(line), "请求：关 / 生效：关");
            rasterfall_canvas_text(canvas, x + 490, row + 34, line, 0x9FE8C8);
        }
        rasterfall_canvas_text(canvas, x + 32, y + 414,
                               rf_render_terminal.message, 0xDDA88E);
    }
    rasterfall_canvas_text(canvas, x + 28, y + 456,
        "点击标签或功能    1/2 切页    回车切换    Esc 退出",
        0xA7DDE0);
    rasterfall_canvas_rect(canvas, rf_render_terminal.pointer_x - 5,
        rf_render_terminal.pointer_y, 11, 1, 0xFFFFFF, 255);
    rasterfall_canvas_rect(canvas, rf_render_terminal.pointer_x,
        rf_render_terminal.pointer_y - 5, 1, 11, 0xFFFFFF, 255);
}
