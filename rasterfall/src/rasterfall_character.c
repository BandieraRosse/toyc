#include "rasterfall_character.h"
#include "string.h"

#define ALL_ACTIONS (RASTERFALL_CHARACTER_ACTION_LOCOMOTION | \
                     RASTERFALL_CHARACTER_ACTION_WEAPON | \
                     RASTERFALL_CHARACTER_ACTION_MELEE | \
                     RASTERFALL_CHARACTER_ACTION_THROW | \
                     RASTERFALL_CHARACTER_ACTION_INCAPACITATED)

/* The initial cast deliberately uses the same procedural geometry.  The
 * catalog is the migration seam for per-character RFM2 assets and authored
 * clips; gameplay and networking only retain the stable profile ID. */
static const struct rasterfall_character_profile characters[] = {
    { RASTERFALL_CHARACTER_HURD_GUNSMITH, RASTERFALL_PROFESSION_GUNSMITH,
      "Akari", NULL, ALL_ACTIONS,
      0xD94F70, 0x542F55, 0xF0C3A5, 0x512B3A },
    { RASTERFALL_CHARACTER_HURD_LOGISTICS, RASTERFALL_PROFESSION_LOGISTICS,
      "Mio", NULL, ALL_ACTIONS,
      0x4C78C2, 0x263A63, 0xEBC0A2, 0x25243D },
    { RASTERFALL_CHARACTER_HURD_MEDIC, RASTERFALL_PROFESSION_MEDIC,
      "Ren", NULL, ALL_ACTIONS,
      0x4FAF82, 0x294F48, 0xD9A47F, 0x33271F },
    { RASTERFALL_CHARACTER_HURD_GUARD, RASTERFALL_PROFESSION_GUARD,
      "Yuki", NULL, ALL_ACTIONS,
      0x9B70C7, 0x49365F, 0xF1C8B0, 0xD8DCE8 },
    { RASTERFALL_CHARACTER_MAID, RASTERFALL_PROFESSION_MAID,
      "Maid", NULL, ALL_ACTIONS,
      0x30343B, 0x20242B, 0xF1C8B0, 0x3A302F },
    { RASTERFALL_CHARACTER_RF_RIFLEMAN, RASTERFALL_PROFESSION_NONE,
      "RF Rifleman", "rf_humanoid_v2", ALL_ACTIONS,
      0x6F8461, 0x84906F, 0xB07F65, 0x282F36 }
};

static const struct rasterfall_character_profile ordinary_character = {
    RASTERFALL_CHARACTER_NONE, RASTERFALL_PROFESSION_NONE,
    "Ordinary", NULL, ALL_ACTIONS,
    0xD94F70, 0x542F55, 0xF0C3A5, 0x512B3A
};

const struct rasterfall_character_profile *rasterfall_character_profile(int id)
{
    if (id < 0 || id >= RASTERFALL_CHARACTER_COUNT)
        return &ordinary_character;
    return &characters[id];
}

int rasterfall_character_for_actor(int actor_id, int class_id)
{
    (void)actor_id;
    (void)class_id;
    return RASTERFALL_CHARACTER_NONE;
}

int rasterfall_character_logic_test(void)
{
    int i, j;
    for (i = 0; i < RASTERFALL_CHARACTER_COUNT; i++) {
        const struct rasterfall_character_profile *profile =
            rasterfall_character_profile(i);
        if (profile->id != i ||
            (i < RASTERFALL_CHARACTER_RF_RIFLEMAN &&
             profile->profession_id != RASTERFALL_PROFESSION_GUNSMITH + i) ||
            (i == RASTERFALL_CHARACTER_RF_RIFLEMAN &&
             profile->profession_id != RASTERFALL_PROFESSION_NONE) ||
            !profile->name ||
            (profile->actions & ALL_ACTIONS) != ALL_ACTIONS)
            return 1;
    }
    if (rasterfall_character_profile(RASTERFALL_CHARACTER_NONE)->id !=
               RASTERFALL_CHARACTER_NONE ||
           rasterfall_character_profile(RASTERFALL_CHARACTER_NONE)->profession_id !=
               RASTERFALL_PROFESSION_NONE ||
           rasterfall_character_for_actor(3, 2) != RASTERFALL_CHARACTER_NONE)
        return 1;
    for (i = 0; i < RASTERFALL_MODULAR_PROFESSION_COUNT; i++) {
        const struct rasterfall_character_visual_recipe *recipe =
            rasterfall_character_visual_recipe(i);
        if (!recipe || recipe->body_resource_id != RASTERFALL_BODY_RF_HUMANOID_V2 ||
            !recipe->shirt_color || !recipe->pants_color ||
            recipe->attachment_count < 3 ||
            recipe->attachment_count > RASTERFALL_CHARACTER_RECIPE_ATTACHMENTS)
            return 1;
        for (j = 0; j < (int)recipe->attachment_count; j++) {
            if (!rasterfall_character_gear_resource_name(
                    recipe->attachments[j].gear_resource_id) ||
                recipe->attachments[j].host_socket < RASTERFALL_ATTACHMENT_BACK ||
                recipe->attachments[j].host_socket > RASTERFALL_ATTACHMENT_HIP_R)
                return 1;
        }
    }
    return rasterfall_character_visual_recipe(-1) != NULL ||
        rasterfall_character_visual_recipe(RASTERFALL_MODULAR_PROFESSION_COUNT) != NULL ||
        strcmp(rasterfall_character_body_resource_name(
            RASTERFALL_BODY_RF_HUMANOID_V2), "rf_humanoid_v2");
}

