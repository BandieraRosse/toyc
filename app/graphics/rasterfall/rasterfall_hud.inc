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
    fill_rect(surface, x - 1, 6, value_w + 2, FB_FONT_H + 4, 0x26384C);
    fb_draw_string((unsigned char *)surface->pixels, x, 8,
                   value, color, surface->stride);
    return x + value_w + FB_FONT_W * 2;
}

static const char *weapon_names[TOY_GAME_WEAPON_COUNT] = {
    "PISTOL", "SMG", "SHOTGUN"
};

/* 武器栏：1[主武器] 弹匣/备弹  2[手枪] 弹匣/备弹，当前槽位高亮 */
static void render_weapon_hud(struct toy_surface *surface, int x, int y)
{
    char line[48];
    int i;
    for (i = 0; i < TOY_GAME_WEAPON_SLOTS; i++) {
        const struct toy_game_slot *s = &game.slots[i];
        uint32_t color = i == game.current_slot ? 0xFFD060 : 0x9AA6B4;
        if (s->weapon < 0)
            snprintf(line, sizeof(line), "%d[-] ", i + 1);
        else if (s->reserve == TOY_GAME_AMMO_INFINITE)
            snprintf(line, sizeof(line), "%d[%s] %d/INF ", i + 1,
                     weapon_names[s->weapon], s->mag);
        else
            snprintf(line, sizeof(line), "%d[%s] %d/%d ", i + 1,
                     weapon_names[s->weapon], s->mag, s->reserve);
        fb_draw_string((unsigned char *)surface->pixels, x, y, line, color,
                       surface->stride);
        x += (int)strlen(line) * FB_FONT_W;
    }
}

static void render_hud(struct toy_surface *surface, int fps)
{
    char line[96];
    int n, x = 8, hint_y;
    uint32_t phase_color = game.campaign_phase == TOY_GAME_PHASE_HORDE ?
                           0xFFD040 : 0x80E0C0;
    x = draw_hud_value(surface, x, "DIR ",
                       campaign_phase_name(game.campaign_phase), phase_color);
    snprintf(line, sizeof(line), "%d", game.spawn_budget);
    x = draw_hud_value(surface, x, "BUD ", line, 0xFFD070);
    snprintf(line, sizeof(line), "%d", game.enemies_alive);
    x = draw_hud_value(surface, x, "LIVE ", line, 0xF0F0F0);
    snprintf(line, sizeof(line), "%d", game.active_attackers);
    x = draw_hud_value(surface, x, "ACT ", line,
                       game.active_attackers > 0 ? 0xFF8060 : 0x80E080);
    snprintf(line, sizeof(line), "%dS",
             game.phase_timer_ms > 0 ? (game.phase_timer_ms + 999) / 1000 : 0);
    x = draw_hud_value(surface, x, "NEXT ", line, 0x80C8FF);
    snprintf(line, sizeof(line), "%d", game.director_encounters);
    x = draw_hud_value(surface, x, "RUN ", line, 0xC0A0FF);
    snprintf(line, sizeof(line), "%d", fps);
    draw_hud_value(surface, x, "FPS ", line, 0x90F090);
    n = snprintf(line, sizeof(line), "HP %d  KILLS %d", game.hp, game.kills);
    if (n > 0)
        fb_draw_string((unsigned char *)surface->pixels, 8, 8 + FB_FONT_H,
                       line, 0xE7E9EC, surface->stride);
    render_weapon_hud(surface, 8, 8 + FB_FONT_H * 2);
    hint_y = 8 + FB_FONT_H * (game.reloading ? 4 : 3);
    if (game.reloading)
        fb_draw_string((unsigned char *)surface->pixels, 8, 8 + FB_FONT_H * 3,
                       "RELOADING...", 0xD88A32, surface->stride);
    if (level_map.safe_count > 1 && toy_game_point_in_box(game.px, game.pz, &map_safe_rooms[1])) {
        snprintf(line, sizeof(line), "EXIT SECURE %d%%",
                 game.goal_hold_ms * 100 / TOY_GAME_GOAL_HOLD_MS);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0x80E080, surface->stride);
    } else if (level_map.safe_count > 0 && toy_game_point_in_box(game.px, game.pz, &map_safe_rooms[0])) {
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       "START SAFE ROOM - REACH GREEN EXIT", 0x80E080,
                       surface->stride);
    } else if (manual_alarm_enabled) {
        snprintf(line, sizeof(line), "ALARM ACTIVE  NEXT %d SEC",
                 (manual_alarm_timer_ms + 999) / 1000);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0xFF6040, surface->stride);
    } else if (game.alarm_timer_ms > 0) {
        snprintf(line, sizeof(line), "ALARM HORDE %d SEC",
                 (game.alarm_timer_ms + 999) / 1000);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0xFFD040, surface->stride);
    } else if (game.campaign_phase == TOY_GAME_PHASE_RELAX) {
        snprintf(line, sizeof(line), "HORDE CLEARED - RELAX %d SEC",
                 (game.phase_timer_ms + 999) / 1000);
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       line, 0x80E080, surface->stride);
    } else if (!manual_alarm_enabled) {
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       "ALARM OFF - USE RED PANEL", 0xFFD040,
                       surface->stride);
    } else {
        fb_draw_string((unsigned char *)surface->pixels, 8, hint_y,
                       "RED FLOOR = SPAWN ZONE", 0xE08080, surface->stride);
    }
    /* 召唤提示：屏幕中央横幅，短暂显示 */
    if (horde_banner_ms > 0) {
        int banner_y = surface->height / 3;
        fb_draw_string((unsigned char *)surface->pixels,
                       (surface->width - (int)strlen(interaction_banner) * FB_FONT_W) / 2,
                       banner_y, interaction_banner, 0xFF3030, surface->stride);
    }
}

/* --dump-frame：把最后一帧写为 PPM（XRGB8888 → RGB，调试渲染用） */
static void dump_frame_ppm(const char *path, const struct toy_surface *surface)
{
    char header[48];
    unsigned char *rgb;
    int hlen, fd, i;
    if (!surface || !surface->pixels || surface->width <= 0 ||
        surface->height <= 0) return;
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

/* 受击红屏：棋盘隔像素填充（省一半写入量） */
static void render_damage_flash(struct toy_surface *surface)
{
    if (game.damage_flash_ms <= 0) return;
    for (int y = 0; y < surface->height; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                     y * surface->stride);
        for (int x = y & 1; x < surface->width; x += 2) row[x] = 0xAA0000;
    }
}

/* ── 子弹轨迹与命中粒子（纯视觉；逻辑步进 16ms 推进） ──────────── */

#define TRACER_SLOTS 32
