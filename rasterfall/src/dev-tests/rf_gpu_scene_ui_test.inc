struct scene_ui_test_sink { unsigned calls; uint64_t hash; int fail; };
static int scene_ui_test_rect(void *context,int x,int y,int w,int h,uint32_t color,int alpha)
{
    struct scene_ui_test_sink *sink=context;
    if (sink->fail || x<0 || y<0 || w<=0 || h<=0 || x+w>1280 || y+h>720)
        return -1;
    unsigned values[6]={(unsigned)x,(unsigned)y,(unsigned)w,(unsigned)h,color,(unsigned)alpha};
    for (int i=0;i<6;++i) sink->hash=(sink->hash^values[i])*1099511628211ULL;
    sink->calls++;
    return 0;
}
static int scene_ui_test(void)
{
    struct scene_ui_test_sink sink={0};
    struct rasterfall_canvas canvas={1280,720,0,&sink,scene_ui_test_rect};
    struct control_settings settings={0};
    struct pause_menu menu={0};
    struct rasterfall_options options;
    char *args[]={"rasterfall","--gpu-scene-play"};
    static struct toy_game before;
    memcpy(&before,&game,sizeof(before));
    rasterfall_options_init(&options,1);
    if (rasterfall_options_parse(&options,2,args) || !options.gpu_required ||
        !options.gpu_native_present || !options.gpu_scene_independent_preview ||
        options.gpu_normal_view || options.gpu_normal_fixed_tick) return 1;
    draw_pause_overlay(&canvas,&menu,&settings,0);
    uint64_t initial=sink.hash;
    if (canvas.failed || !sink.calls) return 2;
    memset(&sink,0,sizeof(sink));settings.mouse_level=7;
    draw_pause_overlay(&canvas,&menu,&settings,0);
    if (canvas.failed || sink.hash==initial) return 3;
    struct rasterfall_net net={0};
    draw_scoreboard(&canvas,&net);
    draw_game_over_panel(&canvas,0);draw_level_won_panel(&canvas,0);
    draw_crosshair(&canvas,&game);
    if (canvas.failed || memcmp(&before,&game,sizeof(before))) return 4;
    sink.fail=1;draw_pause_overlay(&canvas,&menu,&settings,0);
    if (!canvas.failed) return 5;
    __printf("scene UI layouts: read-only, settings, scoreboard, result, failure PASS\n");
    return 0;
}
