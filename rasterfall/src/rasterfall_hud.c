#include "tlibc_everything.h"
#include "rasterfall_hud.h"
#include "fb_draw.h"
#include "fb_font.h"

#define special_target_active ability.special_target_active

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
    } else if (weapon == TOY_GAME_WEAPON_AK) {
        hud_fill_rect(surface, cx - 25, y + 9, 38, 7, color);
        hud_fill_rect(surface, cx + 12, y + 11, 18, 4, color);
        hud_fill_rect(surface, cx - 7, y + 16, 8, 12, color);
        hud_fill_rect(surface, cx - 22, y + 16, 8, 10, color);
    } else if (weapon == TOY_GAME_WEAPON_AWP) {
        hud_fill_rect(surface, cx - 30, y + 10, 44, 5, color);
        hud_fill_rect(surface, cx + 13, y + 11, 21, 3, color);
        hud_fill_rect(surface, cx - 13, y + 15, 9, 13, color);
        hud_fill_rect(surface, cx - 25, y + 7, 12, 4, color);
    } else {
        hud_fill_rect(surface, cx - 12, y + 17, 24, 3, color);
    }
}

static int hud_value_y = 8;

static int draw_hud_value(struct toy_surface *surface, int x,
                          const char *label, const char *value,
                          uint32_t color)
{
    int label_w = (int)strlen(label) * FB_FONT_W;
    int value_w = (int)strlen(value) * FB_FONT_W;
    fb_draw_string((unsigned char *)surface->pixels, x, hud_value_y,
                   label, 0xAAB4C0, surface->stride);
    x += label_w;
    hud_fill_rect(surface, x - 1, hud_value_y - 2, value_w + 2, FB_FONT_H + 4,
                  0x26384C);
    fb_draw_string((unsigned char *)surface->pixels, x, hud_value_y,
                   value, color, surface->stride);
    return x + value_w + FB_FONT_W * 2;
}

static const char *weapon_abbreviation(int weapon)
{
    const struct toy_game_weapon_info *info =
        toy_game_weapon_info_or_null(weapon);
    return info ? info->short_name : "--";
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
    render_weapon_card(surface, x, y + 156, 2, &game->slots[2],
                       game->current_slot == 2);
}

