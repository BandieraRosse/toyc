#include "rasterfall_calibration.h"
#include "string.h"
#include "tlibc_everything.h"
#include "rasterfall_animation_composition.h"

static struct rasterfall_weapon_asset_profile asset_profiles[TOY_GAME_WEAPON_COUNT];
static struct rasterfall_pose_calibration pose_profiles[TOY_GAME_CHARACTER_COUNT][TOY_GAME_WEAPON_COUNT];
static int profiles_ready;

static const int default_body_pose[RASTERFALL_POSE_BODY_CHANNEL_COUNT][3] = {
    {-5, 0, 0}, {-63, 5, 51}, {85, 34, -91}, {21, 66, 2}, {92, 30, -4}
};
static const char *pose_body_channel_names[RASTERFALL_POSE_BODY_CHANNEL_COUNT] = {
    "upper_body", "right_arm", "right_elbow", "left_arm", "left_elbow"
};

static void profile_init(void)
{
    int i;
    static const char *paths[TOY_GAME_WEAPON_COUNT] = {
        "rasterfall/assets/models/pg_glock1.rmesh", "rasterfall/assets/models/smg_mac10.rmesh",
        "rasterfall/assets/models/sg_pump_action.rmesh", "rasterfall/assets/models/ar_ak47.rmesh",
        "rasterfall/assets/models/rf_AWP.rmesh", "rasterfall/assets/models/axe.rmesh",
        "rasterfall/assets/models/bomb.rmesh", "rasterfall/assets/models/molotov.rmesh"
    };
    if (profiles_ready) return;
    memset(asset_profiles, 0, sizeof(asset_profiles));
    memset(pose_profiles, 0, sizeof(pose_profiles));
    for (i = 0; i < TOY_GAME_WEAPON_COUNT; i++) {
        asset_profiles[i].model_path = paths[i];
        asset_profiles[i].base_scale_milli = 760000;
    }
    asset_profiles[TOY_GAME_WEAPON_AK].asset_basis = 2;
    asset_profiles[TOY_GAME_WEAPON_AK].skeletal = 1;
    asset_profiles[TOY_GAME_WEAPON_AK].base_scale_milli = 760000;
    asset_profiles[TOY_GAME_WEAPON_AK].attachment_grip =
        (struct rasterfall_cal_vec3){-18, -8, 24};
    asset_profiles[TOY_GAME_WEAPON_AWP].skeletal = 1;
    asset_profiles[TOY_GAME_WEAPON_AWP].base_scale_milli = 920000;
    pose_profiles[0][TOY_GAME_WEAPON_AK].character_id = 0;
    pose_profiles[0][TOY_GAME_WEAPON_AK].weapon = TOY_GAME_WEAPON_AK;
    /* Imported from tmp/eula_ak.rfpose. */
    pose_profiles[0][TOY_GAME_WEAPON_AK].scale_milli = 500;
    pose_profiles[0][TOY_GAME_WEAPON_AK].offset =
        (struct rasterfall_cal_vec3){255, 25, 15};
    pose_profiles[0][TOY_GAME_WEAPON_AK].yaw_offset = -35;
    pose_profiles[0][TOY_GAME_WEAPON_AK].pitch_offset = -80;
    pose_profiles[0][TOY_GAME_WEAPON_AK].roll_offset = 0;
    pose_profiles[0][TOY_GAME_WEAPON_AK].grip = (struct rasterfall_cal_vec3){-18, -8, 24};
    pose_profiles[0][TOY_GAME_WEAPON_AK].foregrip = (struct rasterfall_cal_vec3){-5, 1, 105};
    pose_profiles[0][TOY_GAME_WEAPON_AK].muzzle = (struct rasterfall_cal_vec3){-5, 15, 220};
    memcpy(pose_profiles[0][TOY_GAME_WEAPON_AK].body_pose, default_body_pose,
           sizeof(default_body_pose));
    pose_profiles[0][TOY_GAME_WEAPON_AK].left_ik = 1;
    for (i = 0; i < TOY_GAME_CHARACTER_COUNT; i++)
        for (int w = 0; w < TOY_GAME_WEAPON_COUNT; w++) {
            if (pose_profiles[i][w].scale_milli == 0) {
                pose_profiles[i][w].character_id = i;
                pose_profiles[i][w].weapon = w;
                pose_profiles[i][w].scale_milli = 1000;
                memcpy(pose_profiles[i][w].body_pose, default_body_pose,
                       sizeof(default_body_pose));
            }
        }
    profiles_ready = 1;
}

