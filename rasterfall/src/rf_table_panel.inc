/* Shared layout for input, overlay and the GPU preview rectangle. */
struct rf_table_layout {int x,y,w,h,scale;struct rf_ui_rect field,video,deploy,close;};
static struct rf_table_layout rf_table_layout(int w,int h)
{
    struct rf_table_layout o={0};
    o.scale=h*1000/720;if(o.scale<650)o.scale=650;if(o.scale>1800)o.scale=1800;
    o.w=1100*o.scale/1000;if(o.w>w-32)o.w=w-32;
    o.h=610*o.scale/1000;if(o.h>h-32)o.h=h-32;
    o.x=(w-o.w)/2;o.y=(h-o.h)/2;
    int pad=24*o.scale/1000,left=o.w*34/100;
    o.field=(struct rf_ui_rect){o.x+pad,o.y+110*o.scale/1000,left-pad,44*o.scale/1000};
    o.video=(struct rf_ui_rect){o.x+left+pad,o.field.y,o.w-left-pad*2,0};
    o.video.h=o.video.w*9/16;
    o.deploy=(struct rf_ui_rect){o.x+pad,o.y+o.h-90*o.scale/1000,left-pad,44*o.scale/1000};
    o.close=(struct rf_ui_rect){o.x+o.w-100*o.scale/1000,o.y+20*o.scale/1000,76*o.scale/1000,34*o.scale/1000};
    return o;
}
static int rf_table_hit(int w,int h,int px,int py)
{
    struct rf_table_layout o=rf_table_layout(w,h);
    if(rf_ui_rect_contains(o.close,px,py))return 21;
    if(rf_ui_rect_contains(o.field,px,py))return 20;
    if(rf_table.dropdown) {
        int row=38*o.scale/1000,top=o.field.y+o.field.h+4;
        if(px>=o.field.x && px<o.field.x+o.field.w && py>=top && py<top+RF_TABLE_MAPS*row)
            return (py-top)/row;
    }
    if(rf_ui_rect_contains(o.deploy,px,py))return 22;
    if(rf_ui_rect_contains(o.video,px,py))return 23;
    return -1;
}
static void rf_table_input(struct rf_game_runtime *r,int w,int h,int px,int py,
    int click,int cancel,int up,int down,int confirm)
{
#ifndef TOYC_WINDOWS
    rf_table.preview_state=4;rf_table.preview_map=rf_table.selected;
#endif
    int hit=rf_table_hit(w,h,px,py);
    rf_table.pointer_x=px;rf_table.pointer_y=py;
    if(hit!=rf_table.hover){if(hit>=0)rasterfall_audio_ui(&r->audio,RF_UI_SOUND_HOVER);rf_table.hover=hit;}
    if(cancel){if(rf_table.dropdown)rf_table.dropdown=0;else rf_table.open=0;
        rasterfall_audio_ui(&r->audio,RF_UI_SOUND_EXIT);return;}
    if(up||down) {
        if(rf_table.dropdown)rf_table.focus=(rf_table.focus+(down?1:RF_TABLE_MAPS-1))%RF_TABLE_MAPS;
        else if(down)rf_table.focus=rf_table.focus==20?23:rf_table.focus==23?22:20;
        else rf_table.focus=rf_table.focus==22?23:rf_table.focus==23?20:22;
        rasterfall_audio_ui(&r->audio,RF_UI_SOUND_HOVER);
    }
    if(confirm)hit=rf_table.dropdown?rf_table.focus:rf_table.focus==22?22:rf_table.focus==23?23:20;
    else if(!click)return;
    if(hit==20){rf_table.dropdown=!rf_table.dropdown;rf_table.focus=rf_table.selected;}
    else if(hit==21){rf_table.open=0;rasterfall_audio_ui(&r->audio,RF_UI_SOUND_EXIT);return;}
    else if(hit>=0 && hit<RF_TABLE_MAPS && rf_table.dropdown) {
        if(rf_table.maps[hit].available) {
            int changed=hit!=rf_table.selected;rf_device_table_request(hit,0);
            if(changed || rf_table.preview_state<0) {
                rf_table.preview_state=0;rf_table.preview_requested=0;
                snprintf(rf_table.message,sizeof(rf_table.message),"点击右侧预览画面框开始加载，完成后才能部署");
            }
            rasterfall_audio_ui(&r->audio,changed?RF_UI_SOUND_CHANGE:RF_UI_SOUND_CLICK);
        }
        rf_table.dropdown=0;rf_table.focus=20;return;
    } else if(hit==23) {
        rf_table.dropdown=0;rf_table.focus=23;
        if(rf_table.preview_state<=0 && rf_table.maps[rf_table.selected].available) {
            rf_table.preview_state=0;rf_table.preview_requested=1;
            snprintf(rf_table.message,sizeof(rf_table.message),"已请求预览，请等待地图加载与首帧渲染完成");
        }
    } else if(hit==22) {
        if(rf_table.preview_state>=3 && rf_table.preview_map==rf_table.selected) {
            rf_device_table_request(rf_table.selected,1);rasterfall_audio_ui(&r->audio,RF_UI_SOUND_CONFIRM);
        } else snprintf(rf_table.message,sizeof(rf_table.message),rf_table.preview_state<=0?
            "请先点击右侧预览画面框，加载完成后才能部署":"请等待出生点预览加载完成");
        return;
    } else rf_table.dropdown=0;
    rasterfall_audio_ui(&r->audio,RF_UI_SOUND_CLICK);
}
static void rf_table_text(struct rasterfall_canvas *c,int x,int y,int w,int h,int scale,const char *text,unsigned color)
{rf_ui_text(c,(struct rf_ui_rect){x,y,w,h},text,color,scale,2);}
static void rf_table_draw(struct rasterfall_canvas *c)
{
    struct rf_table_layout o=rf_table_layout(c->width,c->height);struct rf_ui_rect v=o.video;
    int s=o.scale,pad=24*s/1000,ready=rf_table.preview_state==3&&rf_table.preview_map==rf_table.selected;
    unsigned bg=0x101F2D;
    /* Leave the GPU image hole uncovered by the overlay. */
    rasterfall_canvas_rect(c,o.x,o.y,o.w,v.y-o.y,bg,255);
    rasterfall_canvas_rect(c,o.x,v.y,v.x-o.x,v.h,bg,255);
    rasterfall_canvas_rect(c,v.x+v.w,v.y,o.x+o.w-v.x-v.w,v.h,bg,255);
    rasterfall_canvas_rect(c,o.x,v.y+v.h,o.w,o.y+o.h-v.y-v.h,bg,255);
    rasterfall_canvas_rect(c,o.x,o.y,o.w,2,0x55CCCF,255);
    rf_table_text(c,o.x+pad,o.y+24*s/1000,o.w-pad*5,40*s/1000,s,"前哨站 / 行动部署",0xE1F6F3);
    rf_table_text(c,o.x+pad,o.y+68*s/1000,o.w-pad*2,30*s/1000,s,"选择目的地 · 点击画面框预览 · 确认后进入",0x8FB5C5);
    struct rf_ui_rect f=o.field;
    rasterfall_canvas_rect(c,f.x,f.y,f.w,f.h,rf_table.hover==20?0x37616C:0x223F50,255);
    rf_table_text(c,f.x+10,f.y+8,f.w-36,f.h-8,s,rf_table.maps[rf_table.selected].name,0xDBF2EF);
    rf_table_text(c,f.x+f.w-28,f.y+8,24,f.h,s,rf_table.dropdown?"^":"v",0x80DFD3);
    rf_table_text(c,o.close.x,o.close.y,o.close.w,o.close.h,s,"关闭",rf_table.hover==21?0xFFFFFF:0x8FB5C5);
    if(!ready) {
        rasterfall_canvas_rect(c,v.x,v.y,v.w,v.h,rf_table.preview_state<=0&&rf_table.hover==23?0x223F50:0x172F40,255);
        const char *state=rf_table.preview_state==4?"当前后端不支持 GPU 预览\n仍可确认部署":rf_table.preview_state<0?"预加载失败\n点击此画面框重试":
            rf_table.preview_state==0?"点击此预览画面框\n开始预加载并渲染出生点画面":
            rf_table.preview_state==2?(rf_table.preview_percent>=75?
                "正在渲染出生点首帧…\n完成后才能确认部署":"正在准备场景与模型资源…\n完成后才能确认部署"):
                "加载中…\n正在准备地图与资源，完成后才能部署";
        rf_table_text(c,v.x+pad,v.y+v.h/2-30*s/1000,v.w-pad*2,90*s/1000,s,state,0x9ED5DF);
        if(rf_table.preview_state==1 || rf_table.preview_state==2) {
            int bar_x=v.x+pad,bar_y=v.y+v.h-54*s/1000,bar_w=v.w-pad*2;
            char progress[80];
            snprintf(progress,sizeof(progress),"加载进度（估算） %d%%",rf_table.preview_percent);
            rf_table_text(c,bar_x,bar_y-28*s/1000,bar_w,24*s/1000,s,progress,0xB7DDD9);
            rasterfall_canvas_rect(c,bar_x,bar_y,bar_w,8*s/1000,0x294653,255);
            rasterfall_canvas_rect(c,bar_x,bar_y,bar_w*rf_table.preview_percent/100,8*s/1000,0x76E5CD,255);
        }
    }
    rf_table_text(c,v.x,v.y+v.h+16*s/1000,v.w,70*s/1000,s,
        ready?"出生点 · 首帧已冻结\n地图已预加载，等待确认部署":"预览不会推进目标地图的对局与任务",0x8FB5C5);
    if(!rf_table.dropdown)rf_table_text(c,f.x,f.y+f.h+26*s/1000,f.w,o.deploy.y-f.y-f.h-30,s,
        "预览展示实际地图画面。\n\n切换目的地会释放上一份预加载。\n关闭界面返回大厅。",0x91ADB9);
    struct rf_ui_rect d=o.deploy;
    int deployable=ready||rf_table.preview_state==4;
    rasterfall_canvas_rect(c,d.x,d.y,d.w,d.h,deployable?(rf_table.hover==22?0x438077:0x285C58):0x243B49,255);
    rf_table_text(c,d.x+12,d.y+9,d.w-24,d.h,s,deployable?"确认部署  >":rf_table.preview_state<=0?"请先点击预览":"等待预加载",deployable?0xD7FFF0:0x758F9B);
    rf_table_text(c,o.x+pad,o.y+o.h-32*s/1000,o.w-pad*2,28*s/1000,s,rf_table.message,0xB7CDD2);
    if(rf_table.dropdown) {
        int row=38*s/1000,top=f.y+f.h+4;
        for(int i=0;i<RF_TABLE_MAPS;++i) {
            int hovered=rf_table.hover==i||rf_table.focus==i;
            rasterfall_canvas_rect(c,f.x,top+i*row,f.w,row-1,hovered?0x3C6872:0x213B4B,255);
            if(i==rf_table.selected)rasterfall_canvas_rect(c,f.x,top+i*row,3,row-1,0x76E5CD,255);
            rf_table_text(c,f.x+12,top+i*row+7,f.w-20,row,s,rf_table.maps[i].name,
                rf_table.maps[i].available?0xE0F3F2:0x71838E);
        }
    }
    rasterfall_canvas_rect(c,rf_table.pointer_x-4,rf_table.pointer_y,9,1,0xFFFFFF,255);
    rasterfall_canvas_rect(c,rf_table.pointer_x,rf_table.pointer_y-4,1,9,0xFFFFFF,255);
}
