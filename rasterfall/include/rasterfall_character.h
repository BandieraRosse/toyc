#ifndef RASTERFALL_CHARACTER_H
#define RASTERFALL_CHARACTER_H

#include "tlibc_types.h"
#include "rasterfall_humanoid.h"

/* Stable gameplay-facing visual identities.  Keep these IDs independent of
 * asset paths: a profile may start as a procedural actor and later acquire a
 * skeletal model without changing saves or network state. */
enum rasterfall_character_id {
    RASTERFALL_CHARACTER_NONE = -1,
    RASTERFALL_CHARACTER_HURD_GUNSMITH,
    RASTERFALL_CHARACTER_HURD_LOGISTICS,
    RASTERFALL_CHARACTER_HURD_MEDIC,
    RASTERFALL_CHARACTER_HURD_GUARD,
    RASTERFALL_CHARACTER_MAID,
    RASTERFALL_CHARACTER_RF_RIFLEMAN,
    /* Fixed roster identities. These IDs are content identity, not a
     * profession enum; changing a visual recipe must not rename a character. */
    RASTERFALL_CHARACTER_SQUAD_A_MEDIC,
    RASTERFALL_CHARACTER_SQUAD_A_ENGINEER,
    RASTERFALL_CHARACTER_SQUAD_A_RECON,
    RASTERFALL_CHARACTER_SQUAD_B_RIFLEMAN,
    RASTERFALL_CHARACTER_SQUAD_B_BREACHER,
    RASTERFALL_CHARACTER_SQUAD_B_HEAVY,
    RASTERFALL_CHARACTER_SQUAD_B_MEDIC,
    RASTERFALL_CHARACTER_COUNT
};

/* Independent identity: professions do not select a body or grant abilities. */
enum rasterfall_profession_id {
    RASTERFALL_PROFESSION_NONE,
    RASTERFALL_PROFESSION_GUNSMITH,
    RASTERFALL_PROFESSION_LOGISTICS,
    RASTERFALL_PROFESSION_MEDIC,
    RASTERFALL_PROFESSION_GUARD,
    RASTERFALL_PROFESSION_MAID,
    RASTERFALL_PROFESSION_COUNT
};

enum rasterfall_profession_head { RF_HEAD_NONE, RF_HEAD_GOGGLES, RF_HEAD_CAP,
                                 RF_HEAD_MEDICAL_BAND, RF_HEAD_HELMET_BAND };
enum rasterfall_profession_badge { RF_BADGE_NONE, RF_BADGE_TOOL, RF_BADGE_CRATE,
                                  RF_BADGE_CROSS, RF_BADGE_SHIELD };
enum rasterfall_profession_bag { RF_BAG_NONE, RF_BAG_TOOLS, RF_BAG_MEDICAL };

/* Presentation-only profiles. Zero/NONE resolves to NULL and leaves base
 * appearance and draw order intact; skeletal Maid keeps its authored gear. */
struct rasterfall_profession_visual_profile {
    uint32_t accent_color, gear_color;
    int head, badge, waist_bag;
    int backpack; /* Large supply pack. */
    int vest;     /* Broad, thick torso shell. */
};
const struct rasterfall_profession_visual_profile *
rasterfall_profession_visual_profile(int profession_id);

/* Profession Modularization V1 is a presentation identity catalog, separate
 * from the older gameplay-facing profession IDs above.  Recipes contain only
 * stable resource IDs; loaded resources and model instances belong to the
 * character presentation runtime. */
enum rasterfall_modular_profession_id {
    RASTERFALL_MODULAR_PROFESSION_NONE = -1,
    RASTERFALL_MODULAR_RIFLEMAN,
    RASTERFALL_MODULAR_BREACHER,
    RASTERFALL_MODULAR_RECON,
    RASTERFALL_MODULAR_MEDIC,
    RASTERFALL_MODULAR_ENGINEER,
    RASTERFALL_MODULAR_HEAVY,
    RASTERFALL_MODULAR_PROFESSION_COUNT
};

