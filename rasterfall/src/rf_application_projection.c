#include "tlibc_everything.h"
#include "rf_application_projection.h"
#include "rf_core_host.h"
#include "rf_game_lifecycle.h"

static void copy_text(char *dst, const char *src, int size)
{
    if (!dst || size <= 0) return;
    if (!src) src = "";
    strncpy(dst, src, (unsigned int)size - 1);
    dst[size - 1] = 0;
}

static const char *person_role(int character_id, int class_id)
{
    const struct rasterfall_character_profile *profile;
    profile = rasterfall_character_profile(character_id);
    if (profile && profile->profession_id == RASTERFALL_PROFESSION_MEDIC)
        return "Medic";
    if (character_id == RASTERFALL_CHARACTER_SQUAD_A_ENGINEER)
        return "Engineer";
    if (character_id == RASTERFALL_CHARACTER_SQUAD_A_RECON)
        return "Recon";
    if (character_id == RASTERFALL_CHARACTER_SQUAD_B_BREACHER)
        return "Breacher";
    if (character_id == RASTERFALL_CHARACTER_SQUAD_B_HEAVY)
        return "Heavy";
    if (class_id == TOY_GAME_AI_LEVEL_1) return "Recruit";
    if (class_id == TOY_GAME_AI_LEVEL_3) return "Veteran";
    return "Rifleman";
}

static const char *person_department(int character_id)
{
    const struct rasterfall_character_profile *profile =
        rasterfall_character_profile(character_id);
    if (profile && profile->profession_id == RASTERFALL_PROFESSION_MEDIC)
        return "Medical";
    if (character_id == RASTERFALL_CHARACTER_SQUAD_A_ENGINEER)
        return "Engineering";
    return "Operations";
}

static const char *person_health(const struct toy_game_actor *actor)
{
    if (actor->state == TOY_GAME_ACTOR_DOWNED) return "DOWNED";
    if (actor->state == TOY_GAME_ACTOR_DEAD) return "DEAD";
    return actor->hp < actor->max_hp ? "INJURED" : "HEALTHY";
}

static const char *person_readiness(const struct toy_game_actor *actor)
{
    if (!actor->active || actor->state == TOY_GAME_ACTOR_DEAD)
        return "UNAVAILABLE";
    if (actor->state == TOY_GAME_ACTOR_DOWNED) return "RECOVERY";
    return "READY";
}

static void person_assignment(const struct toy_game_actor *actor,
                              char *dst, int size)
{
    if (actor->base_core) copy_text(dst, "BASE", size);
    else if (actor->companion) copy_text(dst, "COMPANION", size);
    else if (actor->flag_index >= 0)
        snprintf(dst, (unsigned int)size, "FLAG %d", actor->flag_index);
    else copy_text(dst, "RESERVE", size);
}

void rf_application_query_init(
    struct rf_application_query_context *context,
    const struct rf_core *core,
    const struct rf_game_runtime *game_runtime,
    const struct rf_command_context *command)
{
    if (!context) return;
    memset(context, 0, sizeof(*context));
    context->core = core;
    context->game_runtime = game_runtime;
    context->command = command;
}

int rf_application_query_core_status(
    const struct rf_application_query_context *context,
    struct rf_core_status *status)
{
    if (!context || !context->core) return -1;
    return rf_core_get_status(context->core, status);
}

int rf_application_query_game_status(
    const struct rf_application_query_context *context,
    struct rf_game_runtime_status *status)
{
    if (!context || !context->game_runtime) return -1;
    return rf_game_runtime_get_status(context->game_runtime, status);
}

int rf_application_project_personnel(
    const struct rf_application_query_context *context,
    struct rf_personnel_snapshot *snapshot)
{
    const struct rasterfall_session *session;
    int i;
    if (!snapshot) return -1;
    memset(snapshot, 0, sizeof(*snapshot));
    if (!context || !context->game_runtime ||
        !(session = context->game_runtime->session)) return 0;
    snapshot->available = 1;
    for (i = 0; i < TOY_GAME_MAX_ACTORS && snapshot->count < RF_PERSONNEL_MAX; i++) {
        const struct toy_game_actor *actor = &session->game_state.actors[i];
        struct rf_personnel_record *person;
        if (!actor->active || actor->developer_only) continue;
        person = &snapshot->people[snapshot->count++];
        person->person_id = actor->actor_id;
        copy_text(person->display_name, actor->name, RF_PERSONNEL_TEXT_MAX);
        copy_text(person->role, person_role(actor->character_id, actor->class_id),
                  RF_PERSONNEL_TEXT_MAX);
        copy_text(person->department, person_department(actor->character_id),
                  RF_PERSONNEL_TEXT_MAX);
        copy_text(person->readiness, person_readiness(actor), RF_PERSONNEL_TEXT_MAX);
        copy_text(person->health_state, person_health(actor), RF_PERSONNEL_TEXT_MAX);
        person_assignment(actor, person->assignment, RF_PERSONNEL_TEXT_MAX);
    }
    return 0;
}

int rf_application_projection_logic_test(void)
{
    struct rf_application_query_context context;
    rf_application_query_init(&context, NULL, NULL, NULL);
    if (context.core || context.game_runtime || context.command) return 1;
    if (rf_application_query_core_status(&context, NULL) == 0) return 2;
    if (rf_application_query_game_status(&context, NULL) == 0) return 3;
    return 0;
}
