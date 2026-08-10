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

static void draw_weapon_silhouette(struct toy_surface *surface, int weapon,
                                   int cx, int y, uint32_t color)
{
    /* Small, deliberately abstract silhouettes: they remain recognizable at
     * the compact HUD scale without introducing another art asset pipeline. */
    if (weapon == TOY_GAME_WEAPON_PISTOL) {
        hud_fill_rect(surface, cx - 18, y + 9, 28, 7, color);
        hud_fill_rect(surface, cx + 9, y + 11, 9, 4, color);
        hud_fill_rect(surface, cx - 8, y + 16, 8, 12, color);
        hud_fill_rect(surface, cx - 10, y + 26, 9, 4, color);
    } else if (weapon == TOY_GAME_WEAPON_SMG) {
        hud_fill_rect(surface, cx - 23, y + 9, 34, 8, color);
        hud_fill_rect(surface, cx + 10, y + 11, 18, 4, color);
        hud_fill_rect(surface, cx - 25, y + 7, 7, 5, color);
        hud_fill_rect(surface, cx - 4, y + 17, 7, 14, color);
        hud_fill_rect(surface, cx - 15, y + 17, 7, 8, color);
    } else if (weapon == TOY_GAME_WEAPON_SHOTGUN) {
        hud_fill_rect(surface, cx - 25, y + 10, 24, 9, color);
        hud_fill_rect(surface, cx - 1, y + 12, 29, 5, color);
        hud_fill_rect(surface, cx - 14, y + 19, 7, 10, color);
        hud_fill_rect(surface, cx - 25, y + 12, 7, 5, color);
    } else {
        hud_fill_rect(surface, cx - 12, y + 17, 24, 3, color);
    }
}

static int draw_hud_value(struct toy_surface *surface, int x,
                          const char *label, const char *value,
                          uint32_t color)
{
    int label_w = (int)strlen(label) * FB_FONT_W;
    int value_w = (int)strlen(value) * FB_FONT_W;
    fb_draw_string((unsigned char *)surface->pixels, x, 8,
                   label, 0xAAB4C0, surface->stride);
    x += label_w;
    hud_fill_rect(surface, x - 1, 6, value_w + 2, FB_FONT_H + 4,
                  0x26384C);
    fb_draw_string((unsigned char *)surface->pixels, x, 8,
                   value, color, surface->stride);
    return x + value_w + FB_FONT_W * 2;
}

static const char *weapon_abbreviation(int weapon)
{
    if (weapon == TOY_GAME_WEAPON_SMG) return "SMG";
    if (weapon == TOY_GAME_WEAPON_SHOTGUN) return "SG";
    if (weapon == TOY_GAME_WEAPON_PISTOL) return "PG";
    return "--";
}

static void render_weapon_card(struct toy_surface *surface, int x, int y,
                               int slot_index, const struct toy_game_slot *slot,
                               int selected)
{
    char line[24];
    uint32_t border = selected ? RF_COLOR_UI_ACCENT : 0x526170;
    uint32_t text = selected ? RF_COLOR_UI_ACCENT_BRIGHT : 0xD0D7DE;
    int cx = x + 37;
    hud_fill_rect(surface, x, y, 74, 70, RF_COLOR_UI_PANEL_DARK);
    hud_fill_rect(surface, x, y, 74, 3, border);
    hud_fill_rect(surface, x, y + 67, 74, 3, border);
    hud_fill_rect(surface, x, y, 3, 70, border);
    hud_fill_rect(surface, x + 71, y, 3, 70, border);
    snprintf(line, sizeof(line), "%d  %s", slot_index + 1,
             weapon_abbreviation(slot->weapon));
    fb_draw_string((unsigned char *)surface->pixels, x + 7, y + 6,
                   line, text, surface->stride);
    draw_weapon_silhouette(surface, slot->weapon, cx, y + 16,
                           selected ? 0xE4B84E : 0x8A98A8);
    if (slot->weapon < 0)
        snprintf(line, sizeof(line), "-- / --");
    else if (slot->reserve == TOY_GAME_AMMO_INFINITE)
        snprintf(line, sizeof(line), "%d / INF", slot->mag);
    else
        snprintf(line, sizeof(line), "%d / %d", slot->mag, slot->reserve);
    fb_draw_string((unsigned char *)surface->pixels, x + 7, y + 52,
                   line, slot->mag <= 0 ? RF_COLOR_UI_DANGER : text, surface->stride);
}

