/* Runtime asset loaders validate headers, ranges and triangle indices. */
#include "toy_assets.h"
/* EXPECT: 0 */
int main(void) {
    struct toy_texture_asset t, tj; struct toy_sound_asset s; struct toy_mesh_asset m;
    if (toy_texture_load("assets/generated/test.ttex", &t) < 0) return 1;
    if (t.width != 1 || t.height != 1 || t.data_size != 3) return 2;
    toy_texture_unload(&t);
    if (toy_texture_load("assets/generated/test.jpg.ttex", &tj) < 0) return 7;
    if (tj.width != 16 || tj.height != 16 || tj.data_size != 768) return 8;
    toy_texture_unload(&tj);
    if (toy_sound_load("assets/generated/test.tsnd", &s) < 0) return 3;
    if (s.rate != 44100 || s.channels != 1 || s.frames != 1) return 4;
    toy_sound_unload(&s);
    if (toy_mesh_load("assets/generated/test.tmesh", &m) < 0) return 5;
    if (m.vertex_count != 3 || m.index_count != 3) return 6;
    toy_mesh_unload(&m);
    return 0;
}
