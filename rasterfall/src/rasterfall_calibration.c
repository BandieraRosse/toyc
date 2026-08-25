#include "rasterfall_calibration.h"
#include "string.h"
#include "tlibc_everything.h"

static struct rasterfall_weapon_visual_profile profiles[TOY_GAME_WEAPON_COUNT];
static int profiles_ready;
static struct rasterfall_weapon_visual_profile runtime_profile;
static int runtime_profile_enabled;

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
    memset(profiles, 0, sizeof(profiles));
    for (i = 0; i < TOY_GAME_WEAPON_COUNT; i++) {
        profiles[i].model_path = paths[i]; profiles[i].scale_milli = 1000;
    }
    /* The imported AK is reversed in asset space.  This is a source-to-
     * canonical correction, not actor yaw.  750 makes its bbox a usable rifle
     * size under the existing RFU model scale. */
    profiles[TOY_GAME_WEAPON_AK].scale_milli = 750;
    profiles[TOY_GAME_WEAPON_AK].asset_basis = 2;
    profiles[TOY_GAME_WEAPON_AK].grip = (struct rasterfall_cal_vec3){-18, -8, 24};
    profiles[TOY_GAME_WEAPON_AK].foregrip = (struct rasterfall_cal_vec3){0, -4, 190};
    profiles[TOY_GAME_WEAPON_AK].muzzle = (struct rasterfall_cal_vec3){0, 0, 420};
    profiles[TOY_GAME_WEAPON_AK].stock = (struct rasterfall_cal_vec3){0, 4, -190};
    profiles[TOY_GAME_WEAPON_AWP].scale_milli = 820;
    profiles_ready = 1;
}

const struct rasterfall_weapon_visual_profile *rasterfall_weapon_visual_profile(int weapon)
{
    profile_init();
    if (weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT) weapon = TOY_GAME_WEAPON_PISTOL;
    if (runtime_profile_enabled && weapon == TOY_GAME_WEAPON_AK)
        return &runtime_profile;
    return &profiles[weapon];
}

void rasterfall_calibration_apply_runtime(
    const struct rasterfall_weapon_visual_profile *profile)
{
    if (!profile) { runtime_profile_enabled = 0; return; }
    runtime_profile = *profile; runtime_profile_enabled = 1;
}

struct rasterfall_weapon_visual_profile *rasterfall_calibration_weapon(
    struct rasterfall_calibration_state *state, int weapon)
{
    profile_init();
    if (weapon < 0 || weapon >= TOY_GAME_WEAPON_COUNT) return NULL;
    if (state && state->weapon == weapon) return &state->weapon_profile;
    return &profiles[weapon];
}

void rasterfall_calibration_reset(struct rasterfall_calibration_state *state)
{
    const struct rasterfall_weapon_visual_profile *p;
    memset(state, 0, sizeof(*state)); state->character = 0;
    state->weapon = TOY_GAME_WEAPON_AK; state->left_ik = 0;
    p = rasterfall_weapon_visual_profile(state->weapon);
    memcpy(&state->weapon_profile, p, sizeof(*p));
    state->character_profile.right_hand_bone = "右手首";
    state->character_profile.left_hand_bone = "左手首";
    state->locomotion = 0; state->fire_overlay = 0;
    state->page = RASTERFALL_POSE_PAGE_BODY;
    state->selection = 0; state->selected_bone = 0;
    state->selected_axis = 0; state->dirty = 0;
    /* The eight editor rows are deliberately named in the authoring layer.
     * The animation composition adapter maps them to the model's existing
     * five rifle-pose channels until the lower-level rig grows those channels. */
    state->stance[0][0] = -5;
    state->stance[3][0] = -63; state->stance[3][1] = 5; state->stance[3][2] = 51;
    state->stance[4][0] = 85; state->stance[4][1] = 34; state->stance[4][2] = -91;
    state->stance[6][0] = 21; state->stance[6][1] = 66; state->stance[6][2] = 2;
    state->stance[7][0] = 92; state->stance[7][1] = 30; state->stance[7][2] = -4;
}

void rasterfall_calibration_init(struct rasterfall_calibration_state *state)
{ profile_init(); runtime_profile_enabled = 0; rasterfall_calibration_reset(state); }

void rasterfall_weapon_asset_to_canonical(int weapon, int x, int y, int z,
                                          int *ox, int *oy, int *oz)
{
    int basis = rasterfall_weapon_visual_profile(weapon)->asset_basis;
    if (basis == 1) { *ox = z; *oy = y; *oz = x; }
    else if (basis == 2) { *ox = -x; *oy = y; *oz = -z; }
    else { *ox = x; *oy = y; *oz = z; }
}

