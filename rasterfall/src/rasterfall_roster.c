#include "rasterfall_roster.h"
#include "string.h"

static const struct rasterfall_squad_roster rosters[] = {
    {
        RASTERFALL_SQUAD_STANDARD_RESPONSE, "Standard Response Squad",
        {
            { RASTERFALL_CHARACTER_RF_RIFLEMAN, "Jesus" },
            { RASTERFALL_CHARACTER_SQUAD_A_MEDIC, "Squad A Medic" },
            { RASTERFALL_CHARACTER_SQUAD_A_ENGINEER, "Squad A Engineer" },
            { RASTERFALL_CHARACTER_SQUAD_A_RECON, "Squad A Recon" }
        }
    },
    {
        RASTERFALL_SQUAD_ASSAULT, "Assault Squad",
        {
            { RASTERFALL_CHARACTER_SQUAD_B_RIFLEMAN, "Squad B Rifleman" },
            { RASTERFALL_CHARACTER_SQUAD_B_BREACHER, "Squad B Breacher" },
            { RASTERFALL_CHARACTER_SQUAD_B_HEAVY, "Squad B Heavy" },
            { RASTERFALL_CHARACTER_SQUAD_B_MEDIC, "Squad B Medic" }
        }
    }
};

const struct rasterfall_squad_roster *rasterfall_squad_roster(int squad_id)
{
    if (squad_id < 0 || squad_id >= RASTERFALL_SQUAD_COUNT) return NULL;
    return &rosters[squad_id];
}

int rasterfall_roster_logic_test(void)
{
    static const int expected[] = {
        RASTERFALL_CHARACTER_RF_RIFLEMAN,
        RASTERFALL_CHARACTER_SQUAD_A_MEDIC,
        RASTERFALL_CHARACTER_SQUAD_A_ENGINEER,
        RASTERFALL_CHARACTER_SQUAD_A_RECON,
        RASTERFALL_CHARACTER_SQUAD_B_RIFLEMAN,
        RASTERFALL_CHARACTER_SQUAD_B_BREACHER,
        RASTERFALL_CHARACTER_SQUAD_B_HEAVY,
        RASTERFALL_CHARACTER_SQUAD_B_MEDIC
    };
    int squad, member, flat = 0;
    for (squad = 0; squad < RASTERFALL_SQUAD_COUNT; squad++) {
        const struct rasterfall_squad_roster *roster =
            rasterfall_squad_roster(squad);
        if (!roster || roster->squad_id != squad || !roster->name) return 1;
        for (member = 0; member < RASTERFALL_SQUAD_SIZE; member++) {
            if (!roster->members[member].name ||
                roster->members[member].character_id != expected[flat++] ||
                rasterfall_character_profile(
                    roster->members[member].character_id)->id !=
                    roster->members[member].character_id)
                return 1;
        }
    }
    return rasterfall_squad_roster(-1) != NULL ||
           rasterfall_squad_roster(RASTERFALL_SQUAD_COUNT) != NULL ||
           strcmp(rosters[0].name, "Standard Response Squad") ||
           strcmp(rosters[1].name, "Assault Squad");
}
