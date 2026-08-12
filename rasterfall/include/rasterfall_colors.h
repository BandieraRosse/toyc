/* Rasterfall's small semantic palette.  Values are 0xRRGGBB. */
#ifndef RASTERFALL_COLORS_H
#define RASTERFALL_COLORS_H

/* Shared UI colors. */
#define RF_COLOR_UI_BACKGROUND       0x171B24 /* 主背景：深蓝黑 */
#define RF_COLOR_UI_PANEL            0x20252B /* 面板底色：深灰 */
#define RF_COLOR_UI_PANEL_DARK       0x18232D /* 深色面板 */
#define RF_COLOR_UI_TEXT             0xE7E9EC /* 主文字：浅灰白 */
#define RF_COLOR_UI_TEXT_MUTED       0x9AA6B4 /* 次要文字：灰蓝 */
#define RF_COLOR_UI_TEXT_DIM         0x73808D /* 弱化文字：暗灰蓝 */
#define RF_COLOR_UI_ACCENT           0xFFD060 /* 强调色：金黄 */
#define RF_COLOR_UI_ACCENT_BRIGHT    0xFFF0B0 /* 强调亮色：淡黄 */
#define RF_COLOR_UI_DANGER           0xF03030 /* 危险/低血量：红色 */
#define RF_COLOR_UI_WARNING          0xF0C830 /* 警告：橙黄色 */
#define RF_COLOR_UI_SUCCESS          0x40D060 /* 成功/安全：绿色 */
#define RF_COLOR_UI_PLAYER           0x70D8FF /* 玩家标识：亮蓝 */
#define RF_COLOR_UI_AI               0x80E080 /* AI 标识：浅绿 */
#define RF_COLOR_UI_SECONDARY        0x80E0C0 /* 第二玩家/联机：青绿 */

/* Gameplay identity colors. */
#define RF_COLOR_ENEMY_COMMON        0x4A5D3A /* 普通敌人：橄榄绿 */
#define RF_COLOR_ENEMY_PURSUIT_COMMON RF_COLOR_ENEMY_COMMON /* PURSUIT_COMMON：沿用普通敌人颜色 */
#define RF_COLOR_ENEMY_HEAVY         0x624A3A /* 重型敌人：棕灰 */
#define RF_COLOR_ENEMY_PURSUIT_HEAVY 0x7A4A2A /* 重型追击者：深橙棕 */
#define RF_COLOR_ENEMY_PURSUIT_FAST  0xB84A32 /* 快速追击者：赤橙 */
#define RF_COLOR_ENEMY_SMOKER        0x76513A /* Smoker：烟棕色 */
#define RF_COLOR_ENEMY_CHARGER      0x8B5A35 /* Charger：棕色 */
#define RF_COLOR_AI_BASIC            0x596B3A /* 基础 AI：暗橄榄绿 */
#define RF_COLOR_AI_RIFLE            0x386B96 /* 步枪 AI：蓝色 */
#define RF_COLOR_AI_HEAVY            0x252A30 /* 重型 AI：近黑灰 */

#endif
