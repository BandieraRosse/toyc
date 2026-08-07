#include "tlibc_everything.h"
#include "rasterfall_hud.h"
#include "fb_draw.h"
#include "fb_font.h"

static void hud_fill_rect(struct toy_surface *surface, int x, int y,
                          int width, int height, uint32_t color)
{
    int yy, xx;
    uint32_t *row;
    if (!surface || !surface->pixels || width <= 0 || height <= 0) return;
    for (yy = y; yy < y + height; yy++) {
        if (yy < 0 || yy >= surface->height) continue;
        row = (uint32_t *)((unsigned char *)surface->pixels + yy * surface->stride);
        for (xx = x; xx < x + width; xx++)
            if (xx >= 0 && xx < surface->width) row[xx] = color;
    }
}

static const char *campaign_phase_name(int phase)
{
    if (phase == TOY_GAME_PHASE_BUILDUP) return "BUILDUP";
    if (phase == TOY_GAME_PHASE_HORDE) return "HORDE";
    if (phase == TOY_GAME_PHASE_RELAX) return "RELAX";
    return "CALM";
}

static int draw_hud_value(struct toy_surface *surface, int x,
                          const char *label, const char *value, uint32_t color)
{
    int label_w = (int)strlen(label) * FB_FONT_W;
    int value_w = (int)strlen(value) * FB_FONT_W;
    fb_draw_string((unsigned char *)surface->pixels, x, 8,
                   label, 0xAAB4C0, surface->stride);
    x += label_w;
    hud_fill_rect(surface, x - 1, 6, value_w + 2, FB_FONT_H + 4, 0x26384C);
    fb_draw_string((unsigned char *)surface->pixels, x, 8,
                   value, color, surface->stride);
    return x + value_w + FB_FONT_W * 2;
}

static void render_weapon_hud(struct toy_surface *surface, int x, int y,
                              const struct toy_game *game)
{
    char line[48];
    int i;
    for (i = 0; i < TOY_GAME_WEAPON_SLOTS; i++) {
        const struct toy_game_slot *s = &game->slots[i];
        uint32_t color = i == game->current_slot ? 0xFFD060 : 0x9AA6B4;
        if (s->weapon < 0)
            snprintf(line, sizeof(line), "%d[-] ", i + 1);
        else if (s->reserve == TOY_GAME_AMMO_INFINITE)
            snprintf(line, sizeof(line), "%d[%s] %d/INF ", i + 1,
                     toy_game_weapon_name(s->weapon), s->mag);
        else
            snprintf(line, sizeof(line), "%d[%s] %d/%d ", i + 1,
                     toy_game_weapon_name(s->weapon), s->mag, s->reserve);
        fb_draw_string((unsigned char *)surface->pixels, x, y, line, color,
                       surface->stride);
        x += (int)strlen(line) * FB_FONT_W;
    }
}

static void render_network_hud(struct toy_surface *surface,
                               const struct rasterfall_net *net,
                               const char *host_address, int host_port)
{
    char line[96];
    int width, x, y = 8;
    uint32_t color;
    if (!net || net->mode == RASTERFALL_NET_OFF) return;
    if (net->mode == RASTERFALL_NET_HOST) {
        if (net->peer_known && net->connected)
            snprintf(line, sizeof(line), "HOST  PLAYER 2  RTT %d MS", net->rtt_ms);
        else if (net->peer_known)
            snprintf(line, sizeof(line), "HOST  PLAYER LOST");
        else
            snprintf(line, sizeof(line), "HOST  WAITING FOR PLAYER");
        color = net->peer_known && net->connected ? 0x80E0C0 : 0xFFD070;
    } else if (net->connected) {
        snprintf(line, sizeof(line), "CLIENT  CONNECTED  RTT %d MS", net->rtt_ms);
        color = net->rtt_ms > 150 ? 0xFFB060 : 0x80E0C0;
    } else {
        snprintf(line, sizeof(line), "CLIENT  %s",
                 net->last_receive_ms ? "DISCONNECTED" : "CONNECTING...");
        color = 0xFFD070;
    }
    width = (int)strlen(line) * FB_FONT_W;
    x = surface->width - width - 10;
    if (x < 8) x = 8;
    hud_fill_rect(surface, x - 4, y - 2, width + 8, FB_FONT_H + 4, 0x182634);
    fb_draw_string((unsigned char *)surface->pixels, x, y, line, color,
                   surface->stride);
    if (net->mode == RASTERFALL_NET_HOST && host_address && host_address[0]) {
        snprintf(line, sizeof(line), "JOIN %s:%d", host_address, host_port);
        width = (int)strlen(line) * FB_FONT_W;
        x = surface->width - width - 10;
        if (x < 8) x = 8;
        hud_fill_rect(surface, x - 4, y + FB_FONT_H,
                      width + 8, FB_FONT_H + 4, 0x182634);
        fb_draw_string((unsigned char *)surface->pixels, x, y + FB_FONT_H,
                       line, 0xFFD060, surface->stride);
    }
}

