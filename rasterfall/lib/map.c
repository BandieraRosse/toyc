#include "tlibc_everything.h"
#include "toy_map.h"
#include "toy_assets.h"

static int number(const char *s, int base) { return (int)strtol(s, NULL, base); }
static char *word(char **p) { char *s = strtok_r(*p, " \t\r\n", p); return s; }
static char *rest_text(char **p)
{
    char *s, *end;
    if (!p || !*p) return NULL;
    s = *p;
    while (*s == ' ' || *s == '\t') s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
                       end[-1] == '\r' || end[-1] == '\n'))
        *--end = 0;
    *p = end;
    return *s ? s : NULL;
}
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
static int get5(char **p, int *a, int *b, int *c, int *d, int *e)
{
    char *s1=word(p), *s2=word(p), *s3=word(p), *s4=word(p), *s5=word(p);
    if(!s1||!s2||!s3||!s4||!s5)return -1;
    *a=number(s1,10); *b=number(s2,10); *c=number(s3,10);
    *d=number(s4,10); *e=number(s5,10); return 0;
}
static unsigned int color(char *s) { return s ? (unsigned int)strtol(s, NULL, 16) : 0; }
static void copy_role(char *out, const char *in, int size)
{
    if (!out || size <= 0) return;
    out[0] = 0;
    if (in) strncpy(out, in, (size_t)size - 1);
}
static int ai_class(char *s)
{
    if (!s) return TOY_GAME_AI_LEVEL_1;
    if (!strcmp(s, "level1")) return TOY_GAME_AI_LEVEL_1;
    if (!strcmp(s, "level2")) return TOY_GAME_AI_LEVEL_2;
    if (!strcmp(s, "level3")) return TOY_GAME_AI_LEVEL_3;
    return number(s, 10);
}
static void add_draw(struct toy_map *m, int type, int a, int b, int c, int d,
                     int e, int f, unsigned int col, const char *text)
{
    struct toy_map_draw *x;
    if (m->draw_count >= TOY_MAP_MAX_DRAW) return;
    x=&m->draw[m->draw_count++]; x->type=type; x->a=a; x->b=b; x->c=c; x->d=d;
    x->e=e; x->f=f; x->color=col; x->texture_u=0; x->texture_v=0; x->style=0; x->text[0]=0;
    if (text) {
        strncpy(x->text, text, TOY_MAP_TEXT_SIZE-1);
        x->text[TOY_MAP_TEXT_SIZE-1] = 0;
    }
}

