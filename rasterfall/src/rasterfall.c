#include "tlibc_everything.h"
#include "rf_game_lifecycle.h"

/* Process entry only.  Core host setup and the game loop are implemented by
 * the runtime module; keeping this translation unit deliberately boring makes
 * the ownership boundary visible to tools and to future platform ports. */
int main(int argc, char **argv)
{
    return rf_game_runtime_main(argc, argv);
}