const struct rasterfall_weapon_asset_profile *rasterfall_weapon_asset_profile(int weapon)
{
    profile_init();
    if (weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT) weapon = TOY_GAME_WEAPON_PISTOL;
    return &asset_profiles[weapon];
}

const struct rasterfall_pose_calibration *rasterfall_pose_calibration_resolve(
    const struct rasterfall_calibration_state *editor,
    int character_id, int weapon)
{
    profile_init();
    if (character_id < 0 || character_id >= TOY_GAME_CHARACTER_COUNT)
        character_id = 0;
    if (weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT)
        weapon = TOY_GAME_WEAPON_PISTOL;
    if (editor && editor->active && editor->character == character_id &&
        editor->weapon == weapon)
        return &editor->pose;
    return &pose_profiles[character_id][weapon];
}

const char *rasterfall_pose_character_name(int character_id)
{
    static const char *names[TOY_GAME_CHARACTER_COUNT] = {"eula", "ar15", "ump45", "character3"};
    if (character_id < 0 || character_id >= TOY_GAME_CHARACTER_COUNT) return "character";
    return names[character_id];
}

const char *rasterfall_pose_weapon_name(int weapon)
{
    if (weapon == TOY_GAME_WEAPON_AK) return "ak";
    if (weapon == TOY_GAME_WEAPON_AWP) return "awp";
    if (weapon == TOY_GAME_WEAPON_SMG) return "smg";
    if (weapon == TOY_GAME_WEAPON_SHOTGUN) return "shotgun";
    if (weapon == TOY_GAME_WEAPON_PISTOL) return "pistol";
    if (weapon == TOY_GAME_WEAPON_AXE) return "axe";
    if (weapon == TOY_GAME_WEAPON_BOMB) return "bomb";
    if (weapon == TOY_GAME_WEAPON_MOLOTOV) return "molotov";
    return "weapon";
}

void rasterfall_pose_export_path(const struct rasterfall_calibration_state *s,
                                 char *path, int path_size)
{
    if (!path || path_size <= 0) return;
    snprintf(path, path_size, "tmp/%s_%s.rfpose",
             rasterfall_pose_character_name(s ? s->character : 0),
             rasterfall_pose_weapon_name(s ? s->weapon : TOY_GAME_WEAPON_AK));
}

void rasterfall_calibration_reset(struct rasterfall_calibration_state *state)
{
    const struct rasterfall_pose_calibration *p;
    memset(state, 0, sizeof(*state)); state->character = 0;
    state->weapon = TOY_GAME_WEAPON_AK;
    p = rasterfall_pose_calibration_resolve(NULL, state->character, state->weapon);
    memcpy(&state->pose, p, sizeof(*p));
    state->left_ik = state->pose.left_ik;
    state->locomotion = 0; state->fire_overlay = 0;
    state->animation_base = 1;
    state->animation_overlay = 0;
    state->animation_time_ms = 0;
    state->animation_playing = 1;
    state->page = RASTERFALL_POSE_PAGE_BODY;
    state->selection = 0; state->selected_bone = 0;
    state->selected_axis = 0; state->dirty = 0;
}

void rasterfall_calibration_init(struct rasterfall_calibration_state *state)
{ profile_init(); rasterfall_calibration_reset(state); }

void rasterfall_weapon_asset_to_canonical(int weapon, int x, int y, int z,
                                          int *ox, int *oy, int *oz)
{
    int basis = rasterfall_weapon_asset_profile(weapon)->asset_basis;
    if (basis == 1) { *ox = z; *oy = y; *oz = x; }
    else if (basis == 2) { *ox = -x; *oy = y; *oz = -z; }
    else { *ox = x; *oy = y; *oz = z; }
}

static void dump_vec(const char *name, struct rasterfall_cal_vec3 v)
{ __printf("  %s = %d %d %d\n", name, v.x, v.y, v.z); }

