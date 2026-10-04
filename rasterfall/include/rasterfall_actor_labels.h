#ifndef RASTERFALL_ACTOR_LABELS_H
#define RASTERFALL_ACTOR_LABELS_H
/* Shared presentation-only friendly world-label placement and canvas paint. */
#include "rasterfall_canvas.h"
#include <stdint.h>
#define RF_ACTOR_LABEL_CAPACITY 64U
#define RF_ACTOR_LABEL_TEXT_BYTES 64U
struct rf_actor_label {
    int actor_id;
    uint32_t generation;
    int anchor_x,anchor_y,x,y,width,height,text_width;
    int hp_fill,revive_fill,downed,moved;
    uint32_t name_color,hp_color,panel_color;
    char name[RF_ACTOR_LABEL_TEXT_BYTES];
};
struct rf_actor_labels {
    struct rf_actor_label entry[RF_ACTOR_LABEL_CAPACITY];
    unsigned order[RF_ACTOR_LABEL_CAPACITY],count,rect_tests;
};
static inline int rf_actor_label_clamp(int v,int lo,int hi)
{ return v<lo?lo:v>hi?hi:v; }
/* Caller already applied its existing friendly, visibility and projection
 * gates. No Game, session, camera, resource, input or clock reads here. */
static inline int rf_actor_labels_add(struct rf_actor_labels *labels,
    int actor_id,uint32_t generation,int x,int y,const char *name,
    int hp_fill,int downed,int revive_fill,
    uint32_t name_color,uint32_t hp_color,uint32_t panel_color)
{
    if(labels->count>=RF_ACTOR_LABEL_CAPACITY)return 0;
    struct rf_actor_label *e=&labels->entry[labels->count++];
    unsigned bytes=0;
    /* Copy whole UTF-8 sequences so a CPU caller's local demo-name buffer
     * cannot outlive its iteration or be cut inside a multibyte glyph. */
    while(name && *name) {
        const char *next=name;
        rasterfall_canvas_codepoint(&next);
        unsigned n=(unsigned)(next-name);
        if(bytes+n>=RF_ACTOR_LABEL_TEXT_BYTES)break;
        for(unsigned i=0;i<n;++i)e->name[bytes++]=name[i];
        name=next;
    }
    e->name[bytes]=0;
    e->actor_id=actor_id;e->generation=generation;
    e->anchor_x=x;e->anchor_y=y;
    e->text_width=rasterfall_canvas_text_width(e->name,1000);
    e->width=e->text_width>68?e->text_width:68;
    e->height=downed?49:25;e->downed=downed!=0;e->moved=0;
    e->hp_fill=rf_actor_label_clamp(hp_fill,0,64);
    e->revive_fill=rf_actor_label_clamp(revive_fill,0,64);
    e->name_color=name_color;e->hp_color=hp_color;e->panel_color=panel_color;
    return 1;
}
static inline int rf_actor_label_before(const struct rf_actor_label *a,
    const struct rf_actor_label *b)
{
    if(a->actor_id!=b->actor_id)return a->actor_id<b->actor_id;
    if(a->generation!=b->generation)return a->generation<b->generation;
    /* Malformed duplicate identity still has a deterministic presentation
     * tie-break; normal actor identities are unique. */
    if(a->anchor_y!=b->anchor_y)return a->anchor_y<b->anchor_y;
    return a->anchor_x<b->anchor_x;
}
static inline void rf_actor_labels_place(struct rf_actor_labels *labels,int width,int height)
{
    static const int offsets[9][2]={
        {0,0},{-1,0},{1,0},{0,-1},{-1,-1},{1,-1},{0,-2},{-1,-2},{1,-2}
    };
    labels->rect_tests=0;
    for(unsigned i=0;i<labels->count;++i) {
        unsigned at=i;
        while(at && rf_actor_label_before(&labels->entry[i],
                &labels->entry[labels->order[at-1]])) {
            labels->order[at]=labels->order[at-1];--at;
        }
        labels->order[at]=i;
    }
    for(unsigned i=0;i<labels->count;++i) {
        struct rf_actor_label *e=&labels->entry[labels->order[i]];
        int origin_x=e->anchor_x-e->width/2,origin_y=e->anchor_y;
        int step_x=e->width+4;if(step_x>128)step_x=128;
        int64_t best=-1;
        int best_x=origin_x,best_y=origin_y;
        for(unsigned candidate=0;candidate<9;++candidate) {
            int x=origin_x+offsets[candidate][0]*step_x;
            int y=origin_y+offsets[candidate][1]*(e->height+4);
            /* Ordinary eligible anchors remain visible; at edges, clamp
             * the complete block instead of dropping a wounded friend. */
            x=rf_actor_label_clamp(x,0,width>e->width?width-e->width:0);
            y=rf_actor_label_clamp(y,0,height>e->height?height-e->height:0);
            int64_t overlap=0;
            for(unsigned j=0;j<i;++j) {
                const struct rf_actor_label *p=&labels->entry[labels->order[j]];
                int left=x>p->x-2?x:p->x-2;
                int top=y>p->y-2?y:p->y-2;
                int right=x+e->width<p->x+p->width+2?x+e->width:p->x+p->width+2;
                int bottom=y+e->height<p->y+p->height+2?y+e->height:p->y+p->height+2;
                ++labels->rect_tests;
                if(right>left && bottom>top)overlap+=(int64_t)(right-left)*(bottom-top);
            }
            if(best<0 || overlap<best) {best=overlap;best_x=x;best_y=y;}
            if(!overlap)break;
        }
        e->x=best_x;e->y=best_y;
        e->moved=e->x!=origin_x || e->y!=origin_y;
    }
}
static inline void rf_actor_labels_paint(struct rf_actor_labels *labels,
    struct rasterfall_canvas *canvas)
{
    /* Draw all short connectors first, so they cannot overwrite label text.
     * Two rectangle runs plus one dot per moved entry, no pixel-line loop. */
    for(unsigned i=0;i<labels->count;++i) {
        const struct rf_actor_label *e=&labels->entry[labels->order[i]];
        if(!e->moved)continue;
        int end_x=rf_actor_label_clamp(e->anchor_x,0,canvas->width-1);
        int end_y=rf_actor_label_clamp(e->anchor_y,0,canvas->height-1);
        int x=rf_actor_label_clamp(end_x,e->x,e->x+e->width-1);
        int y=rf_actor_label_clamp(end_y,e->y,e->y+e->height-1);
        int left=x<end_x?x:end_x,top=y<end_y?y:end_y;
        /* Candidate displacement is bounded; the edge connector is shorter
         * still. Extreme offscreen inputs remain the projection owner's job. */
        rasterfall_canvas_rect(canvas,left,y,(x>end_x?x-end_x:end_x-x)+1,1,e->name_color,128);
        rasterfall_canvas_rect(canvas,end_x,top,1,(y>end_y?y-end_y:end_y-y)+1,e->name_color,128);
        rasterfall_canvas_rect(canvas,end_x-1,end_y-1,3,3,e->name_color,180);
    }
    for(unsigned i=0;i<labels->count;++i) {
        const struct rf_actor_label *e=&labels->entry[labels->order[i]];
        int center=e->x+e->width/2;
        rasterfall_canvas_text(canvas,center-e->text_width/2,e->y,e->name,e->name_color);
        rasterfall_canvas_rect(canvas,center-34,e->y+18,68,7,e->panel_color,255);
        rasterfall_canvas_rect(canvas,center-32,e->y+20,e->hp_fill,3,e->hp_color,255);
        if(e->downed) {
            rasterfall_canvas_text(canvas,center-24,e->y+29,"DOWNED",e->name_color);
            rasterfall_canvas_rect(canvas,center-32,e->y+46,e->revive_fill,3,0xffd060,255);
        }
    }
}
#endif