void rasterfall_hud_render(struct toy_surface *surface, int fps,
                           const struct rasterfall_hud_state *state)
{
    const struct toy_game *game = state->game;
    const struct toy_game_actor *ai = &game->actors[0];
    char line[96];
    int n, x = 8, hint_y;
    uint32_t phase_color = game->campaign_phase == TOY_GAME_PHASE_HORDE ?
                           0xFFD040 : 0x80E0C0;
    x = draw_hud_value(surface, x, "DIR ",
                       campaign_phase_name(game->campaign_phase), phase_color);
    snprintf(line, sizeof(line), "%d", game->spawn_budget);
    x = draw_hud_value(surface, x, "BUD ", line, 0xFFD070);
    snprintf(line, sizeof(line), "%d", game->enemies_alive);
    x = draw_hud_value(surface, x, "LIVE ", line, 0xF0F0F0);
    snprintf(line, sizeof(line), "%d", game->active_attackers);
    x = draw_hud_value(surface, x, "ACT ", line,
                       game->active_attackers > 0 ? 0xFF8060 : 0x80E080);
    snprintf(line, sizeof(line), "%dS",
             game->phase_timer_ms > 0 ? (game->phase_timer_ms + 999) / 1000 : 0);
    x = draw_hud_value(surface, x, "NEXT ", line, 0x80C8FF);
    snprintf(line, sizeof(line), "%d", game->director_encounters);
    x = draw_hud_value(surface, x, "RUN ", line, 0xC0A0FF);
    snprintf(line, sizeof(line), "%d", fps);
    draw_hud_value(surface, x, "FPS ", line, 0x90F090);
    render_network_hud(surface, state->net, state->host_address, state->host_port);
    n = snprintf(line, sizeof(line), "%s  HP %d  KILLS %d",
                 state->player_name ? state->player_name : "PLAYER",
                 game->hp, game->kills);
    if (n > 0)
        fb_draw_string((unsigned char *)surface->pixels, 8, 8 + FB_FONT_H,
                       line, game->hp < 10 ? 0xF03030 :
                       game->hp < 40 ? 0xF0C830 : 0x40D060,
                       surface->stride);
    render_weapon_hud(surface, 8, 8 + FB_FONT_H * 2, game);
    hint_y = 8 + FB_FONT_H * (game->reloading ? 4 : 3);
    if (game->reloading)
        fb_draw_string((unsigned char *)surface->pixels, 8, 8 + FB_FONT_H * 3,
                       "RELOADING...", 0xD88A32, surface->stride);
    if (game->player_down) {
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       "DOWN - WAIT FOR REVIVE", 0xF03030, surface->stride);
    } else if (state->ai_revive_active && ai->state == TOY_GAME_ACTOR_DOWNED) {
        snprintf(line, sizeof(line), "REVIVING JESUS %d%%",
                 ai->revive_progress_ms * 100 / TOY_GAME_REVIVE_MS);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0x70D8FF, surface->stride);
    } else if (ai->state == TOY_GAME_ACTOR_DOWNED) {
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       "E REVIVE JESUS", 0x70D8FF, surface->stride);
    } else if (state->map->safe_count > 1 &&
        toy_game_point_in_box(game->px, game->pz, &state->safe_rooms[1])) {
        snprintf(line, sizeof(line), "EXIT SECURE %d%%",
                 game->goal_hold_ms * 100 / TOY_GAME_GOAL_HOLD_MS);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0x80E080, surface->stride);
    } else if (state->map->safe_count > 0 &&
               toy_game_point_in_box(game->px, game->pz, &state->safe_rooms[0])) {
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       "START SAFE ROOM - REACH GREEN EXIT", 0x80E080,
                       surface->stride);
    } else if (state->manual_alarm_enabled) {
        snprintf(line, sizeof(line), "ALARM ACTIVE  NEXT %d SEC",
                 (state->manual_alarm_timer_ms + 999) / 1000);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0xFF6040, surface->stride);
    } else if (game->alarm_timer_ms > 0) {
        snprintf(line, sizeof(line), "ALARM HORDE %d SEC",
                 (game->alarm_timer_ms + 999) / 1000);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0xFFD040, surface->stride);
    } else if (game->campaign_phase == TOY_GAME_PHASE_RELAX) {
        snprintf(line, sizeof(line), "HORDE CLEARED - RELAX %d SEC",
                 (game->phase_timer_ms + 999) / 1000);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0x80E080, surface->stride);
    } else {
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       "ALARM OFF - USE RED PANEL", 0xFFD040,
                       surface->stride);
    }
    if (state->horde_banner_ms > 0 && state->interaction_banner) {
        int banner_y = surface->height / 3;
        fb_draw_string((unsigned char *)surface->pixels,
                       (surface->width - (int)strlen(state->interaction_banner) * FB_FONT_W) / 2,
                       banner_y, state->interaction_banner, 0xFF3030,
                       surface->stride);
    }
}