static void dump_vec(const char *name, struct rasterfall_cal_vec3 v)
{ __printf("  %s = %d %d %d\n", name, v.x, v.y, v.z); }

void rasterfall_calibration_dump(const struct rasterfall_calibration_state *s)
{
    int i;
    __printf("calibration character=eula weapon=ak active=%d\n", s->active);
    __printf("weapon scale=%d offset=%d %d %d yaw=%d pitch=%d roll=%d\n", s->weapon_profile.scale_milli,
             s->weapon_profile.offset.x, s->weapon_profile.offset.y, s->weapon_profile.offset.z,
             s->weapon_profile.yaw_offset, s->weapon_profile.pitch_offset, s->weapon_profile.roll_offset);
    dump_vec("grip", s->weapon_profile.grip); dump_vec("foregrip", s->weapon_profile.foregrip);
    dump_vec("muzzle", s->weapon_profile.muzzle); dump_vec("stock", s->weapon_profile.stock);
    __printf("right hand anchor bone=%s offset=%d %d %d rotation=%d %d %d\n",
             s->character_profile.right_hand_bone, s->character_profile.right_grip_anchor.x,
             s->character_profile.right_grip_anchor.y, s->character_profile.right_grip_anchor.z,
             s->character_profile.right_yaw, s->character_profile.right_pitch, s->character_profile.right_roll);
    __printf("left hand anchor bone=%s offset=%d %d %d\n", s->character_profile.left_hand_bone,
             s->character_profile.left_grip_anchor.x, s->character_profile.left_grip_anchor.y,
             s->character_profile.left_grip_anchor.z);
    __printf("left-hand IK enabled=%d axes=%d anchors=%d locomotion=%d fire_overlay=%d\n",
             s->left_ik, s->axes, s->anchors, s->locomotion, s->fire_overlay);
    for (i = 0; i < 8; i++) if (s->stance[i][0] || s->stance[i][1] || s->stance[i][2])
        __printf("stance[%d] = %d %d %d\n", i, s->stance[i][0], s->stance[i][1], s->stance[i][2]);
}

int rasterfall_calibration_logic_test(void)
{
    struct rasterfall_calibration_state s;
    int x, y, z;
    rasterfall_calibration_init(&s);
    if (s.weapon != TOY_GAME_WEAPON_AK || s.weapon_profile.scale_milli != 750) return 1;
    rasterfall_weapon_asset_to_canonical(TOY_GAME_WEAPON_AK, 1, 2, 3, &x, &y, &z);
    if (x != -1 || y != 2 || z != -3) return 2;
    return 0;
}

static int editor_field_count(int page)
{ return page == RASTERFALL_POSE_PAGE_BODY ? 8 : page == RASTERFALL_POSE_PAGE_WEAPON ? 7 : 9; }

static int *editor_value(struct rasterfall_calibration_state *s, int *axis)
{
    int n = s->selection;
    if (s->page == RASTERFALL_POSE_PAGE_BODY) {
        *axis = s->selected_axis; return &s->stance[n][*axis];
    }
    if (s->page == RASTERFALL_POSE_PAGE_WEAPON) {
        if (n == 0) return &s->weapon_profile.scale_milli;
        if (n <= 3) { *axis = n - 1; return &((int *)&s->weapon_profile.offset)[*axis]; }
        if (n == 4) return &s->weapon_profile.pitch_offset;
        if (n == 5) return &s->weapon_profile.yaw_offset;
        return &s->weapon_profile.roll_offset;
    }
    if (n < 3) { *axis = n; return &((int *)&s->weapon_profile.grip)[*axis]; }
    if (n < 6) { *axis = n - 3; return &((int *)&s->weapon_profile.foregrip)[*axis]; }
    *axis = n - 6; return &((int *)&s->weapon_profile.muzzle)[*axis];
}