static const struct rasterfall_profession_visual_profile professions[] = {
    {0, 0, RF_HEAD_NONE, RF_BADGE_NONE, RF_BAG_NONE, 0, 0},
    {0xD28A30, 0x414957, RF_HEAD_GOGGLES, RF_BADGE_TOOL, RF_BAG_TOOLS, 0, 0},
    {0xB5A06C, 0x607384, RF_HEAD_CAP, RF_BADGE_CRATE, RF_BAG_NONE, 1, 0},
    {0x30965C, 0xD3DDD5, RF_HEAD_MEDICAL_BAND, RF_BADGE_CROSS, RF_BAG_MEDICAL, 0, 0},
    {0x80966A, 0x37483F, RF_HEAD_HELMET_BAND, RF_BADGE_SHIELD, RF_BAG_NONE, 0, 1},
    {0, 0, RF_HEAD_NONE, RF_BADGE_NONE, RF_BAG_NONE, 0, 0}
};

const struct rasterfall_profession_visual_profile *
rasterfall_profession_visual_profile(int profession_id)
{
    if (profession_id <= RASTERFALL_PROFESSION_NONE ||
        profession_id >= RASTERFALL_PROFESSION_COUNT) return NULL;
    return &professions[profession_id];
}

#define A(socket, gear) { RASTERFALL_ATTACHMENT_##socket, RASTERFALL_GEAR_##gear }
static const struct rasterfall_character_visual_recipe modular_professions[] = {
    { RASTERFALL_BODY_RF_HUMANOID_V2, 0x6F8461, 0x84906F,
      {A(HEAD,RIFLEMAN_HEAD),A(CHEST,RIFLEMAN_CHEST),A(BACK,RIFLEMAN_BACK)},3},
    { RASTERFALL_BODY_RF_HUMANOID_V2, 0x4D5969, 0x616C79,
      {A(HEAD,BREACHER_HEAD),A(CHEST,BREACHER_CHEST),A(BACK,BREACHER_BACK)},3},
    { RASTERFALL_BODY_RF_HUMANOID_V2, 0x899376, 0x798465,
      {A(HEAD,RECON_HEAD),A(CHEST,RECON_CHEST),A(BACK,RECON_BACK)},3},
    { RASTERFALL_BODY_RF_HUMANOID_V2, 0x7C9093, 0x657E81,
      {A(HEAD,MEDIC_HEAD),A(CHEST,MEDIC_CHEST),A(BACK,MEDIC_BACK)},3},
    { RASTERFALL_BODY_RF_HUMANOID_V2, 0x997C4B, 0x7C7665,
      {A(HEAD,ENGINEER_HEAD),A(CHEST,ENGINEER_CHEST),A(BACK,ENGINEER_BACK),
       A(HIP_L,ENGINEER_HIP_L)},4},
    { RASTERFALL_BODY_RF_HUMANOID_V2, 0x796C59, 0x6C695D,
      {A(HEAD,HEAVY_HEAD),A(CHEST,HEAVY_CHEST),A(BACK,HEAVY_BACK),
       A(HIP_L,HEAVY_HIP_L),A(HIP_R,HEAVY_HIP_R)},5}
};
#undef A

static const char *gear_resource_names[] = {
    "rf_gear_rifleman_head", "rf_gear_rifleman_chest", "rf_gear_rifleman_back",
    "rf_gear_breacher_head", "rf_gear_breacher_chest", "rf_gear_breacher_back",
    "rf_gear_recon_head", "rf_gear_recon_chest", "rf_gear_recon_back",
    "rf_gear_medic_head", "rf_gear_medic_chest", "rf_gear_medic_back",
    "rf_gear_engineer_head", "rf_gear_engineer_chest", "rf_gear_engineer_back",
    "rf_gear_engineer_hip_l", "rf_gear_heavy_head", "rf_gear_heavy_chest",
    "rf_gear_heavy_back", "rf_gear_heavy_hip_l", "rf_gear_heavy_hip_r"
};

const struct rasterfall_character_visual_recipe *
rasterfall_character_visual_recipe(int id)
{
    if (id < 0 || id >= RASTERFALL_MODULAR_PROFESSION_COUNT) return NULL;
    return &modular_professions[id];
}

const char *rasterfall_character_body_resource_name(int id)
{
    return id == RASTERFALL_BODY_RF_HUMANOID_V2 ? "rf_humanoid_v2" : NULL;
}

const char *rasterfall_character_gear_resource_name(int id)
{
    if (id < 0 || id >= RASTERFALL_GEAR_RESOURCE_COUNT) return NULL;
    return gear_resource_names[id];
}
