/* Listener projection only. The audio owner supplies the original PCM and
 * keeps all mixing state on its existing audio thread. */
#include "rasterfall_weaver_audio.h"
#include "rf_mesh_weaver_layout_generated.h"

static void rf_weaver_audio_update(const struct rasterfall_session *s,
    const struct camera *listener,struct rasterfall_audio *audio,int paused)
{
    struct rasterfall_weaver_audio_input input={0};
    const struct toy_mesh_weaver *w=&s->game_state.weaver;
    const struct rf_map_runtime_object *machine=
        rf_map_runtime_find_object(&s->map_ops.runtime,"mesh_weaver");
    input.world_generation=s->scene_local.world_generation;
    input.enabled=machine && w->enabled && s->game_state.state==TOY_GAME_PLAYING;
    input.job_serial=w->job_serial;input.produced_count=w->produced_count;
    input.phase=w->phase;input.paused=paused || w->pause_reason;
    if(input.enabled && listener) {
        double dx=(double)machine->x+512.0*rf_mesh_weaver_build_center_m[0]-listener->x;
        double dy=(double)machine->y-900.0+512.0*rf_mesh_weaver_build_center_m[1]-listener->y;
        double dz=(double)machine->z+512.0*rf_mesh_weaver_build_center_m[2]-listener->z;
        double distance=sqrt(dx*dx+dy*dy+dz*dz),gain=0.0;
        if(distance<4096.0) {
            gain=distance<=1024.0 ? 1.0 : (4096.0-distance)/3072.0;
            gain*=gain;
        }
        double pan=distance>1.0 ?
            (dx*listener->cy-dz*listener->sy)/(1024.0*distance) : 0.0;
        if(pan>1.0)pan=1.0;
        if(pan< -1.0)pan=-1.0;
        input.left_q8=(int)(256.0*gain*(pan>0.0?1.0-.55*pan:1.0));
        input.right_q8=(int)(256.0*gain*(pan<0.0?1.0+.55*pan:1.0));
    }
    /* Zero-gain frames still advance the observed task identity; approaching
     * an already running machine must not replay old startup/completion cues. */
    rasterfall_audio_weaver(audio,&input);
}