int rasterfall_calibration_editor_step(struct rasterfall_calibration_state *s, int action)
{
    int *value, axis, count;
    if (!s) return 0;
    axis = s->selected_axis;
    if (action == RASTERFALL_POSE_EDITOR_EXIT) { s->active = 0; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_TOGGLE_AXES) { s->axes=!s->axes; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_TOGGLE_ANCHORS) { s->anchors=!s->anchors; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_TOGGLE_IK) { s->left_ik=!s->left_ik; return 1; }
    if (action >= RASTERFALL_POSE_EDITOR_AXIS_X && action <= RASTERFALL_POSE_EDITOR_AXIS_Z) { s->selected_axis=action-RASTERFALL_POSE_EDITOR_AXIS_X; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_NEXT_PAGE) { s->page=(s->page+1)%RASTERFALL_POSE_PAGE_COUNT; s->selection=0; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_PREV_PAGE) { s->page=(s->page+RASTERFALL_POSE_PAGE_COUNT-1)%RASTERFALL_POSE_PAGE_COUNT; s->selection=0; return 1; }
    count = editor_field_count(s->page);
    if (action == RASTERFALL_POSE_EDITOR_NEXT_FIELD) { s->selection=(s->selection+1)%count; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_PREV_FIELD) { s->selection=(s->selection+count-1)%count; return 1; }
    if (action == RASTERFALL_POSE_EDITOR_RESET) {
        if (s->page == RASTERFALL_POSE_PAGE_BODY) s->stance[s->selection][0]=s->stance[s->selection][1]=s->stance[s->selection][2]=0;
        else if (s->page == RASTERFALL_POSE_PAGE_WEAPON) { const struct rasterfall_weapon_visual_profile *p=rasterfall_weapon_visual_profile(s->weapon); if(s->selection==0)s->weapon_profile.scale_milli=p->scale_milli; else if(s->selection<=3)((int *)&s->weapon_profile.offset)[s->selection-1]=0; else if(s->selection==4)s->weapon_profile.pitch_offset=0; else if(s->selection==5)s->weapon_profile.yaw_offset=0; else s->weapon_profile.roll_offset=0; }
        else { if(s->selection<3)((int *)&s->weapon_profile.grip)[s->selection]=0; else if(s->selection<6)((int *)&s->weapon_profile.foregrip)[s->selection-3]=0; else ((int *)&s->weapon_profile.muzzle)[s->selection-6]=0; }
        s->dirty=1; return 1;
    }
    if (action == RASTERFALL_POSE_EDITOR_EXPORT) return rasterfall_calibration_export(s);
    value = editor_value(s, &axis);
    if (action == RASTERFALL_POSE_EDITOR_DECREASE) *value -= 1;
    else if (action == RASTERFALL_POSE_EDITOR_INCREASE) *value += 1;
    else if (action == RASTERFALL_POSE_EDITOR_DECREASE_LARGE) *value -= 5;
    else if (action == RASTERFALL_POSE_EDITOR_INCREASE_LARGE) *value += 5;
    else return 0;
    if (s->page == RASTERFALL_POSE_PAGE_BODY) s->selected_bone=s->selection;
    s->dirty=1; return 1;
}

int rasterfall_calibration_export(const struct rasterfall_calibration_state *s)
{
    static const char *bones[] = {"chest","upper_chest","right_shoulder","right_upper_arm","right_forearm","left_shoulder","left_upper_arm","left_forearm"};
    char out[2400]; int n=0, i, fd;
    n += snprintf(out+n,sizeof(out)-n,"character eula\nweapon ak\n\nweapon_scale %d\nweapon_offset %d %d %d\nweapon_rotation %d %d %d\n",s->weapon_profile.scale_milli,s->weapon_profile.offset.x,s->weapon_profile.offset.y,s->weapon_profile.offset.z,s->weapon_profile.pitch_offset,s->weapon_profile.yaw_offset,s->weapon_profile.roll_offset);
    n += snprintf(out+n,sizeof(out)-n,"weapon_grip %d %d %d\nweapon_foregrip %d %d %d\nweapon_muzzle %d %d %d\nweapon_stock %d %d %d\n",s->weapon_profile.grip.x,s->weapon_profile.grip.y,s->weapon_profile.grip.z,s->weapon_profile.foregrip.x,s->weapon_profile.foregrip.y,s->weapon_profile.foregrip.z,s->weapon_profile.muzzle.x,s->weapon_profile.muzzle.y,s->weapon_profile.muzzle.z,s->weapon_profile.stock.x,s->weapon_profile.stock.y,s->weapon_profile.stock.z);
    for(i=0;i<8;i++) n+=snprintf(out+n,sizeof(out)-n,"%s %d %d %d\n",bones[i],s->stance[i][0],s->stance[i][1],s->stance[i][2]);
    n+=snprintf(out+n,sizeof(out)-n,"left_ik %d\n",s->left_ik);
    /* Export is also used from fresh checkouts where build/test cleanup may
     * have removed tmp/.  Existing directories are accepted by the helper. */
    if (tlibc_recursive_mkdir("tmp") < 0) return 0;
    fd=__openat(AT_FDCWD,"tmp/eula_ak.rfpose",O_WRONLY|O_CREAT|O_TRUNC,0644);
    if (fd < 0) return 0;
    __write(fd,out,n);
    __close(fd);
    return 1;
}
