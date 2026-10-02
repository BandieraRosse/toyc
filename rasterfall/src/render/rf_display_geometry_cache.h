#ifndef RF_DISPLAY_GEOMETRY_CACHE_H
#define RF_DISPLAY_GEOMETRY_CACHE_H
#include "rasterfall_lab_terminal.h"
#include <stdlib.h>
#include <string.h>

/* Owner-scoped, camera-independent geometry. A full value key includes text,
 * style, pixel pitch, facing, color and world placement. Animated beams and
 * beacons deliberately stay in their time-dependent emitters. */
#define RF_DISPLAY_QUAD_BUDGET 65536u
struct rf_display_quad { int p[4][3]; unsigned color; };
struct rf_display_geometry {
    struct toy_map_draw key;
    struct rf_display_quad *quads;
    unsigned count,capacity;
    int valid,min[3],max[3];
};
struct rf_display_build {
    struct rf_display_geometry *entry;
    unsigned *budget;
};
static int rf_display_generate(const struct toy_map_draw *d,rf_terminal_quad emit,void *context)
{
    if(rf_lab_projection_text_emit(d,emit,context)<0) return -1;
    return d->style==4 || d->style==5 ? rf_lab_terminal_emit(d,emit,context) : 0;
}
static int rf_display_store(void *context,const int p[4][3],unsigned color)
{
    struct rf_display_build *b=context;
    struct rf_display_geometry *e=b->entry;
    if(e->count==e->capacity) {
        unsigned grow=256;
        if(*b->budget>RF_DISPLAY_QUAD_BUDGET-grow) return -1;
        void *next=realloc(e->quads,(size_t)(e->capacity+grow)*sizeof(*e->quads));
        if(!next) return -1;
        e->quads=next;e->capacity+=grow;*b->budget+=grow;
    }
    memcpy(e->quads[e->count].p,p,sizeof(e->quads[e->count].p));
    e->quads[e->count].color=color;
    for(int axis=0;axis<3;++axis) for(int k=0;k<4;++k) {
        if(!e->count && !k) e->min[axis]=e->max[axis]=p[k][axis];
        if(p[k][axis]<e->min[axis]) e->min[axis]=p[k][axis];
        if(p[k][axis]>e->max[axis]) e->max[axis]=p[k][axis];
    }
    e->count++;
    return 0;
}
/* Budget/allocation failure is a cache miss: the caller emits directly. */
static int rf_display_geometry_prepare(struct rf_display_geometry *e,
    const struct toy_map_draw *d,unsigned *budget)
{
    if(e->valid && !memcmp(&e->key,d,sizeof(*d))) return e->valid>0?0:-1;
    e->valid=0;e->count=0;
    struct rf_display_build build={e,budget};
    if(rf_display_generate(d,rf_display_store,&build)<0) {e->key=*d;e->valid=-1;return -1;}
    e->key=*d;e->valid=1;
    return 0;
}
static int rf_display_geometry_emit(const struct rf_display_geometry *e,
    rf_terminal_quad emit,void *context)
{
    for(unsigned i=0;i<e->count;++i)
        if(emit(context,e->quads[i].p,e->quads[i].color)<0) return -1;
    return 0;
}
#endif
