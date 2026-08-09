#include "rasterfall_viewmodel.h"

#define VIEW_NEAR_Z 192

struct view_vec3 { int x, y, z; };

int rasterfall_viewmodel_weapon(const struct toy_game *game)
{
    int slot = game->current_slot;
    if (slot < 0 || slot >= TOY_GAME_WEAPON_SLOTS) return -1;
    return game->slots[slot].weapon;
}

void rasterfall_viewmodel_muzzle_offset(int weapon, int kick,
                                        int *x, int *y, int *z)
{
    if (weapon == TOY_GAME_WEAPON_SMG) {
        *x = 105; *y = -65; *z = 450;
    } else if (weapon == TOY_GAME_WEAPON_SHOTGUN) {
        *x = 100; *y = -65; *z = 560;
    } else {
        *x = 102; *y = -70; *z = 420;
    }
    *x += kick / 3;
    *y -= kick / 2;
    *z += kick;
}

static int fill_triangle_2d(struct toy_surface *surface,
                            int x0, int y0, int x1, int y1, int x2, int y2,
                            uint32_t color)
{
    int y, tmp, xa, xb, ymin, ymax, drawn = 0;
    long dx01 = 0, dx02, dx12 = 0, xl, xr, lt;
    if (y0 > y1) { tmp=x0; x0=x1; x1=tmp; tmp=y0; y0=y1; y1=tmp; }
    if (y1 > y2) { tmp=x1; x1=x2; x2=tmp; tmp=y1; y1=y2; y2=tmp; }
    if (y0 > y1) { tmp=x0; x0=x1; x1=tmp; tmp=y0; y0=y1; y1=tmp; }
    if (y0 == y2 || y2 < 0 || y0 >= surface->height) return 0;
    if (y1 > y0) dx01 = (long)(x1-x0) * 65536 / (y1-y0);
    dx02 = (long)(x2-x0) * 65536 / (y2-y0);
    if (y2 > y1) dx12 = (long)(x2-x1) * 65536 / (y2-y1);
    ymin = y0 < 0 ? 0 : y0;
    ymax = y2 >= surface->height ? surface->height - 1 : y2;
    for (y = ymin; y <= ymax; y++) {
        if (y <= y1) {
            xl = (long)x0 * 65536 + dx01 * (y-y0);
            xr = (long)x0 * 65536 + dx02 * (y-y0);
        } else {
            xl = (long)x1 * 65536 + dx12 * (y-y1);
            xr = (long)x0 * 65536 + dx02 * (y-y0);
        }
        if (xl > xr) { lt=xl; xl=xr; xr=lt; }
        xa = (int)(xl >> 16); xb = (int)(xr >> 16);
        if (xa < 0) xa = 0;
        if (xb >= surface->width) xb = surface->width - 1;
        if (xa <= xb) {
            uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                         y * surface->stride);
            int x;
            for (x = xa; x <= xb; x++) row[x] = color;
            drawn += xb - xa + 1;
        }
    }
    return drawn;
}

static int project_view(const struct toy_surface *surface,
                        const struct view_vec3 *view,
                        int *x, int *y, int *z)
{
    int focal = surface->width * 3 / 4;
    if (view->z < VIEW_NEAR_Z) return 0;
    *x = surface->width / 2 + view->x * focal / view->z;
    *y = surface->height / 2 - view->y * focal / view->z;
    *z = view->z;
    return 1;
}

static int draw_view_quad(struct toy_surface *surface,
                          const struct view_vec3 *p1,
                          const struct view_vec3 *p2,
                          const struct view_vec3 *p3,
                          const struct view_vec3 *p4, uint32_t color)
{
    int x1, y1, z1, x2, y2, z2, x3, y3, z3, x4, y4, z4;
    if (!project_view(surface,p1,&x1,&y1,&z1) ||
        !project_view(surface,p2,&x2,&y2,&z2) ||
        !project_view(surface,p3,&x3,&y3,&z3) ||
        !project_view(surface,p4,&x4,&y4,&z4)) return 0;
    return fill_triangle_2d(surface,x1,y1,x2,y2,x3,y3,color) +
           fill_triangle_2d(surface,x1,y1,x3,y3,x4,y4,color);
}