enum rasterfall_character_body_resource_id {
    RASTERFALL_BODY_RF_HUMANOID_V2,
    RASTERFALL_BODY_RESOURCE_COUNT
};

enum rasterfall_character_gear_resource_id {
    RASTERFALL_GEAR_RIFLEMAN_HEAD, RASTERFALL_GEAR_RIFLEMAN_CHEST,
    RASTERFALL_GEAR_RIFLEMAN_BACK,
    RASTERFALL_GEAR_BREACHER_HEAD, RASTERFALL_GEAR_BREACHER_CHEST,
    RASTERFALL_GEAR_BREACHER_BACK,
    RASTERFALL_GEAR_RECON_HEAD, RASTERFALL_GEAR_RECON_CHEST,
    RASTERFALL_GEAR_RECON_BACK,
    RASTERFALL_GEAR_MEDIC_HEAD, RASTERFALL_GEAR_MEDIC_CHEST,
    RASTERFALL_GEAR_MEDIC_BACK,
    RASTERFALL_GEAR_ENGINEER_HEAD, RASTERFALL_GEAR_ENGINEER_CHEST,
    RASTERFALL_GEAR_ENGINEER_BACK, RASTERFALL_GEAR_ENGINEER_HIP_L,
    RASTERFALL_GEAR_HEAVY_HEAD, RASTERFALL_GEAR_HEAVY_CHEST,
    RASTERFALL_GEAR_HEAVY_BACK, RASTERFALL_GEAR_HEAVY_HIP_L,
    RASTERFALL_GEAR_HEAVY_HIP_R,
    RASTERFALL_GEAR_RESOURCE_COUNT
};

struct rasterfall_character_attachment_recipe {
    enum rasterfall_character_attachment host_socket;
    int gear_resource_id;
};

#define RASTERFALL_CHARACTER_RECIPE_ATTACHMENTS 5
struct rasterfall_character_visual_recipe {
    int body_resource_id;
    uint32_t shirt_color;
    uint32_t pants_color;
    /* Passive rigid equipment only. Active weapons are resolved from the
     * actor's weapon slot and the finalized WEAPON_R socket. */
    struct rasterfall_character_attachment_recipe
        attachments[RASTERFALL_CHARACTER_RECIPE_ATTACHMENTS];
    unsigned int attachment_count;
};

const struct rasterfall_character_visual_recipe *
rasterfall_character_visual_recipe(int modular_profession_id);
const char *rasterfall_character_body_resource_name(int body_resource_id);
const char *rasterfall_character_gear_resource_name(int gear_resource_id);

enum rasterfall_character_action {
    RASTERFALL_CHARACTER_ACTION_LOCOMOTION = 1 << 0,
    RASTERFALL_CHARACTER_ACTION_WEAPON = 1 << 1,
    RASTERFALL_CHARACTER_ACTION_MELEE = 1 << 2,
    RASTERFALL_CHARACTER_ACTION_THROW = 1 << 3,
    RASTERFALL_CHARACTER_ACTION_INCAPACITATED = 1 << 4
};

struct rasterfall_character_profile {
    int id;
    int profession_id; /* Stable identity only; visual details stay in profession profile. */
    int visual_recipe_id; /* Presentation-only modular recipe selector. */
    const char *name;
    const char *model_path; /* NULL selects the procedural actor renderer. */
    unsigned int actions;
    uint32_t body_color;
    uint32_t leg_color;
    uint32_t skin_color;
    uint32_t hair_color;
};

const struct rasterfall_character_profile *rasterfall_character_profile(int id);
const struct rasterfall_character_visual_recipe *
rasterfall_character_visual_recipe_for_character(int character_id);
int rasterfall_character_for_actor(int actor_id, int class_id);
int rasterfall_character_logic_test(void);

#endif
