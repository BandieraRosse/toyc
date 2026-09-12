/* Included by renderer: six immutable resources, one serial scratch pose each.
 * No asset paths, pose data or family identity enter the gameplay enemy. */
struct enemy_visual_recipe {
    int family, type;
    const char *path;
};
#define INFECTED_RECIPE(family, tag, type, name) \
    { RASTERFALL_ENEMY_VISUAL_##family, TOY_GAME_ENEMY_PURSUIT_##type, \
      "rasterfall/assets/models/enemies/rf_infected_" tag "_" name ".rmesh" }
static const struct enemy_visual_recipe enemy_visual_recipes[] = {
    INFECTED_RECIPE(BLOCK_INFECTED, "block", COMMON, "common"),
    INFECTED_RECIPE(BLOCK_INFECTED, "block", FAST, "fast"),
    INFECTED_RECIPE(BLOCK_INFECTED, "block", HEAVY, "heavy"),
    INFECTED_RECIPE(HUMANOID_INFECTED, "humanoid", COMMON, "common"),
    INFECTED_RECIPE(HUMANOID_INFECTED, "humanoid", FAST, "fast"),
    INFECTED_RECIPE(HUMANOID_INFECTED, "humanoid", HEAVY, "heavy")
};
#undef INFECTED_RECIPE
static struct {
    int status;
    struct rasterfall_model_resource resource;
    struct rasterfall_model_instance instance;
} enemy_visual_cache[6];
/* AUTO is a renderer policy. The selected family is derived per enemy and is
 * deliberately not stored in toy_game or a network snapshot. The legacy
 * block/round renderer remains only as an explicit special-story presentation
 * choice; it is not a version-compatibility path for the new enemy models. */
static int enemy_visual_family = RASTERFALL_ENEMY_VISUAL_AUTO;
static struct { int valid, x, z, phase; unsigned long moved_us; } enemy_visual_motion[TOY_GAME_MAX_ENEMIES];
static int enemy_visual_capture_pose = -1; /* -1 runtime, 0 bind, 1 idle, 2 move */
static int enemy_visual_submissions, enemy_visual_failures;

void rasterfall_render_set_enemy_visual_family(int family)
{
    enemy_visual_family = family >= RASTERFALL_ENEMY_VISUAL_AUTO &&
        family <= RASTERFALL_ENEMY_VISUAL_HUMANOID_INFECTED ? family : 0;
    memset(enemy_visual_motion, 0, sizeof(enemy_visual_motion));
}

static const char *enemy_visual_family_name(int family)
{
    switch (family) {
    case RASTERFALL_ENEMY_VISUAL_BLOCK_INFECTED: return "BLOCK_INFECTED";
    case RASTERFALL_ENEMY_VISUAL_HUMANOID_INFECTED: return "HUMANOID_INFECTED";
    default: return "LEGACY";
    }
}

static const char *enemy_visual_type_name(int type)
{
    switch (type) {
    case TOY_GAME_ENEMY_PURSUIT_FAST: return "FAST";
    case TOY_GAME_ENEMY_PURSUIT_HEAVY: return "HEAVY";
    default: return "COMMON";
    }
}

static unsigned enemy_visual_mix_bucket(int type, int slot)
{
    uint32_t value = (uint32_t)(slot + 1) * 0x9E3779B9u;
    value ^= (uint32_t)(type + 1) * 0x85EBCA6Bu;
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    return value % 100u;
}

/* The slot is only a presentation seed. It keeps a visible enemy stable
 * across frames while remaining outside the gameplay state. */