static void render_weapon_hud(struct toy_surface *surface,
                              const struct toy_game *game)
{
    int x = surface->width - 84;
    int y = surface->height / 2 - 73;
    render_weapon_card(surface, x, y, 0, &game->slots[0],
                       game->current_slot == 0);
    render_weapon_card(surface, x, y + 78, 1, &game->slots[1],
                       game->current_slot == 1);
}

static char hud_upper_ascii(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

static void render_player_hud(struct toy_surface *surface,
                              const struct toy_game *game,
                              const char *player_name)
{
    char name[TOY_GAME_MAX_NAME];
    char line[32];
    int i, x = 16, y = surface->height - 68;
    int bar_x = x + 53, bar_y = y + 29, bar_w = 128;
    uint32_t hp_color = game->hp < 10 ? RF_COLOR_UI_DANGER :
                        game->hp < 40 ? RF_COLOR_UI_WARNING : RF_COLOR_UI_SUCCESS;
    if (!player_name || !*player_name) player_name = "PLAYER";
    for (i = 0; i < TOY_GAME_MAX_NAME - 1 && player_name[i]; i++)
        name[i] = hud_upper_ascii(player_name[i]);
    name[i] = 0;
    hud_fill_rect(surface, x, y, 42, 42, 0x202B35);
    hud_fill_rect(surface, x + 3, y + 3, 36, 36, 0x304354);
    line[0] = name[0] ? name[0] : 'P';
    line[1] = 0;
    fb_draw_string((unsigned char *)surface->pixels, x + 14, y + 11,
                   line, 0xF0F4F8, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels, bar_x, y + 5,
                   name, RF_COLOR_UI_TEXT, surface->stride);
    hud_fill_rect(surface, bar_x, bar_y, bar_w, 10, RF_COLOR_UI_PANEL);
    hud_fill_rect(surface, bar_x, bar_y,
                  game->hp * bar_w / TOY_GAME_PLAYER_HP, 10, hp_color);
    snprintf(line, sizeof(line), "%d / %d", game->hp, TOY_GAME_PLAYER_HP);
    fb_draw_string((unsigned char *)surface->pixels, bar_x + bar_w -
                   (int)strlen(line) * FB_FONT_W, bar_y + 12,
                   line, hp_color, surface->stride);
}

static void render_revive_prompt(struct toy_surface *surface,
                                 const struct rasterfall_hud_state *state)
{
    const struct toy_game *game = state->game;
    char line[64];
    int width, x, y = surface->height / 2 + 24;
    if (game->player_down || (!state->ai_revive_available &&
                              !state->ai_revive_active)) return;
    if (state->ai_revive_active) {
        snprintf(line, sizeof(line), "REVIVING %s  %d%%",
                 state->ai_revive_name ? state->ai_revive_name : "ALLY",
                 state->ai_revive_progress_ms * 100 / TOY_GAME_REVIVE_MS);
    } else {
        snprintf(line, sizeof(line), "E REVIVE %s",
                 state->ai_revive_name ? state->ai_revive_name : "ALLY");
    }
    width = (int)strlen(line) * FB_FONT_W;
    x = (surface->width - width) / 2;
    hud_fill_rect(surface, x - 8, y - 5, width + 16, FB_FONT_H + 10,
                  RF_COLOR_UI_BACKGROUND);
    fb_draw_string((unsigned char *)surface->pixels, x, y, line,
                   RF_COLOR_UI_PLAYER, surface->stride);
    if (state->ai_revive_active) {
        int bar_x = surface->width / 2 - 64;
        hud_fill_rect(surface, bar_x, y + FB_FONT_H + 5, 128, 5, RF_COLOR_UI_PANEL);
        hud_fill_rect(surface, bar_x, y + FB_FONT_H + 5,
                      state->ai_revive_progress_ms * 128 /
                      TOY_GAME_REVIVE_MS, 5, RF_COLOR_UI_PLAYER);
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
        color = net->peer_known && net->connected ? RF_COLOR_UI_SECONDARY : 0xFFD070;
    } else if (net->connected) {
        snprintf(line, sizeof(line), "CLIENT  CONNECTED  RTT %d MS", net->rtt_ms);
        color = net->rtt_ms > 150 ? 0xFFB060 : RF_COLOR_UI_SECONDARY;
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
                       line, RF_COLOR_UI_ACCENT, surface->stride);
    }
}