static void render_money(struct toy_surface *surface, const struct toy_game *game)
{
    char line[24];
    int x = surface->width - 150, y = surface->height / 2 - 112;
    snprintf(line, sizeof(line), "$ %d", game->money);
    hud_fill_rect(surface, x, y, 134, 28, 0x26384C);
    hud_fill_rect(surface, x, y, 3, 28, RF_COLOR_UI_ACCENT);
    fb_draw_string((unsigned char *)surface->pixels, x + 10, y + 6,
                   line, RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
}

static void render_shop(struct toy_surface *surface,
                        const struct rasterfall_hud_state *state)
{
    static const int weapons[] = { TOY_GAME_WEAPON_SMG,
        TOY_GAME_WEAPON_SHOTGUN, TOY_GAME_WEAPON_AK, TOY_GAME_WEAPON_AWP,
        TOY_GAME_WEAPON_AXE, TOY_GAME_WEAPON_BOMB,
        TOY_GAME_WEAPON_MOLOTOV, TOY_GAME_WEAPON_PILL };
    static const int hire_weapons[] = { TOY_GAME_WEAPON_PISTOL,
        TOY_GAME_WEAPON_SMG, TOY_GAME_WEAPON_SHOTGUN, TOY_GAME_WEAPON_AK,
        TOY_GAME_WEAPON_AWP };
    static const char *hire_names[] = { "PISTOL", "SMG", "SG", "AK", "AWP" };
    static const char *player_names[] = { "SMG", "SG", "AK", "AWP", "AXE",
        "BOMB", "MOLOTOV", "PILL" };
    char line[64];
    int i, x = surface->width / 2 - 260, y = surface->height / 2 - 150;
    hud_fill_rect(surface, 0, 0, surface->width, surface->height, 0xD010151D);
    hud_fill_rect(surface, x, y, 520, 42, RF_COLOR_UI_PANEL_DARK);
    fb_draw_string((unsigned char *)surface->pixels, x + 18, y + 13,
                   "ARMORY", RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
    snprintf(line, sizeof(line), "$ %d", state->game->money);
    fb_draw_string((unsigned char *)surface->pixels, x + 400, y + 13,
                   line, RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
    if (!state->shop_page) {
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 78,
                       state->shop_nav_selected == 0 ? "> PLAYER WEAPONS" : "  PLAYER WEAPONS",
                       state->shop_nav_selected == 0 ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 102,
                       state->shop_nav_selected == 1 ? "> HIRE AI TEAMMATE" : "  HIRE AI TEAMMATE",
                       state->shop_nav_selected == 1 ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 126,
                       state->shop_nav_selected == 2 ? "> BUY FLAGS" : "  BUY FLAGS",
                       state->shop_nav_selected == 2 ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 150,
                       state->shop_nav_selected == 3 ? "> ASSIGN AI" : "  ASSIGN AI",
                       state->shop_nav_selected == 3 ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 174,
                       state->shop_nav_selected == 4 ? "> UPGRADE AI" : "  UPGRADE AI",
                       state->shop_nav_selected == 4 ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 198,
                       state->shop_nav_selected == 5 ? "> CHANGE AI WEAPON" : "  CHANGE AI WEAPON",
                       state->shop_nav_selected == 5 ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 222,
                       state->shop_nav_selected == 6 ? "> BASE COMBAT POWER" : "  BASE COMBAT POWER",
                       state->shop_nav_selected == 6 ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 16, y + 286,
                       "ENTER OPEN    ESC CLOSE", RF_COLOR_UI_TEXT, surface->stride);
        return;
    }
    if (state->shop_page == 3) {
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 70,
                       "BUY A NEW FLAG", RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 104,
                       "ENTER  PURCHASE       $250", RF_COLOR_UI_TEXT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 16, y + 286,
                       "ENTER BUY  ESC NAVIGATION", RF_COLOR_UI_TEXT, surface->stride);
        return;
    }
    if (state->shop_page == 4) {
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 58,
                       "SELECT FLAG TO ASSIGN", RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
        for (i = 0; i < state->flag_count && i < 8; i++) {
            snprintf(line, sizeof(line), "%sFLAG %d  ENTER SELECT",
                     i == state->shop_selected ? "> " : "  ", i + 1);
            fb_draw_string((unsigned char *)surface->pixels, x + 24, y + 88 + i * 28,
                           line, i == state->shop_selected ?
                           RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT,
                           surface->stride);
        }
        fb_draw_string((unsigned char *)surface->pixels, x + 16, y + 286,
                       "UP/DOWN SELECT  ENTER OPEN  ESC NAVIGATION", RF_COLOR_UI_TEXT, surface->stride);
        return;
    }
    if (state->shop_page == 5) {
        char assigned[256];
        int assigned_count = 0, pass, n = 0;
        assigned[0] = 0;
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 48,
                       "ASSIGNED TO THIS FLAG", RF_COLOR_UI_SUCCESS, surface->stride);
        for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
            const struct toy_game_actor *a = &state->game->actors[i];
            if (!a->active || a->kind != TOY_GAME_ACTOR_AI || a->base_core ||
                a->developer_only || a->flag_index != state->assignment_flag) continue;
            if (assigned_count) strcat(assigned, "  ");
            strcat(assigned, a->name);
            assigned_count++;
        }
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 70,
                       assigned_count ? assigned : "NONE",
                       RF_COLOR_UI_SUCCESS, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 104,
                       "ENTER ON ASSIGNED AI TO REMOVE", RF_COLOR_UI_TEXT, surface->stride);
        for (pass = 0; pass < 2; pass++) {
            for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
                const struct toy_game_actor *a = &state->game->actors[i];
                int assigned = a->flag_index == state->assignment_flag;
                int assigned_elsewhere = a->flag_index >= 0 && !assigned;
                if (!a->active || a->kind != TOY_GAME_ACTOR_AI || a->base_core ||
                    a->developer_only || assigned_elsewhere ||
                    (pass == 0 ? !assigned : assigned)) continue;
                if (n >= state->shop_scroll && n < state->shop_scroll + 6) {
                    int row = n - state->shop_scroll;
                    const char *level = a->class_id == TOY_GAME_AI_LEVEL_1 ? "LV1" :
                                        a->class_id == TOY_GAME_AI_LEVEL_3 ? "LV3" : "LV2";
                    const char *weapon = "NONE";
                    if (a->current_slot >= 0 && a->current_slot < TOY_GAME_WEAPON_SLOTS &&
                        a->slots[a->current_slot].weapon >= 0)
                        weapon = toy_game_weapon_name(a->slots[a->current_slot].weapon);
                    snprintf(line, sizeof(line), "%s  %s", a->name,
                             assigned ? "ASSIGNED" : "AVAILABLE");
                    if (n == state->shop_selected)
                        fb_draw_string((unsigned char *)surface->pixels, x + 24,
                                       y + 132 + row * 24, ">", 0xFFFFFF, surface->stride);
                    fb_draw_string((unsigned char *)surface->pixels, x + 40,
                                   y + 132 + row * 24, line,
                                   assigned ? RF_COLOR_UI_SUCCESS : 0xD0A05A,
                                   surface->stride);
                    fb_draw_string((unsigned char *)surface->pixels, x + 220,
                                   y + 132 + row * 24, level,
                                   0x76B7FF, surface->stride);
                    fb_draw_string((unsigned char *)surface->pixels, x + 270,
                                   y + 132 + row * 24, weapon,
                                   0xF0C674, surface->stride);
                }
                n++;
            }
        }
        if (state->shop_scroll > 0)
            fb_draw_string((unsigned char *)surface->pixels, x + 490, y + 132,
                           "^", RF_COLOR_UI_TEXT, surface->stride);
        if (n > state->shop_scroll + 6)
            fb_draw_string((unsigned char *)surface->pixels, x + 490, y + 264,
                           "v", RF_COLOR_UI_TEXT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 16, y + 286,
                       "UP/DOWN SELECT  ENTER TOGGLE  ESC FLAGS", RF_COLOR_UI_TEXT, surface->stride);
        return;
    }
    if (state->shop_page == 2) {
        for (i = 0; i < 5; i++) {
            int cy = y + 58 + i * 45;
            int price = TOY_CONFIG_AI_HIRE_PRICE +
                        (hire_weapons[i] == TOY_GAME_WEAPON_PISTOL ?
                         TOY_CONFIG_AI_HIRE_PISTOL_WEAPON_PRICE :
                         toy_game_weapon_price(hire_weapons[i]) *
                         TOY_CONFIG_AI_HIRE_WEAPON_PRICE_MULTIPLIER);
            hud_fill_rect(surface, x + 16, cy, 488, 36,
                          i == state->shop_selected ? 0x3C4E5B : RF_COLOR_UI_PANEL_DARK);
            snprintf(line, sizeof(line), "%sLV1  %s  $%d",
                     i == state->shop_selected ? "> " : "  ",
                     hire_names[i], price);
            fb_draw_string((unsigned char *)surface->pixels, x + 30, cy + 10,
                           line, i == state->shop_selected ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT,
                           surface->stride);
        }
        fb_draw_string((unsigned char *)surface->pixels, x + 16, y + 286,
                       "MONEY", RF_COLOR_UI_ACCENT, surface->stride);
        snprintf(line, sizeof(line), "$ %d", state->game->money);
        fb_draw_string((unsigned char *)surface->pixels, x + 68, y + 286,
                       line, RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 160, y + 286,
                       "UP/DOWN SELECT  ENTER HIRE  ESC NAVIGATION", RF_COLOR_UI_TEXT, surface->stride);
        return;
    }
    if (state->shop_page == 6) {
        int n = 0;
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 58,
                       "UPGRADE HIRED AI", RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
        for (i = 0; i < TOY_GAME_REMOTE_ACTOR_BASE; i++) {
            const struct toy_game_actor *a = &state->game->actors[i];
            int price, selected;
            if (!a->active || a->kind != TOY_GAME_ACTOR_AI || !a->hired) continue;
            selected = n == state->shop_selected;
            price = a->class_id == TOY_GAME_AI_LEVEL_1 ? TOY_CONFIG_AI_LEVEL_2_PRICE :
                    a->class_id == TOY_GAME_AI_LEVEL_2 ? TOY_CONFIG_AI_LEVEL_3_PRICE : 0;
            snprintf(line, sizeof(line), "%s%s  LV%d  %s",
                     selected ? "> " : "  ", a->name, a->class_id + 1,
                     price ? "UPGRADE" : "MAX LEVEL");
            fb_draw_string((unsigned char *)surface->pixels, x + 24, y + 90 + n * 28,
                           line, selected ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT,
                           surface->stride);
            if (price) {
                snprintf(line, sizeof(line), "$%d", price);
                fb_draw_string((unsigned char *)surface->pixels, x + 390,
                               y + 90 + n * 28, line, RF_COLOR_UI_ACCENT, surface->stride);
            }
            n++;
        }
        if (!n) fb_draw_string((unsigned char *)surface->pixels, x + 24, y + 96,
                               "NO HIRED AI", RF_COLOR_UI_TEXT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 16, y + 286,
                       "UP/DOWN SELECT  ENTER UPGRADE  ESC NAVIGATION",
                       RF_COLOR_UI_TEXT, surface->stride);
        return;
    }
    if (state->shop_page == 7) {
        int n = 0;
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 58,
                       "SELECT AI", RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
        for (i = 0; i < TOY_GAME_REMOTE_ACTOR_BASE; i++) {
            const struct toy_game_actor *a = &state->game->actors[i];
            const char *weapon;
            if (!a->active || a->kind != TOY_GAME_ACTOR_AI || !a->hired) continue;
            weapon = a->current_slot >= 0 &&
                     a->current_slot < TOY_GAME_WEAPON_SLOTS &&
                     a->slots[a->current_slot].weapon >= 0 ?
                     toy_game_weapon_name(a->slots[a->current_slot].weapon) : "NONE";
            snprintf(line, sizeof(line), "%s%s  LV%d  %s",
                     n == state->shop_selected ? "> " : "  ", a->name,
                     a->class_id + 1, weapon);
            fb_draw_string((unsigned char *)surface->pixels, x + 24,
                           y + 90 + n * 28, line,
                           n == state->shop_selected ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT,
                           surface->stride);
            n++;
        }
        if (!n) fb_draw_string((unsigned char *)surface->pixels, x + 24, y + 96,
                               "NO HIRED AI", RF_COLOR_UI_TEXT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 16, y + 286,
                       "UP/DOWN SELECT  ENTER WEAPONS  ESC NAVIGATION",
                       RF_COLOR_UI_TEXT, surface->stride);
        return;
    }
    if (state->shop_page == 8) {
        static const int ai_weapons[] = { TOY_GAME_WEAPON_PISTOL,
            TOY_GAME_WEAPON_SMG, TOY_GAME_WEAPON_SHOTGUN,
            TOY_GAME_WEAPON_AK, TOY_GAME_WEAPON_AWP };
        static const char *ai_weapon_names[] = { "PISTOL", "SMG", "SG", "AK", "AWP" };
        for (i = 0; i < 5; i++) {
            int cy = y + 58 + i * 45;
            int price = ai_weapons[i] == TOY_GAME_WEAPON_PISTOL ? 0 :
                        toy_game_weapon_price(ai_weapons[i]) *
                        TOY_CONFIG_AI_HIRE_WEAPON_PRICE_MULTIPLIER;
            hud_fill_rect(surface, x + 16, cy, 488, 36,
                          i == state->shop_selected ? 0x3C4E5B : RF_COLOR_UI_PANEL_DARK);
            snprintf(line, sizeof(line), "%s%s  $%d", i == state->shop_selected ? "> " : "  ",
                     ai_weapon_names[i], price);
            fb_draw_string((unsigned char *)surface->pixels, x + 30, cy + 10,
                           line, i == state->shop_selected ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT,
                           surface->stride);
        }
        fb_draw_string((unsigned char *)surface->pixels, x + 16, y + 286,
                       "UP/DOWN SELECT  ENTER CHANGE  ESC AI LIST",
                       RF_COLOR_UI_TEXT, surface->stride);
        return;
    }
    if (state->shop_page == 9) {
        int total_power = 0, n = 0;
        fb_draw_string((unsigned char *)surface->pixels, x + 20, y + 58,
                       "BASE COMBAT POWER", RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
        for (i = 0; i < TOY_GAME_REMOTE_ACTOR_BASE; i++) {
            const struct toy_game_actor *a = &state->game->actors[i];
            const char *weapon = "NONE";
            const char *level;
            int power;
            if (!a->active || a->kind != TOY_GAME_ACTOR_AI || !a->hired) continue;
            level = a->class_id == TOY_GAME_AI_LEVEL_1 ? "LV1" :
                    a->class_id == TOY_GAME_AI_LEVEL_3 ? "LV3" : "LV2";
            if (a->current_slot >= 0 && a->current_slot < TOY_GAME_WEAPON_SLOTS &&
                a->slots[a->current_slot].weapon >= 0)
                weapon = toy_game_weapon_name(a->slots[a->current_slot].weapon);
            power = toy_game_actor_combat_power(a);
            total_power += power;
            if (n < 6) {
                snprintf(line, sizeof(line), "%s  %s  %s  CP %d",
                         a->name, level, weapon, power);
                fb_draw_string((unsigned char *)surface->pixels, x + 24,
                               y + 92 + n * 28, line,
                               RF_COLOR_UI_TEXT, surface->stride);
            }
            n++;
        }
        if (!n)
            fb_draw_string((unsigned char *)surface->pixels, x + 24, y + 96,
                           "NO HIRED AI", RF_COLOR_UI_TEXT, surface->stride);
        snprintf(line, sizeof(line), "TOTAL CP  %d", total_power);
        fb_draw_string((unsigned char *)surface->pixels, x + 300, y + 258,
                       line, RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, x + 16, y + 286,
                       "ESC NAVIGATION", RF_COLOR_UI_TEXT, surface->stride);
        return;
    }
    for (i = 0; i < 8; i++) {
        int cx = x + 16;
        int cy = y + 56 + i * 27;
        int selected = i == state->shop_selected;
        int unlocked = toy_game_weapon_unlocked(state->game, weapons[i]);
        int quantity = -1, quantity_max = 0;
        if (weapons[i] == TOY_GAME_WEAPON_BOMB ||
            weapons[i] == TOY_GAME_WEAPON_MOLOTOV) {
            quantity = state->game->slots[2].weapon == weapons[i] ?
                       state->game->slots[2].mag : 0;
            quantity_max = TOY_GAME_THROWABLE_MAX;
        } else if (weapons[i] == TOY_GAME_WEAPON_PILL) {
            quantity = state->game->slots[3].weapon == weapons[i] ?
                       state->game->slots[3].mag : 0;
            quantity_max = TOY_GAME_PILL_MAX;
        }
        hud_fill_rect(surface, cx, cy, 488, 24,
                      selected ? 0x3C4E5B : RF_COLOR_UI_PANEL_DARK);
        fb_draw_string((unsigned char *)surface->pixels, cx + 10, cy + 6,
                       player_names[i], selected ? RF_COLOR_UI_ACCENT_BRIGHT : RF_COLOR_UI_TEXT,
                       surface->stride);
        if (quantity >= 0 && unlocked)
            snprintf(line, sizeof(line), "%d/%d  BUY $%d", quantity,
                     quantity_max, toy_game_weapon_price(weapons[i]));
        else if (unlocked)
            snprintf(line, sizeof(line), "OWNED  ENTER EQUIP");
        else
            snprintf(line, sizeof(line), "LOCKED  BUY $%d",
                     toy_game_weapon_price(weapons[i]));
        fb_draw_string((unsigned char *)surface->pixels, cx + 170, cy + 6,
                       line, unlocked ? RF_COLOR_UI_SUCCESS : RF_COLOR_UI_DANGER,
                       surface->stride);
    }
    fb_draw_string((unsigned char *)surface->pixels, x + 16,
                   y + 286, "MONEY", RF_COLOR_UI_ACCENT, surface->stride);
    snprintf(line, sizeof(line), "$ %d", state->game->money);
    fb_draw_string((unsigned char *)surface->pixels, x + 68, y + 286,
                   line, RF_COLOR_UI_ACCENT_BRIGHT, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels, x + 160, y + 286,
                   "UP/DOWN SELECT  ENTER BUY/EQUIP  ESC CLOSE", RF_COLOR_UI_TEXT,
                   surface->stride);
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
    if (game->slots[3].weapon == TOY_GAME_WEAPON_PILL) {
        int px = bar_x + bar_w + 18, py = bar_y - 2;
        hud_fill_rect(surface, px, py, 30, 24, 0xE8EEE8);
        hud_fill_rect(surface, px + 12, py + 4, 6, 16, 0x20B84B);
        hud_fill_rect(surface, px + 7, py + 9, 16, 6, 0x20B84B);
        snprintf(line, sizeof(line), "%d", game->slots[3].mag);
        fb_draw_string((unsigned char *)surface->pixels, px + 35, py + 5,
                       line, 0x70E090, surface->stride);
    }
}