static int enemy_visual_family_for_enemy(int type, int slot)
{
    unsigned bucket;
    int block;
    if (type < TOY_GAME_ENEMY_PURSUIT_COMMON ||
        type > TOY_GAME_ENEMY_PURSUIT_HEAVY)
        /* Special infected always use their procedural new-model rig. */
        return RASTERFALL_ENEMY_VISUAL_HUMANOID_INFECTED;
    if (enemy_visual_family != RASTERFALL_ENEMY_VISUAL_AUTO)
        return enemy_visual_family;
    bucket = enemy_visual_mix_bucket(type, slot);
    if (type == TOY_GAME_ENEMY_PURSUIT_FAST) {
        block = 30;
    } else if (type == TOY_GAME_ENEMY_PURSUIT_HEAVY) {
        block = 40;
    } else {
        block = 60;
    }
    if (bucket < (unsigned)block)
        return RASTERFALL_ENEMY_VISUAL_BLOCK_INFECTED;
    return RASTERFALL_ENEMY_VISUAL_HUMANOID_INFECTED;
}

static void enemy_visual_rotate(struct rasterfall_model_asset *pose,
    enum rasterfall_humanoid_bone role, int x, int y, int z)
{
    int bone = rasterfall_model_humanoid_bone(pose, role);
    if (bone < 0) return;
    pose->bones[bone].rotate_x = x;
    pose->bones[bone].rotate_y = y;
    pose->bones[bone].rotate_z = z;
}

