#include "tlibc_everything.h"
#include "toy_map.h"

static int number(const char *s, int base) { return (int)strtol(s, NULL, base); }
static char *word(char **p) { char *s = strtok_r(*p, " \t\r\n", p); return s; }
static int get4(char **p, int *a, int *b, int *c, int *d)
{
    char *s1=word(p), *s2=word(p), *s3=word(p), *s4=word(p);
    if (!s1 || !s2 || !s3 || !s4) return -1;
    *a=number(s1,10); *b=number(s2,10); *c=number(s3,10); *d=number(s4,10); return 0;
}
static int get2(char **p, int *a, int *b)
{
    char *s1=word(p), *s2=word(p); if(!s1||!s2)return -1;
    *a=number(s1,10); *b=number(s2,10); return 0;
}
static unsigned int color(char *s) { return s ? (unsigned int)strtol(s, NULL, 16) : 0; }
static void add_draw(struct toy_map *m, int type, int a, int b, int c, int d,
                     int e, int f, unsigned int col, const char *text)
{
    struct toy_map_draw *x;
    if (m->draw_count >= TOY_MAP_MAX_DRAW) return;
    x=&m->draw[m->draw_count++]; x->type=type; x->a=a; x->b=b; x->c=c; x->d=d;
    x->e=e; x->f=f; x->color=col; x->texture_u=0; x->texture_v=0; x->style=0; x->text[0]=0;
    if (text) strncpy(x->text, text, TOY_MAP_TEXT_SIZE-1);
}

