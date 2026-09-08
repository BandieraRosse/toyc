#include "rasterfall_character.h"

#define ALL_ACTIONS (RASTERFALL_CHARACTER_ACTION_LOCOMOTION | \
                     RASTERFALL_CHARACTER_ACTION_WEAPON | \
                     RASTERFALL_CHARACTER_ACTION_MELEE | \
                     RASTERFALL_CHARACTER_ACTION_THROW | \
                     RASTERFALL_CHARACTER_ACTION_INCAPACITATED)

/* The initial cast deliberately uses the same procedural geometry.  The
 * catalog is the migration seam for per-character RFM2 assets and authored
 * clips; gameplay and networking only retain the stable profile ID. */
static const struct rasterfall_character_profile characters[] = {
    { RASTERFALL_CHARACTER_AKARI, "Akari", NULL, ALL_ACTIONS,
      0xD94F70, 0x542F55, 0xF0C3A5, 0x512B3A },
    { RASTERFALL_CHARACTER_MIO, "Mio", NULL, ALL_ACTIONS,
      0x4C78C2, 0x263A63, 0xEBC0A2, 0x25243D },
    { RASTERFALL_CHARACTER_REN, "Ren", NULL, ALL_ACTIONS,
      0x4FAF82, 0x294F48, 0xD9A47F, 0x33271F },
    { RASTERFALL_CHARACTER_YUKI, "Yuki", NULL, ALL_ACTIONS,
      0x9B70C7, 0x49365F, 0xF1C8B0, 0xD8DCE8 }
};

const struct rasterfall_character_profile *rasterfall_character_profile(int id)
{
    if (id < 0 || id >= RASTERFALL_CHARACTER_COUNT)
        id = RASTERFALL_CHARACTER_AKARI;
    return &characters[id];
}

int rasterfall_character_for_actor(int actor_id, int class_id)
{
    unsigned int seed = (unsigned int)(actor_id < 0 ? -actor_id : actor_id);
    seed += (unsigned int)(class_id < 0 ? 0 : class_id) * 3U;
    return (int)(seed % RASTERFALL_CHARACTER_COUNT);
}

int rasterfall_character_logic_test(void)
{
    int i;
    for (i = 0; i < RASTERFALL_CHARACTER_COUNT; i++) {
        const struct rasterfall_character_profile *profile =
            rasterfall_character_profile(i);
        if (profile->id != i || !profile->name ||
            (profile->actions & ALL_ACTIONS) != ALL_ACTIONS)
            return 1;
    }
    return rasterfall_character_profile(-1)->id != RASTERFALL_CHARACTER_AKARI;
}

static const struct rasterfall_profession_visual_profile professions[] = {
    {0, 0, RF_HEAD_NONE, RF_BADGE_NONE, RF_BAG_NONE, 0, 0},
    {0xD28A30, 0x414957, RF_HEAD_GOGGLES, RF_BADGE_TOOL, RF_BAG_TOOLS, 0, 0},
    {0xB5A06C, 0x607384, RF_HEAD_CAP, RF_BADGE_CRATE, RF_BAG_NONE, 1, 0},
    {0x30965C, 0xD3DDD5, RF_HEAD_MEDICAL_BAND, RF_BADGE_CROSS, RF_BAG_MEDICAL, 0, 0},
    {0x80966A, 0x37483F, RF_HEAD_HELMET_BAND, RF_BADGE_SHIELD, RF_BAG_NONE, 0, 1}
};

const struct rasterfall_profession_visual_profile *
rasterfall_profession_visual_profile(int profession_id)
{
    if (profession_id <= RASTERFALL_PROFESSION_NONE ||
        profession_id >= RASTERFALL_PROFESSION_COUNT) return NULL;
    return &professions[profession_id];
}