static int render_infected_enemy(struct toy_renderer *renderer,
    const struct camera *camera, const struct toy_game_enemy *enemy,
    const struct toy_game_enemy *source, int slot, int death_scale,
    uint32_t feedback, int family)
{
    int index, moving = 0, phase, swing, scale, pixels;
    struct rasterfall_model_instance *instance;
    struct rasterfall_model_asset *pose;
    for (index = 0; index < 6; index++)
        if (enemy_visual_recipes[index].family == family &&
            enemy_visual_recipes[index].type == enemy->type) break;
    if (index == 6) return -1;
    if (!enemy_visual_cache[index].status) {
        enemy_visual_cache[index].status = -1;
        if (rasterfall_model_resource_load(&enemy_visual_cache[index].resource,
                enemy_visual_recipes[index].path) == 0) {
            if (rasterfall_model_instance_init(&enemy_visual_cache[index].instance,
                    &enemy_visual_cache[index].resource) == 0)
                enemy_visual_cache[index].status = 1;
            else rasterfall_model_resource_unload(&enemy_visual_cache[index].resource);
        }
        if (enemy_visual_cache[index].status < 0)
            __fprintf(2, "enemy visual: asset failed; legacy fallback: %s\n",
                      enemy_visual_recipes[index].path);
    }
    if (enemy_visual_cache[index].status < 0) { enemy_visual_failures++; return -1; }
    instance = &enemy_visual_cache[index].instance;
    if (rasterfall_model_instance_reset_pose(instance) < 0) return -1;
    pose = rasterfall_model_instance_pose(instance);
    if (!pose || !pose->has_character_contract || !pose->position_scale) {
        enemy_visual_failures++;
        return -1;
    }
    if (slot >= 0 && enemy_visual_motion[slot].valid) {
        int dx = source->x - enemy_visual_motion[slot].x;
        int dz = source->z - enemy_visual_motion[slot].z;
        int distance = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
        /* Ignore slot reuse/teleports; displacement drives presentation stride. */
        moving = distance > 0 && distance < 300 && source->active == 1;
        if (moving) {
            enemy_visual_motion[slot].phase =
                (enemy_visual_motion[slot].phase + distance) % 640;
            enemy_visual_motion[slot].moved_us = render_monotonic_us();
        } else if (distance >= 300) {
            enemy_visual_motion[slot].phase = 0;
            enemy_visual_motion[slot].moved_us = 0;
        }
        moving = source->active == 1 && enemy_visual_motion[slot].moved_us &&
                 (unsigned long)render_monotonic_us() - enemy_visual_motion[slot].moved_us < 100000;
    }
    if (slot >= 0) {
        enemy_visual_motion[slot].valid = source->active == 1;
        enemy_visual_motion[slot].x = source->x;
        enemy_visual_motion[slot].z = source->z;
        phase = enemy_visual_motion[slot].phase;
    } else {
        phase = 0;
    }
    if (enemy_visual_capture_pose >= 0) {
        moving = enemy_visual_capture_pose == 2;
        phase = 100;
    }
    swing = moving ? (phase < 320 ? phase : 640-phase) * 48 / 320 - 24 : 0;
    if (enemy_visual_capture_pose != 0) {
        int fast = enemy->type == TOY_GAME_ENEMY_PURSUIT_FAST;
        int heavy = enemy->type == TOY_GAME_ENEMY_PURSUIT_HEAVY;
        enemy_visual_rotate(pose, RASTERFALL_HUMANOID_SPINE, fast ? 23 : 8, 0, 0);
        enemy_visual_rotate(pose, RASTERFALL_HUMANOID_HEAD, -8, 0, heavy ? -5 : 6);
        enemy_visual_rotate(pose, RASTERFALL_HUMANOID_LEFT_UPPER_ARM,
                            0, fast ? -36 : -12, fast ? -44 : -68);
        enemy_visual_rotate(pose, RASTERFALL_HUMANOID_RIGHT_UPPER_ARM,
                            0, fast ? 42 : 8, fast ? 42 : 62);
        enemy_visual_rotate(pose, RASTERFALL_HUMANOID_LEFT_FOREARM, 0, -15, -14);
        enemy_visual_rotate(pose, RASTERFALL_HUMANOID_RIGHT_FOREARM, 0, 23, 18);
        enemy_visual_rotate(pose, RASTERFALL_HUMANOID_LEFT_UPPER_LEG, swing, 0, 0);
        enemy_visual_rotate(pose, RASTERFALL_HUMANOID_RIGHT_UPPER_LEG, -swing, 0, 0);
        enemy_visual_rotate(pose, RASTERFALL_HUMANOID_LEFT_LOWER_LEG,
                            swing < 0 ? -swing : 0, 0, 0);
        enemy_visual_rotate(pose, RASTERFALL_HUMANOID_RIGHT_LOWER_LEG,
                            swing > 0 ? swing : 0, 0, 0);
    }
    if (rasterfall_model_instance_update_bones(instance) < 0) return -1;
    /* Authored meters -> RFU. Heavy size is authored, never doubled by the
     * legacy 1350 multiplier. Legacy deaths retain vertical collapse only. */
    scale = 512000 / pose->position_scale;
    active_infected_model = 1;
    active_infected_squash = source->active == 2 && !active_enemy_transform ? death_scale : 1000;
    active_infected_tint = feedback;
    active_gallery_facing = 1;
    active_gallery_sy = enemy->dir_x;
    active_gallery_cy = enemy->dir_z;
    if (!active_gallery_sy && !active_gallery_cy) active_gallery_cy = 1024;
    pixels = render_gallery_model(renderer, camera, pose, enemy->x,
        -900 + active_enemy_lift, enemy->z, scale);
    active_gallery_facing = 0;
    active_infected_model = 0;
    active_infected_squash = 1000;
    active_infected_tint = 0;
    enemy_visual_submissions++;
    return pixels;
}

static int render_infected_display_model(struct toy_renderer *renderer,
    const struct camera *camera, int family, int type, int x, int z)
{
    struct toy_game_enemy model;
    memset(&model, 0, sizeof(model));
    model.active = 1;
    model.type = type;
    model.x = x; model.z = z; model.dir_z = -1024;
    if (family == RASTERFALL_ENEMY_VISUAL_LEGACY) {
        uint32_t color = type == TOY_GAME_ENEMY_PURSUIT_HEAVY ?
            RF_COLOR_ENEMY_PURSUIT_HEAVY : type == TOY_GAME_ENEMY_PURSUIT_FAST ?
            RF_COLOR_ENEMY_PURSUIT_FAST : RF_COLOR_ENEMY_PURSUIT_COMMON;
        return type == TOY_GAME_ENEMY_PURSUIT_FAST ?
            render_round_enemy(renderer, camera, &model, 1000, color) :
            render_block_enemy(renderer, camera, &model,
                type == TOY_GAME_ENEMY_PURSUIT_HEAVY ? 1350 : 1000, color);
    }
    return render_infected_enemy(renderer, camera, &model, &model, -1,
                                 1000, 0, family);
}
