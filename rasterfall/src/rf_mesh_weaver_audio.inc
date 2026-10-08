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
        int x=machine->x+(int)(512.0*rf_mesh_weaver_build_center_m[0]);
        int y=machine->y-900+(int)(512.0*rf_mesh_weaver_build_center_m[1]);
        int z=machine->z+(int)(512.0*rf_mesh_weaver_build_center_m[2]);
        struct rf_audio_spatial gain;
        rf_audio_spatial_gain(&gain,x-audio->listener_x,y-audio->listener_y,
            z-audio->listener_z,audio->listener_sy,audio->listener_cy,1024,4096);
        input.left_q8=gain.left_q8;input.right_q8=gain.right_q8;
    }
    /* Zero-gain frames still advance the observed task identity; approaching
     * an already running machine must not replay old startup/completion cues. */
    rasterfall_audio_weaver(audio,&input);
}
