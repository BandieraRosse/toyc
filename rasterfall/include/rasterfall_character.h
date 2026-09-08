#ifndef RASTERFALL_CHARACTER_H
#define RASTERFALL_CHARACTER_H

#include "tlibc_types.h"

/* Stable gameplay-facing visual identities.  Keep these IDs independent of
 * asset paths: a profile may start as a procedural actor and later acquire a
 * skeletal model without changing saves or network state. */
enum rasterfall_character_id {
    RASTERFALL_CHARACTER_AKARI,
    RASTERFALL_CHARACTER_MIO,
    RASTERFALL_CHARACTER_REN,
    RASTERFALL_CHARACTER_YUKI,
    RASTERFALL_CHARACTER_COUNT
};

/* Independent identity: professions do not select a body or grant abilities. */
enum rasterfall_profession_id {
    RASTERFALL_PROFESSION_NONE,
    RASTERFALL_PROFESSION_GUNSMITH,
    RASTERFALL_PROFESSION_LOGISTICS,
    RASTERFALL_PROFESSION_MEDIC,
    RASTERFALL_PROFESSION_GUARD,
    RASTERFALL_PROFESSION_COUNT
};

enum rasterfall_profession_head { RF_HEAD_NONE, RF_HEAD_GOGGLES, RF_HEAD_CAP,
                                 RF_HEAD_MEDICAL_BAND, RF_HEAD_HELMET_BAND };
enum rasterfall_profession_badge { RF_BADGE_NONE, RF_BADGE_TOOL, RF_BADGE_CRATE,
                                  RF_BADGE_CROSS, RF_BADGE_SHIELD };
enum rasterfall_profession_bag { RF_BAG_NONE, RF_BAG_TOOLS, RF_BAG_MEDICAL };

/* Presentation-only, deliberately limited to the four current silhouettes.
 * Zero/NONE resolves to NULL and leaves base appearance and draw order intact. */
struct rasterfall_profession_visual_profile {
    uint32_t accent_color, gear_color;
    int head, badge, waist_bag;
    int backpack; /* Large supply pack. */
    int vest;     /* Broad, thick torso shell. */
};
const struct rasterfall_profession_visual_profile *
rasterfall_profession_visual_profile(int profession_id);

enum rasterfall_character_action {
    RASTERFALL_CHARACTER_ACTION_LOCOMOTION = 1 << 0,
    RASTERFALL_CHARACTER_ACTION_WEAPON = 1 << 1,
    RASTERFALL_CHARACTER_ACTION_MELEE = 1 << 2,
    RASTERFALL_CHARACTER_ACTION_THROW = 1 << 3,
    RASTERFALL_CHARACTER_ACTION_INCAPACITATED = 1 << 4
};

struct rasterfall_character_profile {
    int id;
    const char *name;
    const char *model_path; /* NULL selects the procedural actor renderer. */
    unsigned int actions;
    uint32_t body_color;
    uint32_t leg_color;
    uint32_t skin_color;
    uint32_t hair_color;
};

const struct rasterfall_character_profile *rasterfall_character_profile(int id);
int rasterfall_character_for_actor(int actor_id, int class_id);
int rasterfall_character_logic_test(void);

#endif
