#include "tlibc_everything.h"
#include "rf_rts.h"
#include "rasterfall_character.h"
#include "rasterfall_model.h"

/* Small immutable wire masks derived from the actual selected body's bind
 * mesh. No second animation evaluator, framebuffer upload or retained model.
 * The UI tints these masks with live health and scales them into each card. */
struct rts_portrait_cache {
    const char *path;
    unsigned char mask[RF_RTS_PORTRAIT_W*RF_RTS_PORTRAIT_H];
};
static struct rts_portrait_cache portraits[16];
static unsigned portrait_next;
static const char *portrait_path(const struct toy_game_actor *a)
{
    const struct rasterfall_character_profile *p=rasterfall_character_profile(a->character_id);
    const struct rasterfall_character_visual_recipe *recipe=rasterfall_character_visual_recipe_for_character(a->character_id);
    if(recipe)return rasterfall_character_body_resource_path(recipe->body_resource_id);
    if(p && p->model_path)return p->model_path;
    return rasterfall_character_body_resource_path(RASTERFALL_BODY_RF_BLOCK);
}
const unsigned char *rf_rts_portrait(const struct toy_game_actor *a)
{
    const char *path=portrait_path(a);
    for(unsigned i=0;i<16;++i)
        if(portraits[i].path && !strcmp(portraits[i].path,path))return portraits[i].mask;
    return NULL;
}
static void portrait_line(unsigned char *mask,int x0,int y0,int x1,int y1)
{
    int dx=abs(x1-x0),dy=-abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,e=dx+dy;
    for(;;) {
        if(x0>=0 && x0<RF_RTS_PORTRAIT_W && y0>=0 && y0<RF_RTS_PORTRAIT_H)
            mask[y0*RF_RTS_PORTRAIT_W+x0]=1;
        if(x0==x1 && y0==y1)break;
        int e2=e*2;if(e2>=dy){e+=dy;x0+=sx;}if(e2<=dx){e+=dx;y0+=sy;}
    }
}
static void portrait_prepare(const struct toy_game_actor *a)
{
    struct rasterfall_model_asset asset={0};
    if(rf_rts_portrait(a))return;
    struct rts_portrait_cache *out=&portraits[portrait_next++%16];
    out->path=portrait_path(a);memset(out->mask,0,sizeof(out->mask));
    if(rasterfall_model_load(&asset,out->path)<0)return;
    double minx=1e30,miny=1e30,maxx=-1e30,maxy=-1e30;
    for(unsigned i=0;i<asset.vertex_count;++i) {
        int v[3];memcpy(v,asset.vertices+i*asset.vertex_bytes,sizeof(v));
        double x=v[0]*.92+v[2]*.38,y=v[1];
        if(x<minx)minx=x;
        if(x>maxx)maxx=x;
        if(y<miny)miny=y;
        if(y>maxy)maxy=y;
    }
    double scale=(RF_RTS_PORTRAIT_W-8)/(maxx-minx+1);
    double ys=(RF_RTS_PORTRAIT_H-8)/(maxy-miny+1);if(ys<scale)scale=ys;
    for(unsigned i=0;i+2<asset.index_count;i+=3) {
        int x[3],y[3],valid=1;
        for(int n=0;n<3;++n) {
            unsigned index;int v[3];memcpy(&index,asset.indices+4*(i+n),4);
            if(index>=asset.vertex_count){valid=0;break;}
            memcpy(v,asset.vertices+index*asset.vertex_bytes,sizeof(v));
            x[n]=RF_RTS_PORTRAIT_W/2+(int)((v[0]*.92+v[2]*.38-(minx+maxx)*.5)*scale);
            y[n]=RF_RTS_PORTRAIT_H-4-(int)((v[1]-miny)*scale);
        }
        if(valid)for(int n=0;n<3;++n)portrait_line(out->mask,x[n],y[n],x[(n+1)%3],y[(n+1)%3]);
    }
    /* Dense production meshes have subpixel triangles at HUD size. Keep
     * their contour and a readable scan grid instead of a solid bright blob;
     * sparse edges from the Block body remain fully visible. */
    unsigned char edges[RF_RTS_PORTRAIT_W*RF_RTS_PORTRAIT_H];
    memcpy(edges,out->mask,sizeof(edges));
    for(int y=1;y<RF_RTS_PORTRAIT_H-1;++y)for(int x=1;x<RF_RTS_PORTRAIT_W-1;++x) {
        int k=y*RF_RTS_PORTRAIT_W+x;
        if(!edges[k])continue;
        int contour=!edges[k-1] || !edges[k+1] || !edges[k-RF_RTS_PORTRAIT_W] || !edges[k+RF_RTS_PORTRAIT_W];
        out->mask[k]=contour?235:(y%10==0 || (x+y/3)%9==0)?175:45;
    }
    rasterfall_model_unload(&asset);
}
void rf_rts_portraits_prepare(const struct rf_rts_state *s,const struct toy_game *g)
{
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)
        if(rf_rts_member_valid(&s->selected[i],g,i))portrait_prepare(&g->actors[i]);
}