static int draw_view_box(struct toy_surface *surface,
                         int minx, int maxx, int miny, int maxy,
                         int minz, int maxz, uint32_t color,
                         int skip_back, int skip_top, int kick)
{
    struct view_vec3 a, b, c, d, e, f, g, h;
    a.x=minx; a.y=miny; a.z=minz;
    b.x=maxx; b.y=miny; b.z=minz;
    c.x=maxx; c.y=miny; c.z=maxz;
    d.x=minx; d.y=miny; d.z=maxz;
    e.x=minx; e.y=maxy; e.z=minz;
    f.x=maxx; f.y=maxy; f.z=minz;
    g.x=maxx; g.y=maxy; g.z=maxz;
    h.x=minx; h.y=maxy; h.z=maxz;
    int kx=kick/3, ky=-kick/2, kz=kick, pixels=0;
    a.x+=kx; b.x+=kx; c.x+=kx; d.x+=kx; e.x+=kx; f.x+=kx; g.x+=kx; h.x+=kx;
    a.y+=ky; b.y+=ky; c.y+=ky; d.y+=ky; e.y+=ky; f.y+=ky; g.y+=ky; h.y+=ky;
    a.z+=kz; b.z+=kz; c.z+=kz; d.z+=kz; e.z+=kz; f.z+=kz; g.z+=kz; h.z+=kz;
    pixels += draw_view_quad(surface,&d,&c,&g,&h,color);
    pixels += draw_view_quad(surface,&b,&c,&g,&f,color+0x0A0A0A);
    pixels += draw_view_quad(surface,&d,&a,&e,&h,color-0x0A0A0A);
    if (!skip_top) pixels += draw_view_quad(surface,&e,&f,&g,&h,color+0x141414);
    if (!skip_back) pixels += draw_view_quad(surface,&a,&b,&f,&e,color-0x141414);
    pixels += draw_view_quad(surface,&a,&b,&c,&d,color-0x0C0C0C);
    return pixels;
}

static int render_pistol(struct toy_surface *s, int kick)
{
    int p=0;
    p+=draw_view_box(s,90,150,-180,-95,255,305,0x5A4630,1,1,kick);
    p+=draw_view_box(s,85,160,-115,-45,255,340,0x3E4652,0,0,kick);
    p+=draw_view_box(s,75,130,-90,-50,300,420,0x2E343D,1,0,kick);
    return p;
}

static int render_smg(struct toy_surface *s, int kick)
{
    int p=0;
    p+=draw_view_box(s,95,155,-170,-95,275,320,0x4A4438,1,1,kick);
    p+=draw_view_box(s,90,170,-115,-40,255,345,0x3B4148,0,0,kick);
    p+=draw_view_box(s,70,140,-85,-40,310,450,0x2F343B,1,0,kick);
    return p;
}

static int render_shotgun(struct toy_surface *s, int kick)
{
    int p=0;
    p+=draw_view_box(s,95,175,-110,-40,270,355,0x46505A,0,0,kick);
    p+=draw_view_box(s,70,130,-90,-40,335,560,0x3A434D,1,0,kick);
    p+=draw_view_box(s,75,140,-100,-45,390,460,0x2C3138,0,0,kick);
    return p;
}

int rasterfall_viewmodel_render(struct toy_renderer *renderer,
                                const struct toy_game *game,
                                const struct rasterfall_effects *effects)
{
    int weapon = rasterfall_viewmodel_weapon(game);
    int kick = effects->weapon_kick;
    if (weapon == TOY_GAME_WEAPON_SMG) return render_smg(&renderer->surface,kick);
    if (weapon == TOY_GAME_WEAPON_SHOTGUN) return render_shotgun(&renderer->surface,kick);
    if (weapon == TOY_GAME_WEAPON_PISTOL) return render_pistol(&renderer->surface,kick);
    return 0;
}
