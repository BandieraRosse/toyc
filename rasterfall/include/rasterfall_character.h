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