static void render_revive_prompt(struct toy_surface *surface,
                                 const struct rasterfall_hud_state *state)
{
    const struct toy_game *game = state->game;
    char line[64];
    int width, x, y = surface->height / 2 + 24;
    if (game->player_down) {
        snprintf(line, sizeof(line), "F REVIVE $%d   WAIT FOR RESCUE",
                 RASTERFALL_PAID_REVIVE_COST);
    } else if (!state->ai_revive_available &&
                              !state->ai_revive_active &&
                              !state->player_revive_available &&
                              !state->player_revive_active) return;
    else if (state->player_revive_active) {
        snprintf(line, sizeof(line), "REVIVING %s  %d%%",
                 state->player_revive_name ? state->player_revive_name : "PLAYER",
                 state->player_revive_progress_ms * 100 / TOY_GAME_REVIVE_MS);
    } else if (state->player_revive_available) {
        snprintf(line, sizeof(line), "E REVIVE %s",
                 state->player_revive_name ? state->player_revive_name : "PLAYER");
    } else if (state->ai_revive_active) {
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
    if (state->player_revive_active || state->ai_revive_active) {
        int bar_x = surface->width / 2 - 64;
        int progress = state->player_revive_active ?
            state->player_revive_progress_ms : state->ai_revive_progress_ms;
        hud_fill_rect(surface, bar_x, y + FB_FONT_H + 5, 128, 5, RF_COLOR_UI_PANEL);
        hud_fill_rect(surface, bar_x, y + FB_FONT_H + 5,
                      progress * 128 / TOY_GAME_REVIVE_MS, 5,
                      RF_COLOR_UI_PLAYER);
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
        int connected = 0;
        for (int i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++)
            if (net->clients[i].active && net->clients[i].connected) connected++;
        snprintf(line, sizeof(line), "HOST  %d/%d CLIENTS", connected,
                 RASTERFALL_NET_CLIENT_MAX);
        color = connected ? RF_COLOR_UI_SECONDARY : 0xFFD070;
    } else if (net->connected) {
        snprintf(line, sizeof(line), "CLIENT P%d  CONNECTED  RTT %d MS",
                 net->local_player_id + 1, net->rtt_ms);
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
    if (net->mode == RASTERFALL_NET_HOST) {
        int line_y = y + FB_FONT_H * 2;
        int cursor = 0;
        line[0] = 0;
        for (int i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
            const struct rasterfall_net_client *client = &net->clients[i];
            if (!client->active || !client->connected) continue;
            snprintf(line + cursor, sizeof(line) - (size_t)cursor,
                     "%sP%d %dMS %d.%d%%", cursor ? "  " : "",
                     client->client_id + 1, client->rtt_ms,
                     client->loss_permille / 10,
                     client->loss_permille % 10);
            cursor = (int)strlen(line);
        }
        width = (int)strlen(line) * FB_FONT_W;
        x = surface->width - width - 10;
        if (x < 8) x = 8;
        hud_fill_rect(surface, x - 4, line_y - 2, width + 8,
                      FB_FONT_H + 4, 0x182634);
        fb_draw_string((unsigned char *)surface->pixels, x, line_y, line,
                       RF_COLOR_UI_SECONDARY, surface->stride);
    }
    {
        int line_y = y + FB_FONT_H *
            (net->mode == RASTERFALL_NET_HOST ? 3 : 2);
        int avg_rtt = net->net_stats_avg_rtt_ms > 0 ?
                      net->net_stats_avg_rtt_ms : net->rtt_ms;
        int tx_kbps = net->net_stats_tx_bps / 1024;
        int rx_kbps = net->net_stats_rx_bps / 1024;
        /* Small client packets can be below 1 KiB/s. Keep an active link
         * visibly non-zero instead of making the client look unconnected. */
        if (net->net_stats_tx_bps > 0 && tx_kbps == 0) tx_kbps = 1;
        if (net->net_stats_rx_bps > 0 && rx_kbps == 0) rx_kbps = 1;
        int loss_major = net->net_stats_loss_permille / 10;
        int loss_minor = net->net_stats_loss_permille % 10;
        snprintf(line, sizeof(line), "NET AVG %dMS TX %d RX %dKB/S LOSS %d.%d%%",
                 avg_rtt, tx_kbps, rx_kbps,
                 loss_major, loss_minor);
        width = (int)strlen(line) * FB_FONT_W;
        x = surface->width - width - 10;
        if (x < 8) x = 8;
        hud_fill_rect(surface, x - 4, line_y - 2, width + 8,
                      FB_FONT_H + 4, 0x182634);
        fb_draw_string((unsigned char *)surface->pixels, x, line_y, line,
                       RF_COLOR_UI_SECONDARY, surface->stride);
        line_y += FB_FONT_H;
        snprintf(line, sizeof(line), "P/E/W %lu/%lu/%lu INPUT REC %lu DUP %lu",
                 net->player_snapshots_received,
                 net->entity_snapshots_received,
                 net->world_snapshots_received,
                 net->input_recovered, net->input_duplicates);
        width = (int)strlen(line) * FB_FONT_W;
        x = surface->width - width - 10;
        if (x < 8) x = 8;
        hud_fill_rect(surface, x - 4, line_y - 2, width + 8,
                      FB_FONT_H + 4, 0x182634);
        fb_draw_string((unsigned char *)surface->pixels, x, line_y, line,
                       RF_COLOR_UI_SECONDARY, surface->stride);
    }
}

static void render_wave_hud(struct toy_surface *surface,
                            const struct toy_game *game, int fps)
{
    char value[32];
    int x = 8;
    const char *status;
    if (game->campaign_phase == TOY_GAME_PHASE_HORDE)
        status = "ACTIVE";
    else if (game->campaign_phase == TOY_GAME_PHASE_BUILDUP)
        status = "STARTING";
    else
        status = game->wave >= TOY_GAME_WAVE_MAX ? "COMPLETE" : "REST";
    hud_value_y = 8;
    x = draw_hud_value(surface, x, "WAVE ", status,
                       game->campaign_phase == TOY_GAME_PHASE_HORDE ?
                       0xFFD040 : RF_COLOR_UI_SECONDARY);
    snprintf(value, sizeof(value), "%d/%d", game->wave, TOY_GAME_WAVE_MAX);
    x = draw_hud_value(surface, x, "NUMBER ", value, 0xFFD070);
    if (game->campaign_phase == TOY_GAME_PHASE_HORDE ||
        game->campaign_phase == TOY_GAME_PHASE_BUILDUP) {
        snprintf(value, sizeof(value), "%d", game->wave_attack_points);
        x = draw_hud_value(surface, x, "ATTACK ", value, 0xFFD070);
    }
    snprintf(value, sizeof(value), "%d", game->enemies_alive);
    x = draw_hud_value(surface, x, "LIVE ", value, 0xF0F0F0);
    if ((game->campaign_phase == TOY_GAME_PHASE_CALM &&
         game->spawn_timer_ms > 0) || game->phase_timer_ms > 0) {
        snprintf(value, sizeof(value), "%dS",
                 ((game->campaign_phase == TOY_GAME_PHASE_CALM ?
                   game->spawn_timer_ms : game->phase_timer_ms) + 999) / 1000);
        x = draw_hud_value(surface, x,
                           game->campaign_phase == TOY_GAME_PHASE_BUILDUP ?
                           "BEGIN " : "NEXT ", value, 0x80C8FF);
    }
    snprintf(value, sizeof(value), "%d", fps);
    draw_hud_value(surface, x, "FPS ", value, 0x90F090);

    /* Detailed queue counts occupy their own row so they do not run into
     * the network/weapon HUD on the right side of the screen. */
    if (game->campaign_phase == TOY_GAME_PHASE_HORDE ||
        game->campaign_phase == TOY_GAME_PHASE_BUILDUP) {
        hud_value_y = 30;
        x = 8;
        snprintf(value, sizeof(value), "%d", game->wave_waiting_common);
        x = draw_hud_value(surface, x, "COMMON ", value, 0xD0D0D0);
        snprintf(value, sizeof(value), "%d", game->wave_waiting_fast);
        x = draw_hud_value(surface, x, "FAST ", value, 0xFF8060);
        snprintf(value, sizeof(value), "%d", game->wave_waiting_heavy);
        x = draw_hud_value(surface, x, "HEAVY ", value, 0xC89070);
        snprintf(value, sizeof(value), "%d", game->wave_waiting_special);
        x = draw_hud_value(surface, x, "SPECIAL ", value, 0xD080FF);
        snprintf(value, sizeof(value), "%d", game->wave_waiting_tank);
        x = draw_hud_value(surface, x, "TANK ", value, 0xFF4040);
    }
    hud_value_y = 8;
}

void rasterfall_hud_render(struct toy_surface *surface, int fps,
                           const struct rasterfall_hud_state *state)
{
    const struct toy_game *game = state->game;
    render_wave_hud(surface, game, fps);
    render_network_hud(surface, state->net, state->host_address, state->host_port);
    render_weapon_hud(surface, game);
    render_money(surface, game);
    render_player_hud(surface, game, state->player_name);
    render_revive_prompt(surface, state);
    if (state->shop_open) render_shop(surface, state);
    if (state->horde_banner_ms > 0 && state->interaction_banner) {
        int banner_y = surface->height / 3;
        fb_draw_string((unsigned char *)surface->pixels,
                       (surface->width - (int)strlen(state->interaction_banner) * FB_FONT_W) / 2,
                       banner_y, state->interaction_banner,
                       state->interaction_banner_success ? RF_COLOR_UI_SUCCESS : RF_COLOR_UI_DANGER,
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
    if (state->flag_carried || state->flag_near) {
        snprintf(label, sizeof(label), "F %s FLAG",
                 state->flag_carried ? "PLANT" : "CARRY");
        text_w = (int)strlen(label) * FB_FONT_W;
        x = (renderer->surface.width - text_w) / 2;
        y = renderer->surface.height - FB_FONT_H - 18;
        hud_fill_rect(&renderer->surface, x - 5, y - 3, text_w + 10,
                      FB_FONT_H + 6, RF_COLOR_UI_BACKGROUND);
        fb_draw_string((unsigned char *)renderer->surface.pixels, x, y,
                       label, RF_COLOR_UI_ACCENT, renderer->surface.stride);
        return;
    }
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
    else if (it->kind == TOY_MAP_PICKUP_TANK_BUTTON)
        snprintf(label, sizeof(label), "E SUMMON TANK");
    else if (it->kind == TOY_MAP_PICKUP_BASE_1_BUTTON)
        snprintf(label, sizeof(label), "E OPEN BASE 1 GATE");
    else if (it->kind == TOY_MAP_PICKUP_BASE_2_BUTTON)
        snprintf(label, sizeof(label), "E OPEN BASE 2 GATE");
    else if (it->kind == TOY_MAP_PICKUP_AMMO)
        snprintf(label, sizeof(label), "E TAKE AMMO");
    else if (it->kind == TOY_MAP_PICKUP_SHOP)
        snprintf(label, sizeof(label), "E OPEN ARMORY");
    else if (it->kind == TOY_MAP_PICKUP_MONEY_BUTTON)
        snprintf(label, sizeof(label), "E ADD $500");
    else if (it->kind == TOY_MAP_PICKUP_CLEAR_HIRED_BUTTON)
        snprintf(label, sizeof(label), "E CLEAR HIRED AI");
    else if (it->kind == TOY_MAP_PICKUP_POSE_RESET_BUTTON)
        snprintf(label, sizeof(label), "E: RESET POSE");
    else if (it->kind == TOY_MAP_PICKUP_POSE_RIGHT_ARM_BUTTON)
        snprintf(label, sizeof(label), "E: RIGHT ARM");
    else if (it->kind == TOY_MAP_PICKUP_POSE_ARMS_BUTTON)
        snprintf(label, sizeof(label), "E: ARMS");
    else if (it->kind == TOY_MAP_PICKUP_POSE_BODY_BUTTON)
        snprintf(label, sizeof(label), "E: BODY TURN");
    else if (it->kind == TOY_MAP_PICKUP_WEAPON ||
             it->kind == TOY_MAP_PICKUP_SMG ||
             it->kind == TOY_MAP_PICKUP_SHOTGUN) {
        int weapon = it->kind == TOY_MAP_PICKUP_WEAPON ? it->weapon :
                     it->kind == TOY_MAP_PICKUP_SMG ? TOY_GAME_WEAPON_SMG :
                     TOY_GAME_WEAPON_SHOTGUN;
        const char *name = toy_game_weapon_name(weapon);
        snprintf(label, sizeof(label), "E %s %s",
                 state->game->slots[0].weapon == weapon ? "REFILL" : "PICK UP",
                 name);
    } else {
        snprintf(label, sizeof(label), "E INTERACT");
    }
    text_w = (int)strlen(label) * FB_FONT_W;
    x = (renderer->surface.width - text_w) / 2;
    y = renderer->surface.height - FB_FONT_H - 18;
    hud_fill_rect(&renderer->surface, x - 5, y - 3, text_w + 10, FB_FONT_H + 6,
                  RF_COLOR_UI_BACKGROUND);
    fb_draw_string((unsigned char *)renderer->surface.pixels, x, y,
                   label, RF_COLOR_UI_ACCENT, renderer->surface.stride);
}

int rasterfall_hud_dump_frame(const char *path, const struct toy_surface *surface)
{
    char header[48];
    unsigned char *rgb;
    int hlen, fd, i;
    if (!surface || !surface->pixels || surface->width <= 0 || surface->height <= 0)
        return -1;
    hlen = snprintf(header, sizeof(header), "P6\n%d %d\n255\n",
                    surface->width, surface->height);
    rgb = tlibc_malloc((size_t)surface->width * surface->height * 3);
    if (!rgb) return -1;
    for (i = 0; i < surface->width * surface->height; i++) {
        uint32_t p = ((uint32_t *)surface->pixels)[i];
        rgb[i * 3 + 0] = (unsigned char)((p >> 16) & 0xFF);
        rgb[i * 3 + 1] = (unsigned char)((p >> 8) & 0xFF);
        rgb[i * 3 + 2] = (unsigned char)(p & 0xFF);
    }
    fd = __creat(path, 0644);
    if (fd >= 0) {
        __write(fd, header, hlen);
        __write(fd, rgb, surface->width * surface->height * 3);
        __close(fd);
        __printf("rasterfall: dumped frame to %s\n", path);
    } else {
        tlibc_free(rgb);
        return -1;
    }
    tlibc_free(rgb);
    return 0;
}

static void dump_put_u16(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static void dump_put_u32(unsigned char *p, unsigned int value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

int rasterfall_hud_dump_bmp(const char *path, const struct toy_surface *surface)
{
    unsigned char header[54];
    unsigned char *data;
    unsigned int row_bytes, data_size;
    int fd, x, y;
    if (!surface || !surface->pixels || surface->width <= 0 ||
        surface->height <= 0) return -1;
    row_bytes = ((unsigned int)surface->width * 3U + 3U) & ~3U;
    if (row_bytes > 0xffffffffU / (unsigned int)surface->height) return -1;
    data_size = row_bytes * (unsigned int)surface->height;
    data = tlibc_malloc(data_size);
    if (!data) return -1;
    memset(data, 0, data_size);
    for (y = 0; y < surface->height; y++) {
        unsigned char *out = data + (surface->height - 1 - y) * row_bytes;
        const uint32_t *in = (const uint32_t *)((const unsigned char *)surface->pixels +
                                                y * surface->stride);
        for (x = 0; x < surface->width; x++) {
            out[x * 3 + 0] = (unsigned char)(in[x] & 0xff);
            out[x * 3 + 1] = (unsigned char)((in[x] >> 8) & 0xff);
            out[x * 3 + 2] = (unsigned char)((in[x] >> 16) & 0xff);
        }
    }
    memset(header, 0, sizeof(header));
    header[0] = 'B'; header[1] = 'M';
    dump_put_u32(header + 2, 54U + data_size);
    dump_put_u32(header + 10, 54);
    dump_put_u32(header + 14, 40);
    dump_put_u32(header + 18, (unsigned int)surface->width);
    dump_put_u32(header + 22, (unsigned int)surface->height);
    dump_put_u16(header + 26, 1);
    dump_put_u16(header + 28, 24);
    dump_put_u32(header + 34, data_size);
    fd = __creat(path, 0644);
    if (fd < 0 || __write(fd, header, sizeof(header)) != (int)sizeof(header) ||
        __write(fd, data, data_size) != (int)data_size) {
        if (fd >= 0) __close(fd);
        tlibc_free(data);
        return -1;
    }
    __close(fd);
    tlibc_free(data);
    __printf("rasterfall: dumped model view to %s\n", path);
    return 0;
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