void rasterfall_calibration_dump(const struct rasterfall_calibration_state *s)
{
    int i;
    __printf("calibration character=%s weapon=%s active=%d\n",
             rasterfall_pose_character_name(s->character),
             rasterfall_pose_weapon_name(s->weapon), s->active);
    __printf("weapon scale=%d offset=%d %d %d yaw=%d pitch=%d roll=%d\n", s->pose.scale_milli,
             s->pose.offset.x, s->pose.offset.y, s->pose.offset.z,
             s->pose.yaw_offset, s->pose.pitch_offset, s->pose.roll_offset);
    dump_vec("grip", s->pose.grip); dump_vec("foregrip", s->pose.foregrip);
    dump_vec("muzzle", s->pose.muzzle);
    __printf("left-hand IK enabled=%d axes=%d anchors=%d locomotion=%d fire_overlay=%d\n",
             s->pose.left_ik, s->axes, s->anchors, s->locomotion, s->fire_overlay);
    for (i = 0; i < RASTERFALL_RIFLE_POSE_BONE_COUNT; i++)
        __printf("body[%s] = %d %d %d\n", rasterfall_rifle_pose_bone_display_names[i],
                 s->pose.body_pose[i][0], s->pose.body_pose[i][1],
                 s->pose.body_pose[i][2]);
}

int rasterfall_calibration_logic_test(void)
{
    struct rasterfall_calibration_state s;
    int x, y, z;
    rasterfall_calibration_init(&s);
    if (s.weapon != TOY_GAME_WEAPON_AK || s.pose.scale_milli != 500) return 1;
    rasterfall_weapon_asset_to_canonical(TOY_GAME_WEAPON_AK, 1, 2, 3, &x, &y, &z);
    if (x != -1 || y != 2 || z != -3) return 2;
    return 0;
}

static int editor_field_count(int page)
{ return page == RASTERFALL_POSE_PAGE_BODY ? RASTERFALL_POSE_BODY_CHANNEL_COUNT : page == RASTERFALL_POSE_PAGE_WEAPON ? 7 : page == RASTERFALL_POSE_PAGE_ANCHORS ? 9 : 3; }

static int *editor_value(struct rasterfall_calibration_state *s, int *axis)
{
    int n = s->selection;
    if (s->page == RASTERFALL_POSE_PAGE_BODY) {
        *axis = s->selected_axis; return &s->pose.body_pose[n][*axis];
    }
    if (s->page == RASTERFALL_POSE_PAGE_WEAPON) {
        if (n == 0) return &s->pose.scale_milli;
        if (n <= 3) { *axis = n - 1; return &((int *)&s->pose.offset)[*axis]; }
        if (n == 4) return &s->pose.pitch_offset;
        if (n == 5) return &s->pose.yaw_offset;
        return &s->pose.roll_offset;
    }
    if (s->page == RASTERFALL_POSE_PAGE_ANIMATION) {
        if (n == 0) return &s->animation_base;
        if (n == 1) return &s->animation_overlay;
        return &s->animation_time_ms;
    }
    if (n < 3) { *axis = n; return &((int *)&s->pose.grip)[*axis]; }
    if (n < 6) { *axis = n - 3; return &((int *)&s->pose.foregrip)[*axis]; }
    *axis = n - 6; return &((int *)&s->pose.muzzle)[*axis];
}