int toy_map_load(const char *path, struct toy_map *m)
{
    uint32_t size; unsigned char *data; char *line, *save, *kind;
    if (!m || !path) return -1;
    memset(m,0,sizeof(*m));
    m->start_safe_index = -1;
    m->goal_safe_index = -1;
    m->alarm_spawn_zone = -1;
    m->start_cy = 1024;
    data = toy_asset_load_file(path, &size);
    if (!data || size == 0 || size > 256 * 1024) {
        if (data) tlibc_free(data);
        return -1;
    }
    m->blob = (char *)tlibc_malloc(size + 1);
    if (!m->blob) { tlibc_free(data); return -1; }
    memcpy(m->blob, data, size);
    tlibc_free(data);
    m->blob[size] = 0;
    for(line=strtok_r(m->blob,"\n",&save);line;line=strtok_r(NULL,"\n",&save)){
        char *p=line,*s; int a,b,c,d;
        s=strchr(line,'#'); if(s)*s=0; kind=word(&p); if(!kind)continue;
        if(!strcmp(kind,"world") && get4(&p,&a,&b,&c,&d)==0){char *q=word(&p);if(q)m->room_limit=number(q,10);m->minx=a;m->maxx=b;m->minz=c;m->maxz=d;}
        else if(!strcmp(kind,"start") && get2(&p,&m->start_x,&m->start_z)==0){char *sy=word(&p),*cy=word(&p);if(sy&&cy){m->start_sy=number(sy,10);m->start_cy=number(cy,10);}}
        else if(!strcmp(kind,"box") && get4(&p,&a,&b,&c,&d)==0){
            char *h=word(&p),*co=word(&p),*opt;
            struct toy_map_box *box;
            if(!h||!co||m->box_count>=TOY_MAP_MAX_BOXES)continue;
            box=&m->boxes[m->box_count++]; memset(box,0,sizeof(*box));
            box->minx=a; box->maxx=b; box->minz=c; box->maxz=d;
            box->height=number(h,10); box->color=color(co);
            box->visible=1; box->collision=1;
            while ((opt=word(&p)) != NULL) {
                if (!strcmp(opt,"air")) {
                    box->air=1; box->visible=0; box->collision=1;
                    copy_role(box->role, "air_gate", TOY_MAP_ROLE_SIZE);
                } else if (!strcmp(opt,"visible")) box->visible=1;
                else if (!strcmp(opt,"hidden")) box->visible=0;
                else if (!strcmp(opt,"collision")) box->collision=1;
                else if (!strcmp(opt,"nocollision")) box->collision=0;
                else if (!strncmp(opt,"role=",5))
                    copy_role(box->role, opt+5, TOY_MAP_ROLE_SIZE);
            }
        } else if(!strcmp(kind,"safe") && get4(&p,&a,&b,&c,&d)==0 && m->safe_count<TOY_MAP_MAX_ZONES){
            char *role=word(&p); int index=m->safe_count;
            m->safe_rooms[index].minx=a; m->safe_rooms[index].maxx=b;
            m->safe_rooms[index].minz=c; m->safe_rooms[index].maxz=d;
            m->safe_count++;
            if (role && !strcmp(role,"start")) m->start_safe_index=index;
            if (role && (!strcmp(role,"goal") || !strcmp(role,"exit")))
                m->goal_safe_index=index;
        } else if(!strcmp(kind,"base") && m->base_count<TOY_MAP_MAX_BASES){
            char *id=word(&p); struct toy_map_base *base;
            if (!id || get4(&p,&a,&b,&c,&d) < 0) continue;
            base=&m->bases[m->base_count++]; base->id=number(id,10);
            base->box.minx=a; base->box.maxx=b; base->box.minz=c; base->box.maxz=d;
        } else if(!strcmp(kind,"ai_spawn") && m->ai_spawn_count<TOY_MAP_MAX_AI_SPAWNS){
            char *name=word(&p), *base=word(&p), *class_name=word(&p);
            char *sx=word(&p), *sz=word(&p), *down=word(&p), *weapon_name=word(&p);
            struct toy_map_ai_spawn *spawn;
            if (!name || !base || !class_name || !sx || !sz) continue;
            spawn=&m->ai_spawns[m->ai_spawn_count++]; memset(spawn,0,sizeof(*spawn));
            spawn->weapon = -1;
            copy_role(spawn->name,name,TOY_MAP_ROLE_SIZE);
            spawn->base_id=number(base,10); spawn->class_id=ai_class(class_name);
            spawn->x=number(sx,10); spawn->z=number(sz,10);
            spawn->downed=down ? number(down,10) != 0 : 1;
            if (weapon_name) spawn->weapon = toy_game_weapon_from_name(weapon_name);
        }
        else if(!strcmp(kind,"spawn") && get4(&p,&a,&b,&c,&d)==0 && m->spawn_count<TOY_MAP_MAX_ZONES){char *co=word(&p);m->spawn_zones[m->spawn_count].box.minx=a;m->spawn_zones[m->spawn_count].box.maxx=b;m->spawn_zones[m->spawn_count].box.minz=c;m->spawn_zones[m->spawn_count].box.maxz=d;m->spawn_zones[m->spawn_count].color=color(co);m->spawn_count++;}
        else if(!strcmp(kind,"alarm") && get4(&p,&a,&b,&c,&d)==0){char *zone=word(&p);m->alarm_zone.minx=a;m->alarm_zone.maxx=b;m->alarm_zone.minz=c;m->alarm_zone.maxz=d;m->has_alarm=1;if(zone)m->alarm_spawn_zone=number(zone,10);}
        else if(!strcmp(kind,"floor") && get4(&p,&a,&b,&c,&d)==0){char *co=word(&p);add_draw(m,TOY_MAP_DRAW_FLOOR,a,b,c,d,0,0,color(co),NULL);}
        else if(!strcmp(kind,"border") && get4(&p,&a,&b,&c,&d)==0){char *w=word(&p),*co=word(&p);if(w)add_draw(m,TOY_MAP_DRAW_BORDER,a,b,c,d,number(w,10),0,color(co),NULL);}
        else if(!strcmp(kind,"wall") && get4(&p,&a,&b,&c,&d)==0){char *h=word(&p),*co=word(&p);if(h)add_draw(m,TOY_MAP_DRAW_WALL,a,b,c,d,number(h,10),0,color(co),NULL);}
        else if(!strcmp(kind,"label") && get4(&p,&a,&b,&c,&d)==0){char *co=word(&p),*t=word(&p);add_draw(m,TOY_MAP_DRAW_LABEL,a,b,c,d,0,0,color(co),t);}
        else if(!strcmp(kind,"sign") && get4(&p,&a,&b,&c,&d)==0){char *y0=word(&p),*y1=word(&p),*co=word(&p),*t=rest_text(&p);if(y0&&y1&&co)add_draw(m,TOY_MAP_DRAW_SIGN,a,b,c,d,number(y0,10),number(y1,10),color(co),t);}
        else if(!strcmp(kind,"model") && get4(&p,&a,&b,&c,&d)==0){char *y0=word(&p),*y1=word(&p),*co=word(&p),*st=word(&p);if(y0&&y1){add_draw(m,TOY_MAP_DRAW_MODEL,a,b,c,d,number(y0,10),number(y1,10),color(co),NULL);if(st)m->draw[m->draw_count-1].style=number(st,10);}}
        else if(!strcmp(kind,"platform") && m->platform_count<TOY_MAP_MAX_PLATFORMS){int h;if(get5(&p,&a,&b,&c,&d,&h)==0){struct toy_game_platform *pl=&m->platforms[m->platform_count++];pl->minx=a;pl->maxx=b;pl->minz=c;pl->maxz=d;pl->height=h;}}
        else if(!strcmp(kind,"texture") && get4(&p,&a,&b,&c,&d)==0){char *y=word(&p),*u=word(&p),*v=word(&p),*co=word(&p);if(y&&u&&v){add_draw(m,TOY_MAP_DRAW_TEXTURE,a,b,c,d,number(y,10),0,color(co),NULL);m->draw[m->draw_count-1].texture_u=number(u,10);m->draw[m->draw_count-1].texture_v=number(v,10);}}
        else if(!strcmp(kind,"pickup") && m->pickup_count<TOY_MAP_MAX_PICKUPS){
            char *k=word(&p),*sx=word(&p),*sz=word(&p),*sy=word(&p);
            if(k&&sx&&sz&&sy){
                int weapon = toy_game_weapon_from_name(k);
                m->pickups[m->pickup_count].kind = !strcmp(k,"shop") ?
                    TOY_MAP_PICKUP_SHOP : weapon >= 0 ?
                    ((weapon == TOY_GAME_WEAPON_PILL) ? TOY_MAP_PICKUP_PILL :
                     (weapon == TOY_GAME_WEAPON_BOMB ||
                      weapon == TOY_GAME_WEAPON_MOLOTOV) ?
                     TOY_MAP_PICKUP_THROWABLE : TOY_MAP_PICKUP_WEAPON) :
                    (!strcmp(k,"ammo") ? TOY_MAP_PICKUP_AMMO :
                     TOY_MAP_PICKUP_AMMO);
                m->pickups[m->pickup_count].weapon = weapon;
                m->pickups[m->pickup_count].x=number(sx,10);
                m->pickups[m->pickup_count].z=number(sz,10);
                m->pickups[m->pickup_count].y=number(sy,10);
                m->pickup_count++;
            }
        }
        else if((!strcmp(kind,"button") || !strcmp(kind,"button_air") ||
                 !strcmp(kind,"button_alarm") || !strcmp(kind,"button_heavy") ||
                 !strcmp(kind,"button_fast") || !strcmp(kind,"button_base1") ||
                 !strcmp(kind,"button_base2") || !strcmp(kind,"button_smoker") ||
                 !strcmp(kind,"button_charger") || !strcmp(kind,"button_tank") ||
                 !strcmp(kind,"button_money") ||
                 !strcmp(kind,"button_clear_hired") ||
                 !strcmp(kind,"button_wave_skip") ||
                 !strcmp(kind,"button_attack_x2") ||
                 !strcmp(kind,"button_attack_x3") ||
                 !strcmp(kind,"button_attack_x4") ||
                 !strcmp(kind,"button_pose_reset") ||
                 !strcmp(kind,"button_pose_right_arm") ||
                 !strcmp(kind,"button_pose_arms") ||
                 !strcmp(kind,"button_pose_body") ||
                 !strcmp(kind,"button_anim_idle") ||
                 !strcmp(kind,"button_anim_walk") ||
                 !strcmp(kind,"button_anim_jog")) && m->pickup_count<TOY_MAP_MAX_PICKUPS){
            char *sx=word(&p),*sz=word(&p),*sy=word(&p);
            if(sx&&sz&&sy){
                m->pickups[m->pickup_count].kind=!strcmp(kind,"button_air") ?
                    TOY_MAP_PICKUP_AIR_BUTTON : !strcmp(kind,"button_alarm") ?
                    TOY_MAP_PICKUP_ALARM_BUTTON : !strcmp(kind,"button_heavy") ?
                    TOY_MAP_PICKUP_HEAVY_HORDE_BUTTON : !strcmp(kind,"button_fast") ?
                    TOY_MAP_PICKUP_FAST_HORDE_BUTTON : !strcmp(kind,"button_base1") ?
                    TOY_MAP_PICKUP_BASE_1_BUTTON : !strcmp(kind,"button_base2") ?
                    TOY_MAP_PICKUP_BASE_2_BUTTON : !strcmp(kind,"button_smoker") ?
                    TOY_MAP_PICKUP_SMOKER_BUTTON : !strcmp(kind,"button_charger") ?
                    TOY_MAP_PICKUP_CHARGER_BUTTON : !strcmp(kind,"button_tank") ?
                    TOY_MAP_PICKUP_TANK_BUTTON : TOY_MAP_PICKUP_BUTTON;
                if (!strcmp(kind,"button_money"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_MONEY_BUTTON;
                else if (!strcmp(kind,"button_clear_hired"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_CLEAR_HIRED_BUTTON;
                else if (!strcmp(kind,"button_wave_skip"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_WAVE_SKIP_BUTTON;
                else if (!strcmp(kind,"button_attack_x2"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_ATTACK_X2_BUTTON;
                else if (!strcmp(kind,"button_attack_x3"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_ATTACK_X3_BUTTON;
                else if (!strcmp(kind,"button_attack_x4"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_ATTACK_X4_BUTTON;
                else if (!strcmp(kind,"button_pose_reset"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_POSE_RESET_BUTTON;
                else if (!strcmp(kind,"button_pose_right_arm"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_POSE_RIGHT_ARM_BUTTON;
                else if (!strcmp(kind,"button_pose_arms"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_POSE_ARMS_BUTTON;
                else if (!strcmp(kind,"button_pose_body"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_POSE_BODY_BUTTON;
                else if (!strcmp(kind,"button_anim_idle"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_ANIM_IDLE_BUTTON;
                else if (!strcmp(kind,"button_anim_walk"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_ANIM_WALK_BUTTON;
                else if (!strcmp(kind,"button_anim_jog"))
                    m->pickups[m->pickup_count].kind = TOY_MAP_PICKUP_ANIM_JOG_BUTTON;
                m->pickups[m->pickup_count].x=number(sx,10);
                m->pickups[m->pickup_count].z=number(sz,10);
                m->pickups[m->pickup_count].y=number(sy,10);
                m->pickup_count++;
            }
        }
    }
    /* A defense arena may intentionally have no interior collision boxes;
     * room_limit still supplies the outer gameplay boundary. */
    if (!(m->room_limit > 0)) {
        toy_map_unload(m);
        return -1;
    }
    return 0;
}
void toy_map_unload(struct toy_map *m){if(m&&m->blob)tlibc_free(m->blob);if(m)memset(m,0,sizeof(*m));}