int toy_map_load(const char *path, struct toy_map *m)
{
    int fd, n, got=0; struct stat st; char *line, *save, *kind;
    if (!m || !path) return -1;
    memset(m,0,sizeof(*m));
    fd=openat(AT_FDCWD,path,O_RDONLY,0); if(fd<0 || fstat(fd,&st)<0) return -1;
    if(st.st_size<=0 || st.st_size>256*1024){close(fd);return -1;}
    m->blob=(char *)tlibc_malloc((unsigned long)st.st_size+1);
    if(!m->blob){close(fd);return -1;}
    while(got<st.st_size){n=read(fd,m->blob+got,(int)st.st_size-got);if(n<=0){toy_map_unload(m);close(fd);return -1;}got+=n;}
    close(fd); m->blob[got]=0;
    for(line=strtok_r(m->blob,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){
        char *p=line,*s; int a,b,c,d;
        s=strchr(line,'#'); if(s)*s=0; kind=word(&p); if(!kind)continue;
        if(!strcmp(kind,"world") && get4(&p,&a,&b,&c,&d)==0){char *q=word(&p);if(q)m->room_limit=number(q,10);m->minx=a;m->maxx=b;m->minz=c;m->maxz=d;}
        else if(!strcmp(kind,"start") && get2(&p,&m->start_x,&m->start_z)==0){}
        else if(!strcmp(kind,"box") && get4(&p,&a,&b,&c,&d)==0){
            char *h=word(&p),*co=word(&p),*air=word(&p); if(!h||!co||m->box_count>=TOY_MAP_MAX_BOXES)continue;
            m->boxes[m->box_count].minx=a;m->boxes[m->box_count].maxx=b;m->boxes[m->box_count].minz=c;m->boxes[m->box_count].maxz=d;m->boxes[m->box_count].height=number(h,10);m->boxes[m->box_count].color=color(co);m->boxes[m->box_count].air=air&&(!strcmp(air,"air"));m->box_count++;
        } else if(!strcmp(kind,"safe") && get4(&p,&a,&b,&c,&d)==0 && m->safe_count<TOY_MAP_MAX_ZONES){m->safe_rooms[m->safe_count].minx=a;m->safe_rooms[m->safe_count].maxx=b;m->safe_rooms[m->safe_count].minz=c;m->safe_rooms[m->safe_count].maxz=d;m->safe_count++;}
        else if(!strcmp(kind,"spawn") && get4(&p,&a,&b,&c,&d)==0 && m->spawn_count<TOY_MAP_MAX_ZONES){char *co=word(&p);m->spawn_zones[m->spawn_count].box.minx=a;m->spawn_zones[m->spawn_count].box.maxx=b;m->spawn_zones[m->spawn_count].box.minz=c;m->spawn_zones[m->spawn_count].box.maxz=d;m->spawn_zones[m->spawn_count].color=color(co);m->spawn_count++;}
        else if(!strcmp(kind,"alarm") && get4(&p,&a,&b,&c,&d)==0){m->alarm_zone.minx=a;m->alarm_zone.maxx=b;m->alarm_zone.minz=c;m->alarm_zone.maxz=d;m->has_alarm=1;}
        else if(!strcmp(kind,"floor") && get4(&p,&a,&b,&c,&d)==0){char *co=word(&p);add_draw(m,TOY_MAP_DRAW_FLOOR,a,b,c,d,0,0,color(co),NULL);}
        else if(!strcmp(kind,"border") && get4(&p,&a,&b,&c,&d)==0){char *w=word(&p),*co=word(&p);if(w)add_draw(m,TOY_MAP_DRAW_BORDER,a,b,c,d,number(w,10),0,color(co),NULL);}
        else if(!strcmp(kind,"wall") && get4(&p,&a,&b,&c,&d)==0){char *h=word(&p),*co=word(&p);if(h)add_draw(m,TOY_MAP_DRAW_WALL,a,b,c,d,number(h,10),0,color(co),NULL);}
        else if(!strcmp(kind,"label") && get4(&p,&a,&b,&c,&d)==0){char *co=word(&p),*t=word(&p);add_draw(m,TOY_MAP_DRAW_LABEL,a,b,c,d,0,0,color(co),t);}
        else if(!strcmp(kind,"model") && get4(&p,&a,&b,&c,&d)==0){char *y0=word(&p),*y1=word(&p),*co=word(&p),*st=word(&p);if(y0&&y1){add_draw(m,TOY_MAP_DRAW_MODEL,a,b,c,d,number(y0,10),number(y1,10),color(co),NULL);if(st)m->draw[m->draw_count-1].style=number(st,10);}}
        else if(!strcmp(kind,"texture") && get4(&p,&a,&b,&c,&d)==0){char *y=word(&p),*u=word(&p),*v=word(&p),*co=word(&p);if(y&&u&&v){add_draw(m,TOY_MAP_DRAW_TEXTURE,a,b,c,d,number(y,10),0,color(co),NULL);m->draw[m->draw_count-1].texture_u=number(u,10);m->draw[m->draw_count-1].texture_v=number(v,10);}}
        else if(!strcmp(kind,"pickup") && m->pickup_count<TOY_MAP_MAX_PICKUPS){
            char *k=word(&p),*sx=word(&p),*sz=word(&p),*sy=word(&p);
            if(k&&sx&&sz&&sy){
                m->pickups[m->pickup_count].kind=
                    !strcmp(k,"smg")?TOY_MAP_PICKUP_SMG:
                    !strcmp(k,"shotgun")?TOY_MAP_PICKUP_SHOTGUN:TOY_MAP_PICKUP_AMMO;
                m->pickups[m->pickup_count].x=number(sx,10);
                m->pickups[m->pickup_count].z=number(sz,10);
                m->pickups[m->pickup_count].y=number(sy,10);
                m->pickup_count++;
            }
        }
        else if(!strcmp(kind,"button") && m->pickup_count<TOY_MAP_MAX_PICKUPS){
            char *sx=word(&p),*sz=word(&p),*sy=word(&p);
            if(sx&&sz&&sy){
                m->pickups[m->pickup_count].kind=TOY_MAP_PICKUP_BUTTON;
                m->pickups[m->pickup_count].x=number(sx,10);
                m->pickups[m->pickup_count].z=number(sz,10);
                m->pickups[m->pickup_count].y=number(sy,10);
                m->pickup_count++;
            }
        }
    }
    return m->room_limit>0 && m->box_count>0 ? 0 : (toy_map_unload(m),-1);
}
void toy_map_unload(struct toy_map *m){if(m&&m->blob)tlibc_free(m->blob);if(m)memset(m,0,sizeof(*m));}