int rasterfall_calibration_editor_step(struct rasterfall_calibration_state *s, int action)
{
    int *value, axis, count;
    if (!s) return 0;
    axis = s->selected_axis;
    if (action == RASTERFALL_POSE_EDITOR_EXIT) { s->active = 0; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_TOGGLE_AXES) { s->axes=!s->axes; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_TOGGLE_ANCHORS) { s->anchors=!s->anchors; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_TOGGLE_IK) {
        s->left_ik=!s->left_ik; s->pose.left_ik=s->left_ik; s->dirty=1; return 1;
    }
    if (action == RASTERFALL_POSE_EDITOR_TOGGLE_ANIMATION_PLAY) {
        s->animation_playing=!s->animation_playing; return 1;
    }
    if (action >= RASTERFALL_POSE_EDITOR_AXIS_X && action <= RASTERFALL_POSE_EDITOR_AXIS_Z) { s->selected_axis=action-RASTERFALL_POSE_EDITOR_AXIS_X; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_NEXT_PAGE) { s->page=(s->page+1)%RASTERFALL_POSE_PAGE_COUNT; s->selection=0; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_PREV_PAGE) { s->page=(s->page+RASTERFALL_POSE_PAGE_COUNT-1)%RASTERFALL_POSE_PAGE_COUNT; s->selection=0; return 1; }
    count = editor_field_count(s->page);
    if (action == RASTERFALL_POSE_EDITOR_NEXT_FIELD) { s->selection=(s->selection+1)%count; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_PREV_FIELD) { s->selection=(s->selection+count-1)%count; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_RESET) {
        if (s->page == RASTERFALL_POSE_PAGE_BODY) s->pose.body_pose[s->selection][0]=s->pose.body_pose[s->selection][1]=s->pose.body_pose[s->selection][2]=0;
        else if (s->page == RASTERFALL_POSE_PAGE_WEAPON) { const struct rasterfall_pose_calibration *p=rasterfall_pose_calibration_resolve(NULL,s->character,s->weapon); if(s->selection==0)s->pose.scale_milli=p->scale_milli; else if(s->selection<=3)((int *)&s->pose.offset)[s->selection-1]=0; else if(s->selection==4)s->pose.pitch_offset=0; else if(s->selection==5)s->pose.yaw_offset=0; else s->pose.roll_offset=0; }
        else if (s->page == RASTERFALL_POSE_PAGE_ANCHORS) { if(s->selection<3)((int *)&s->pose.grip)[s->selection]=0; else if(s->selection<6)((int *)&s->pose.foregrip)[s->selection-3]=0; else ((int *)&s->pose.muzzle)[s->selection-6]=0; }
        else { if(s->selection==0)s->animation_base=1; else if(s->selection==1)s->animation_overlay=0; else s->animation_time_ms=0; }
        s->dirty=1; return 1;
    }
    if (action == RASTERFALL_POSE_EDITOR_EXPORT) return rasterfall_calibration_export(s);
    value = editor_value(s, &axis);
    if (action == RASTERFALL_POSE_EDITOR_DECREASE) *value -= 1;
    else if (action == RASTERFALL_POSE_EDITOR_INCREASE) *value += 1;
    else if (action == RASTERFALL_POSE_EDITOR_DECREASE_LARGE) *value -= 5;
    else if (action == RASTERFALL_POSE_EDITOR_INCREASE_LARGE) *value += 5;
    else return 0;
    if (s->page == RASTERFALL_POSE_PAGE_ANIMATION) {
        if (s->selection == 0) { if (*value < 0) *value=0; if (*value > 1) *value=1; }
        else if (s->selection == 1) { if (*value < 0) *value=0; if (*value > 2) *value=2; }
        else if (*value < 0) *value=0;
    }
    if (s->page == RASTERFALL_POSE_PAGE_BODY) s->selected_bone=s->selection;
    s->dirty=1; return 1;
}

int rasterfall_calibration_export(const struct rasterfall_calibration_state *s)
{
    char out[2400]; int n=0, i, fd;
    n += snprintf(out+n,sizeof(out)-n,"rfpose 1\ncharacter %s\nweapon %s\n\nweapon_scale %d\nweapon_offset %d %d %d\nweapon_rotation %d %d %d\n",rasterfall_pose_character_name(s->character),rasterfall_pose_weapon_name(s->weapon),s->pose.scale_milli,s->pose.offset.x,s->pose.offset.y,s->pose.offset.z,s->pose.yaw_offset,s->pose.pitch_offset,s->pose.roll_offset);
    n += snprintf(out+n,sizeof(out)-n,"grip %d %d %d\nforegrip %d %d %d\nmuzzle %d %d %d\n",s->pose.grip.x,s->pose.grip.y,s->pose.grip.z,s->pose.foregrip.x,s->pose.foregrip.y,s->pose.foregrip.z,s->pose.muzzle.x,s->pose.muzzle.y,s->pose.muzzle.z);
    for(i=0;i<RASTERFALL_POSE_BODY_CHANNEL_COUNT;i++) n+=snprintf(out+n,sizeof(out)-n,"body %s %d %d %d\n",pose_body_channel_names[i],s->pose.body_pose[i][0],s->pose.body_pose[i][1],s->pose.body_pose[i][2]);
    n+=snprintf(out+n,sizeof(out)-n,"left_ik %d\n",s->pose.left_ik);
    /* Export is also used from fresh checkouts where build/test cleanup may
     * have removed tmp/.  Existing directories are accepted by the helper. */
    if (tlibc_recursive_mkdir("tmp") < 0) return 0;
    {
        char path[128];
        rasterfall_pose_export_path(s, path, sizeof(path));
        fd=__openat(AT_FDCWD,path,O_WRONLY|O_CREAT|O_TRUNC,0644);
    }
    if (fd < 0) return 0;
    __write(fd,out,n);
    __close(fd);
    return 1;
}
