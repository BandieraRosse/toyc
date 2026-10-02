#ifndef RF_SCENE_ID_SET_H
#define RF_SCENE_ID_SET_H
#include "toy_map.h"
#include <stdint.h>
#include <string.h>
/* Exact collision resolution; hashing changes work, never ID semantics. */
#define RF_SCENE_ID_LIMIT (TOY_MAP_MAX_DRAW>TOY_MAP_MAX_PROPS?TOY_MAP_MAX_DRAW:TOY_MAP_MAX_PROPS)
static int rf_scene_ids_unique(const void *items,unsigned count,size_t stride,unsigned text_capacity)
{
    unsigned slots[RF_SCENE_ID_LIMIT*2]={0};
    if(count>RF_SCENE_ID_LIMIT) return -1;
    for(unsigned i=0;i<count;++i) {
        const char *id=(const char *)items+(size_t)i*stride;
        uint32_t hash=2166136261u;unsigned n=0;
        for(;n<text_capacity && id[n];++n) hash=(hash^(unsigned char)id[n])*16777619u;
        if(!n || n==text_capacity) return -1;
        unsigned slot=hash%(RF_SCENE_ID_LIMIT*2);
        while(slots[slot]) {
            const char *previous=(const char *)items+(size_t)(slots[slot]-1)*stride;
            if(!strcmp(previous,id)) return -1;
            slot=(slot+1)%(RF_SCENE_ID_LIMIT*2);
        }
        slots[slot]=i+1;
    }
    return 0;
}
#endif
