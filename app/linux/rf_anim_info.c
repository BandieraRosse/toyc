#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_action.h"

int main(int argc, char **argv)
{
    struct rasterfall_action_clip clip;
    if (argc != 2) { __fprintf(2, "usage: rf_anim_info action.rfanim\n"); return 2; }
    if (rasterfall_action_load(&clip, argv[1]) < 0) {
        __fprintf(2, "rf_anim_info: invalid action: %s\n", argv[1]); return 1;
    }
    rasterfall_action_dump(&clip);
    return 0;
}
