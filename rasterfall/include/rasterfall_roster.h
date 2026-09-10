#ifndef RASTERFALL_ROSTER_H
#define RASTERFALL_ROSTER_H

#include "rasterfall_character.h"

#define RASTERFALL_SQUAD_SIZE 4

enum rasterfall_squad_id {
    RASTERFALL_SQUAD_STANDARD_RESPONSE,
    RASTERFALL_SQUAD_ASSAULT,
    RASTERFALL_SQUAD_COUNT
};

struct rasterfall_roster_member {
    int character_id;
    const char *name;
};

/* Ordered game-content roster. It intentionally contains no AI, formation,
 * leader, or gameplay-ability data. */
struct rasterfall_squad_roster {
    int squad_id;
    const char *name;
    struct rasterfall_roster_member members[RASTERFALL_SQUAD_SIZE];
};

const struct rasterfall_squad_roster *rasterfall_squad_roster(int squad_id);
int rasterfall_roster_logic_test(void);

#endif