static void render_director_hud(struct toy_surface *surface,
                                const struct toy_game *game, int fps)
{
    char value[32];
    char next_value[32];
    int x = 8;
    const char *phase = game->campaign_phase == TOY_GAME_PHASE_BUILDUP ?
                        "BUILDUP" :
                        game->campaign_phase == TOY_GAME_PHASE_HORDE ?
                        "HORDE" :
                        game->campaign_phase == TOY_GAME_PHASE_RELAX ?
                        "RELAX" : "CALM";
    if (game->phase_timer_ms > 0) {
        snprintf(next_value, sizeof(next_value), "%dS",
                 (game->phase_timer_ms + 999) / 1000);
    } else if (game->campaign_phase == TOY_GAME_PHASE_BUILDUP) {
        strcpy(next_value, "IN");
    } else {
        strcpy(next_value, "READY");
    }
    x = draw_hud_value(surface, x, "DIRECTOR ", phase,
                       game->campaign_phase == TOY_GAME_PHASE_HORDE ?
                       0xFFD040 : RF_COLOR_UI_SECONDARY);
    snprintf(value, sizeof(value), "%d", game->spawn_budget);
    x = draw_hud_value(surface, x, "QUEUE ", value, 0xFFD070);
    snprintf(value, sizeof(value), "%d", game->enemies_alive);
    x = draw_hud_value(surface, x, "LIVE ", value, 0xF0F0F0);
    snprintf(value, sizeof(value), "%d", game->active_attackers);
    x = draw_hud_value(surface, x, "ACT ", value,
                       game->active_attackers > 0 ? 0xFF8060 : RF_COLOR_UI_AI);
    x = draw_hud_value(surface, x, "NEXT ", next_value, 0x80C8FF);
    snprintf(value, sizeof(value), "%d", game->director_encounters);
    x = draw_hud_value(surface, x, "RUN ", value, 0xC0A0FF);
    snprintf(value, sizeof(value), "%d", fps);
    draw_hud_value(surface, x, "FPS ", value, 0x90F090);
}

void rasterfall_hud_render(struct toy_surface *surface, int fps,
                           const struct rasterfall_hud_state *state)
{
    const struct toy_game *game = state->game;
    render_director_hud(surface, game, fps);
    render_network_hud(surface, state->net, state->host_address, state->host_port);
    render_weapon_hud(surface, game);
    render_player_hud(surface, game, state->player_name);
    render_revive_prompt(surface, state);
    if (state->horde_banner_ms > 0 && state->interaction_banner) {
        int banner_y = surface->height / 3;
        fb_draw_string((unsigned char *)surface->pixels,
                       (surface->width - (int)strlen(state->interaction_banner) * FB_FONT_W) / 2,
                       banner_y, state->interaction_banner, 0xFF3030,
                       surface->stride);
    }
    {
        int smoker_pull = game->player_pull_enemy_index >= 0;
        for (int i = 0; !smoker_pull && i < TOY_GAME_MAX_ENEMIES; i++)
            if (game->enemies[i].active == 1 &&
                game->enemies[i].type == TOY_GAME_ENEMY_SMOKER &&
                game->enemies[i].special_target_active)
                smoker_pull = 1;
        if (game->player_control_disabled && smoker_pull) {
        const char *warning = "WARNING: SMOKER PULLING YOU";
        fb_draw_string((unsigned char *)surface->pixels,
                       (surface->width - (int)strlen(warning) * FB_FONT_W) / 2,
                       surface->height / 2 - 48, warning, 0xFF5040,
                       surface->stride);
        }
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
    else if (it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON)
        snprintf(label, sizeof(label), "E SUMMON SMOKER");
    else if (it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON)
        snprintf(label, sizeof(label), "E SUMMON CHARGER");
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
                  RF_COLOR_UI_BACKGROUND);
    fb_draw_string((unsigned char *)renderer->surface.pixels, x, y,
                   label, RF_COLOR_UI_ACCENT, renderer->surface.stride);
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
