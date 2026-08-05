#include "tlibc_everything.h"
#include "toy_assets.h"

static uint16_t a16(const unsigned char *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t a32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int range(uint32_t off, uint32_t len, uint32_t total) { return off <= total && len <= total - off; }
static unsigned char *read_asset(const char *path, uint32_t *size)
{
    int fd; struct stat st; unsigned char *p; int n, got = 0;
    if (!path || !size) return NULL;
    fd = openat(AT_FDCWD, path, O_RDONLY, 0); if (fd < 0 || fstat(fd, &st) < 0) return NULL;
    if (st.st_size < 0 || st.st_size > 64 * 1024 * 1024) { close(fd); return NULL; }
    p = (unsigned char *)tlibc_malloc((unsigned long)st.st_size); if (!p) { close(fd); return NULL; }
    while (got < st.st_size) { n = read(fd, p + got, (int)(st.st_size - got)); if (n <= 0) { tlibc_free(p); close(fd); return NULL; } got += n; }
    close(fd); *size = (uint32_t)got; return p;
}
static int header(const unsigned char *p, uint32_t n, const char *magic, uint16_t *hs)
{
    if (n < 8 || memcmp(p, magic, 4) != 0 || a16(p + 4) != TOY_ASSET_VERSION) return -1;
    *hs = a16(p + 6); return *hs >= 8 && *hs <= n;
}
int toy_texture_load(const char *path, struct toy_texture_asset *o)
{
    unsigned char *p; uint32_t n, off, len, w, h; uint16_t hs;
    if (!o) return -1; memset(o, 0, sizeof(*o)); p = read_asset(path, &n); if (!p || header(p,n,"TTEX",&hs)<0 || hs < 32 || n < 32) { if(p)tlibc_free(p); return -1; }
    w=a32(p+8); h=a32(p+12); off=a32(p+20); len=a32(p+24);
    if (!w || !h || a16(p+16)!=3 || a16(p+18)!=1 || w > 8192 || h > 8192 || off < hs || w > 0xffffffffu/(h*3u) || len != w*h*3u || !range(off,len,n)) { tlibc_free(p); return -1; }
    o->blob=p; o->data=p+off; o->width=w; o->height=h; o->data_size=len; return 0;
}
int toy_sound_load(const char *path, struct toy_sound_asset *o)
{
    unsigned char *p; uint32_t n, off, len, rate, frames; uint16_t hs, ch, bits;
    if (!o) return -1; memset(o,0,sizeof(*o)); p=read_asset(path,&n); if(!p || header(p,n,"TSND",&hs)<0 || hs<32 || n<32){if(p)tlibc_free(p);return -1;}
    rate=a32(p+8); ch=a16(p+12); bits=a16(p+14); frames=a32(p+16); off=a32(p+20); len=a32(p+24);
    if(!rate || ch<1 || ch>2 || bits!=16 || !frames || frames>0xffffffffu/(ch*2u) || len!=frames*ch*2u || !range(off,len,n)){tlibc_free(p);return -1;}
    o->blob=p;o->data=p+off;o->rate=rate;o->channels=ch;o->frames=frames;o->data_size=len;return 0;
}
int toy_mesh_load(const char *path, struct toy_mesh_asset *o)
{
    unsigned char *p; uint32_t n, vo, io, vc, ic, vs, is, vbytes, ibytes; uint16_t hs;
    if(!o)return -1;memset(o,0,sizeof(*o));p=read_asset(path,&n);if(!p||header(p,n,"TMES",&hs)<0||hs<40||n<40){if(p)tlibc_free(p);return -1;}
    vc=a32(p+8);ic=a32(p+12);vs=a32(p+16);is=a32(p+20);vo=a32(p+24);io=a32(p+28);
    if(!vc||!ic||vc>1048576||ic>3145728||vs!=16||is!=4||vc>0xffffffffu/vs||ic>0xffffffffu/is){tlibc_free(p);return -1;}
    vbytes=vc*vs;ibytes=ic*is;if(!range(vo,vbytes,n)||!range(io,ibytes,n)||vo<hs||io<vo||io-vo<vbytes){tlibc_free(p);return -1;}
    {uint32_t i;for(i=0;i<ic;i++)if(a32(p+io+i*4)>=vc){tlibc_free(p);return -1;}}
    o->blob=p;o->vertices=p+vo;o->indices=p+io;o->vertex_count=vc;o->index_count=ic;return 0;
}
void toy_texture_unload(struct toy_texture_asset *o) { if(o && o->blob) tlibc_free((void *)o->blob); if(o) memset(o,0,sizeof(*o)); }
void toy_sound_unload(struct toy_sound_asset *o) { if(o && o->blob) tlibc_free((void *)o->blob); if(o) memset(o,0,sizeof(*o)); }
void toy_mesh_unload(struct toy_mesh_asset *o) { if(o && o->blob) tlibc_free((void *)o->blob); if(o) memset(o,0,sizeof(*o)); }