void rasterfall_hud_draw_interact_prompt(struct toy_renderer *renderer,
                                         const struct rasterfall_hud_state *state)
{
    const struct rasterfall_interactable *it;
    char label[48];
    int text_w, x, y;
    if (state->highlighted < 0 || state->highlighted >= state->interactable_count)
        return;
    it = &state->interactables[state->highlighted];
    if (it->kind == TOY_MAP_PICKUP_BUTTON)
        snprintf(label, sizeof(label), "E SUMMON HORDE");
    else if (it->kind == TOY_MAP_PICKUP_AIR_BUTTON)
        snprintf(label, sizeof(label), "E AIR WALLS %s",
                 state->air_walls_enabled ? "OFF" : "ON");
    else if (it->kind == TOY_MAP_PICKUP_ALARM_BUTTON)
        snprintf(label, sizeof(label), "E ALARM %s",
                 state->manual_alarm_enabled ? "OFF" : "ON");
    else if (it->kind == TOY_MAP_PICKUP_HEAVY_HORDE_BUTTON)
        snprintf(label, sizeof(label), "E SUMMON BROWN BRUTES");
    else if (it->kind == TOY_MAP_PICKUP_FAST_HORDE_BUTTON)
        snprintf(label, sizeof(label), "E SUMMON RED RUNNERS");
    else if (it->kind == TOY_MAP_PICKUP_BASE_1_BUTTON)
        snprintf(label, sizeof(label), "E OPEN BASE 1 GATE");
    else if (it->kind == TOY_MAP_PICKUP_BASE_2_BUTTON)
        snprintf(label, sizeof(label), "E OPEN BASE 2 GATE");
    else if (it->kind == TOY_MAP_PICKUP_AMMO)
        snprintf(label, sizeof(label), "E TAKE AMMO");
    else {
        int weapon = it->kind == TOY_MAP_PICKUP_SMG ?
                     TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_SHOTGUN;
        const char *name = it->kind == TOY_MAP_PICKUP_SMG ? "SMG" : "SHOTGUN";
        snprintf(label, sizeof(label), "E %s %s",
                 state->game->slots[0].weapon == weapon ? "REFILL" : "PICK UP", name);
    }
    text_w = (int)strlen(label) * FB_FONT_W;
    x = (renderer->surface.width - text_w) / 2;
    y = renderer->surface.height - FB_FONT_H - 18;
    hud_fill_rect(&renderer->surface, x - 5, y - 3, text_w + 10, FB_FONT_H + 6,
                  0x171B24);
    fb_draw_string((unsigned char *)renderer->surface.pixels, x, y,
                   label, 0xFFD060, renderer->surface.stride);
}

void rasterfall_hud_dump_frame(const char *path, const struct toy_surface *surface)
{
    char header[48];
    unsigned char *rgb;
    int hlen, fd, i;
    if (!surface || !surface->pixels || surface->width <= 0 || surface->height <= 0)
        return;
    hlen = snprintf(header, sizeof(header), "P6\n%d %d\n255\n",
                    surface->width, surface->height);
    rgb = tlibc_malloc((size_t)surface->width * surface->height * 3);
    if (!rgb) return;
    for (i = 0; i < surface->width * surface->height; i++) {
        uint32_t p = ((uint32_t *)surface->pixels)[i];
        rgb[i * 3 + 0] = (unsigned char)(p & 0xFF);
        rgb[i * 3 + 1] = (unsigned char)((p >> 8) & 0xFF);
        rgb[i * 3 + 2] = (unsigned char)((p >> 16) & 0xFF);
    }
    fd = __creat(path, 0644);
    if (fd >= 0) {
        __write(fd, header, hlen);
        __write(fd, rgb, surface->width * surface->height * 3);
        __close(fd);
        __printf("rasterfall: dumped frame to %s\n", path);
    }
    tlibc_free(rgb);
}

void rasterfall_hud_damage_flash(struct toy_surface *surface,
                                 const struct toy_game *game)
{
    int x, y;
    if (game->damage_flash_ms <= 0) return;
    for (y = 0; y < surface->height; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                     y * surface->stride);
        for (x = y & 1; x < surface->width; x += 2) row[x] = 0xAA0000;
    }
}
