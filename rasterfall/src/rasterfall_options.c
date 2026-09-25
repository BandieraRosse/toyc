#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "rasterfall_net.h"
#include "rasterfall_model.h"
#include "rasterfall_enemy_visual.h"
#include "rasterfall_options.h"
#include "rf_gpu_vulkan_backend.h"

static int positive_int(const char *text, int fallback)
{
    int value = 0;
    if (!text || *text < '0' || *text > '9') return fallback;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text++ - '0');
        if (value > 1000000) return fallback;
    }
    return *text || value <= 0 ? fallback : value;
}

static int signed_int(const char *text, int *value)
{
    int sign = 1, result = 0;
    if (!text || !*text || !value) return -1;
    if (*text == '-') { sign = -1; text++; }
    if (*text < '0' || *text > '9') return -1;
    while (*text >= '0' && *text <= '9') {
        if (result > 100000000) return -1;
        result = result * 10 + (*text++ - '0');
    }
    if (*text) return -1;
    *value = result * sign;
    return 0;
}

int rasterfall_options_default_textures_enabled(void)
{
#ifdef TOYC_WINDOWS
    return 1;
#else
    return 0;
#endif
}

static int numeric_argument(int argc, char **argv, int arg)
{
    return arg + 1 < argc && argv[arg + 1][0] >= '0' &&
           argv[arg + 1][0] <= '9';
}

static int require_arguments(int argc, char **argv, int arg, int count,
                             const char *option)
{
    int i;
    if (arg + count < argc) {
        for (i = 1; i <= count; i++)
            if (argv[arg + i][0] == '-') break;
        if (i > count) return 0;
    }
    __fprintf(2, "rasterfall: option %s requires %d argument%s\n",
              option, count, count == 1 ? "" : "s");
    return -1;
}

void rasterfall_options_init(struct rasterfall_options *o,
                             int textures_enabled)
{
    memset(o, 0, sizeof(*o));
    o->requested_net_mode = RASTERFALL_NET_OFF;
    o->net_port = RASTERFALL_NET_DEFAULT_PORT;
    o->textures_enabled = textures_enabled;
    o->enemy_visual_family = RASTERFALL_ENEMY_VISUAL_AUTO;
    o->edge_pass_enabled = 1;
    o->stats_enabled = 1;
    o->model_views_supersample = 1;
    o->model_skinning = -1;
    o->gpu_character_skinning = 1;
    o->model_pose = RASTERFALL_MODEL_POSE_BIND;
    o->performance_iterations = 5;
    o->performance_warmup = 3;
    o->performance_repeats = 3;
    o->actor_raster_workers = 8;
    o->gpu_present_fault_frame = 1;
}

void rasterfall_options_usage(int fd)
{
    __fprintf(fd,
        "usage: rasterfall [runtime options]\n"
        "  --host | --connect <ip> [--port <port>] [--net-loss <percent>]\n"
        "  --textures | --no-textures  --no-edge-pass  --no-stats\n"
        "  --renderer <cpu|gpu-compute> [--gpu-required] [--gpu-native-present]\n"
        "  --gpu-present-fault <acquire-out-of-date|record-failure|submit-failure|present-out-of-date|present-suboptimal> [frame]\n"
        "  --legacy-map  (force legacy map loader)\n"
        "  --map <path>  (load an explicit V1 map for local inspection)\n"
        "  --texture-stats  --frames <count>  --dump-frame <path>\n"
        "  --logic-test  --input-test  --action-runtime-debug  --auto  --frame-audit  --gpu-rb0-stats\n"
        "  --gpu-scene-native-fixture (isolated frozen map/body/head native Scene)\n"
        "  --gpu-scene-world-preview (experimental WORLD-only native Scene; other layers pending)\n"
        "  --gpu-scene-independent-preview (diagnostic independent layered Scene)\n"
        "  --gpu-scene-play (experimental single-player independent native GPU renderer)\n"
        "  --gpu-scene-pose-test (frozen rifleman palette/attachment resource regression)\n"
        "  --gpu-world-cycle-test  (diagnostic Outpost/Campaign/WHU/Campaign runtime cycle)\n"
        "  --gpu-normal-scene <near|near-heavy|enemy-cull-in|enemy-cull-out|enemy-cull-imported-in|enemy-cull-imported-out|mid|interior|thin-far|base|spawn|west-facility|map-wall|map-ramp|map-platform|map-label|map-sign|model-legacy|model-special|enemy-special|enemy-death|enemy-death-west|scene-effects-stress|enemy-fade|enemy-tongue|actor-procedural|frame-effects|model-infected|actor-rifleman|actor-standard|actor-assault|projectile|pickup|map-gate-on|map-gate-off|map-near|map-thin|whu-a18|whu-b-plaza|whu-library|whu-d-ef> <0|10|20|30|60|64>\n"
        "  --gpu-normal-fixed-tick  (diagnostic: one 16ms gameplay tick per rendered normal-scene or wave-repro frame)\n"
        "  --gpu-character-vertex-diff  (frame 30 device-local position/normal proof)\n"
        "  --gpu-character-skinning-off  (use the CPU-skinned vertex upload rollback path)\n"
        "  --enemy-visual-capture <output-dir> (families + rigid specials; attack keys, silhouette, world, death)\n"
        "  --enemy-visual-family <legacy|block-infected|humanoid-infected> (default: mixed)\n"
        "  --visual-capture <desktop-v1|procedural-humanoid|hurd-squad|lighting-props|modular-teammate> --visual-output <path.bmp>\n"
        "  --visual-capture <arch-family|arch-alley|arch-hall> --visual-output <path.bmp>\n"
        "  --visual-capture <campus-corner|campus-corner-near|campus-corner-mid|campus-corner-far> --visual-output <path.bmp>\n"
        "    campus-asset-<name> captures one Temporary Campus Kit module\n"
        "    campus-corner-ground inspects plaza, stairs, retaining wall and track edge\n"
        "    arch-alley / arch-hall also accept -inside, -far, -reverse suffixes\n"
        "    arch-asset-<name> captures one architectural module at an elevated metric view\n"
        "  --character-acceptance <model.rmesh> <output-dir>\n"
        "  --eula-animation-acceptance <model-dir> <output-dir>\n"
        "  --profession-lineup <model-dir> <output-dir>\n"
        "  --squad-acceptance <model-dir> <output-dir>\n"
        "  --rigid-attachment-acceptance <model-dir> <output-dir>\n"
        "  --character-world-capture <output-dir> [--character-world-model <model.rmesh>]\n"
        "  --environment-capture <output-dir> (Campaign views; WHU views with --map)\n"
        "  --normal-frame-audit <x> <z> <sy> <cy> <pitch-sy> <pitch-cy> <width> <height> <output.bmp>\n"
        "  --model-views <model> <dir> [--model-views-supersample <1|2>]\n"
        "  --model-static-views <model> <dir>\n"
        "  --model-pose-views <model> <dir> <bind|right-arm|arms|body|rfchar-test>\n"
        "  --model-material-regression <model> <dir>\n"
        "  --model-performance <model> [iterations] [workers]\n"
        "  --character-performance <model> [warmup] [frames] [repeats] [workers]\n"
        "  --character-performance-suite [warmup] [frames] [repeats] [workers]\n"
        "  --render-performance [iterations] (headless world/enemy cost ablations)\n"
        "  --gpu-world-raster-test <near|mid> <0|30> <commands.bin>\n"
        "  --gpu-normal-scene <view> <0|10|20|30|60> (normal deterministic Campaign runtime; views listed above)\n"
        "    UI views: ui-pause|ui-scoreboard|ui-shop|ui-over|ui-won\n"
        "    Corridor: west-empty 0; west-button|west-button-no-tank 0|16|32|64 (0 means 16; buttons at frame 61)\n"
        "  --gpu-frame-capture <output.bmp> [--gpu-capture-frame <N>] (native mixed GPU final image; with --frame-audit also writes <output.bmp>.scene.ppm; independent Scene preview writes only <output.bmp>.scene.ppm; default frame 30)\n"
        "  --gpu-wave-repro (start the real wave timer immediately in the loaded world)\n"
        "  --actor-performance [iterations] [frontend-workers] [raster-workers]\n"
        "  --model-bones <model> [search]  --model-humanoid <model>\n"
        "  --model-humanoid-basis <model>\n"
        "  --model-retarget-test <model> <right-arm|left-arm|right-leg|chest>\n"
        "  --model-glb-animation <model> <glb> <clip>\n"
        "  --model-glb-motion-diagnostic <model> <glb>\n"
        "  --action-info <action.rfanim>\n"
        "  --action-preview <model.rmesh> <action.rfanim> <time-ms> <output.bmp>\n"
        "  --action-composition-capture <model.rmesh> <lower.rfanim> <lower-ms> <upper.rfanim> <upper-ms> <additive.rfanim> <additive-ms> <output.bmp>\n"
        "  --pose-debug <model.rmesh> <action.rfanim> <time-ms> <humanoid-role>\n"
        "  --pose-debug <model.rmesh> <lower.rfanim> <lower-ms> <upper.rfanim> <upper-ms> <humanoid-role>\n"
        "  --pose-debug <model.rmesh> <lower.rfanim> <lower-ms> <upper.rfanim> <upper-ms> <additive.rfanim> <additive-ms> <humanoid-role>\n"
        "  legacy VMD diagnostics (old PMX/VMD path):\n"
        "    --vmd-eula-walk <model> <vmd>\n"
        "    --vmd-freeze-head | --vmd-freeze-torso\n"
        "    --vmd-disable-ik | --vmd-disable-grant\n"
        "    --vmd-legacy-root-offset | --vmd-legacy-leg-ccd\n"
        "    --vmd-skin-trace\n");
}

int rasterfall_options_parse(struct rasterfall_options *o, int argc, char **argv)
{
    int arg;
    for (arg = 1; arg < argc; arg++) {
        const char *option = argv[arg];
        if (!strcmp(option, "--help")) {
            rasterfall_options_usage(1);
            return 1;
        } else if (!strcmp(option, "--input-test")) o->input_debug = 1;
        else if (!strcmp(option, "--legacy-map")) o->legacy_map = 1;
        else if (!strcmp(option, "--map")) {
            if (require_arguments(argc, argv, arg, 1, option) < 0) return -1;
            o->map_path = argv[++arg];
        }
        else if (!strcmp(option, "--action-runtime-debug")) o->action_runtime_debug = 1;
        else if (!strcmp(option, "--frame-audit")) o->frame_audit = 1;
        else if (!strcmp(option, "--gpu-rb0-stats")) o->gpu_rb0_stats = 1;
        else if (!strcmp(option, "--gpu-world-cycle-test")) o->world_cycle_gate = 1;
        else if (!strcmp(option, "--gpu-scene-native-fixture")) o->gpu_scene_native_fixture = 1;
        else if (!strcmp(option, "--gpu-scene-world-preview")) o->gpu_scene_world_preview = 1;
        else if (!strcmp(option, "--gpu-scene-play")) o->gpu_scene_play = 1;
        else if (!strcmp(option, "--gpu-scene-independent-preview")) {
            o->gpu_scene_independent_preview = 1;
            o->gpu_scene_world_preview = 1;
        }
        else if (!strcmp(option, "--gpu-scene-pose-test")) o->gpu_scene_pose_test = 1;
        else if (!strcmp(option, "--logic-test") ||
                 !strcmp(option, "--net-test")) o->logic_test = 1;
        else if (!strcmp(option, "--host"))
            o->requested_net_mode = RASTERFALL_NET_HOST;
        else if (!strcmp(option, "--connect")) {
            if (require_arguments(argc,argv,arg,1,option)<0) return -1;
            o->requested_net_mode=RASTERFALL_NET_CLIENT;o->net_address=argv[++arg];
        } else if (!strcmp(option, "--port")) {
            if (require_arguments(argc,argv,arg,1,option)<0) return -1;
            o->net_port=positive_int(argv[++arg],0);
            if (!o->net_port || o->net_port > 65535) {
                __fprintf(2,"rasterfall: invalid port\n"); return -1; }
        } else if (!strcmp(option, "--net-loss")) {
            if (require_arguments(argc,argv,arg,1,option)<0) return -1;
            arg++;
            o->net_loss_percent=!strcmp(argv[arg],"0") ? 0 :
                positive_int(argv[arg],-1);
            if (o->net_loss_percent<0 || o->net_loss_percent>100) {
                __fprintf(2,"rasterfall: invalid net loss percent\n");return -1;
            }
        } else if (!strcmp(option,"--auto")) o->auto_mode=1;
        else if (!strcmp(option,"--renderer")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            arg++;
            if(!strcmp(argv[arg],"cpu"))o->renderer_mode=0;
            else if(!strcmp(argv[arg],"gpu-compute"))o->renderer_mode=1;
            else {__fprintf(2,"rasterfall: renderer must be cpu or gpu-compute\n");return -1;}
        }
        else if (!strcmp(option,"--gpu-required")) o->gpu_required=1;
        else if (!strcmp(option,"--gpu-native-present")) o->gpu_native_present=1;
        else if (!strcmp(option,"--gpu-present-fault")) {
            const char *fault;
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            fault=argv[++arg];
            if(!strcmp(fault,"acquire-out-of-date"))
                o->gpu_present_fault=RF_GPU_PRESENT_FAULT_ACQUIRE_OUT_OF_DATE;
            else if(!strcmp(fault,"record-failure"))
                o->gpu_present_fault=RF_GPU_PRESENT_FAULT_RECORD_FAILURE;
            else if(!strcmp(fault,"submit-failure"))
                o->gpu_present_fault=RF_GPU_PRESENT_FAULT_SUBMIT_FAILURE;
            else if(!strcmp(fault,"present-out-of-date"))
                o->gpu_present_fault=RF_GPU_PRESENT_FAULT_PRESENT_OUT_OF_DATE;
            else if(!strcmp(fault,"present-suboptimal"))
                o->gpu_present_fault=RF_GPU_PRESENT_FAULT_PRESENT_SUBOPTIMAL;
            else {
                __fprintf(2,"rasterfall: invalid GPU present fault %s\n",fault);
                return -1;
            }
            if(numeric_argument(argc,argv,arg))
                o->gpu_present_fault_frame=positive_int(argv[++arg],0);
            if(!o->gpu_present_fault_frame)return -1;
        }
        else if (!strcmp(option,"--gpu-frame-capture")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->gpu_frame_capture=argv[++arg];
        } else if (!strcmp(option,"--gpu-capture-frame")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->gpu_capture_frame=positive_int(argv[++arg],0);
            if (!o->gpu_capture_frame) return -1;
        }
        else if (!strcmp(option,"--textures")) o->textures_enabled=1;
        else if (!strcmp(option,"--no-textures")) o->textures_enabled=0;
        else if (!strcmp(option,"--edge-pass")) o->edge_pass_enabled=1;
        else if (!strcmp(option,"--no-edge-pass")) o->edge_pass_enabled=0;
        else if (!strcmp(option,"--no-stats")) o->stats_enabled=0;
        else if (!strcmp(option,"--texture-stats")) o->texture_stats=1;
        else if (!strcmp(option,"--dump-frame")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->dump_path=argv[++arg];
        } else if (!strcmp(option,"--enemy-visual-capture")) {
            if (require_arguments(argc, argv, arg, 1, option) < 0) return -1;
            o->enemy_visual_capture_dir = argv[++arg];
        } else if (!strcmp(option,"--enemy-visual-family")) {
            if (require_arguments(argc, argv, arg, 1, option) < 0) return -1;
            const char *family = argv[++arg];
            if (!strcmp(family, "legacy")) o->enemy_visual_family = 0;
            else if (!strcmp(family, "block-infected")) o->enemy_visual_family = 1;
            else if (!strcmp(family, "humanoid-infected")) o->enemy_visual_family = 2;
            else { __fprintf(2, "rasterfall: invalid enemy visual family: %s\n", family); return -1; }
        } else if (!strcmp(option,"--visual-capture")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->visual_scenario=argv[++arg];
        } else if (!strcmp(option,"--visual-output")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->visual_output=argv[++arg];
        } else if (!strcmp(option,"--profession-lineup")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->profession_lineup_models=argv[++arg];
            o->profession_lineup_dir=argv[++arg];
        } else if (!strcmp(option,"--squad-acceptance")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->squad_acceptance_models=argv[++arg];
            o->squad_acceptance_dir=argv[++arg];
        } else if (!strcmp(option,"--rigid-attachment-acceptance")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->rigid_attachment_models=argv[++arg];
            o->rigid_attachment_dir=argv[++arg];
        } else if (!strcmp(option,"--character-acceptance")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->character_acceptance_model=argv[++arg];
            o->character_acceptance_dir=argv[++arg];
        } else if (!strcmp(option,"--eula-animation-acceptance")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->eula_acceptance_models=argv[++arg];
            o->eula_acceptance_dir=argv[++arg];
        } else if (!strcmp(option,"--render-performance")) {
            o->render_performance=1;
            if(numeric_argument(argc,argv,arg))o->performance_iterations=positive_int(argv[++arg],o->performance_iterations);
        } else if (!strcmp(option,"--gpu-world-raster-test")) {
            if(require_arguments(argc,argv,arg,3,option)<0)return -1;
            o->gpu_world_raster_view=argv[++arg];
            o->gpu_world_raster_enemies=atoi(argv[++arg]);
            o->gpu_world_raster_output=argv[++arg];
            if ((strcmp(o->gpu_world_raster_view,"near") &&
                 strcmp(o->gpu_world_raster_view,"mid")) ||
                (o->gpu_world_raster_enemies != 0 &&
                 o->gpu_world_raster_enemies != 30)) {
                __fprintf(2,"rasterfall: --gpu-world-raster-test expects near|mid and 0|30\n");
                return -1;
            }
        } else if (!strcmp(option,"--gpu-normal-scene")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->gpu_normal_view=argv[++arg];
            o->gpu_normal_enemies=atoi(argv[++arg]);
            if ((strcmp(o->gpu_normal_view,"near") &&
                 strcmp(o->gpu_normal_view,"near-heavy") &&
                 strcmp(o->gpu_normal_view,"enemy-cull-in") &&
                 strcmp(o->gpu_normal_view,"enemy-cull-out") &&
                 strcmp(o->gpu_normal_view,"enemy-cull-imported-in") &&
                 strcmp(o->gpu_normal_view,"enemy-cull-imported-out") &&
                 strcmp(o->gpu_normal_view,"mid") &&
                 strcmp(o->gpu_normal_view,"interior") &&
                 strcmp(o->gpu_normal_view,"thin-far") &&
                 strcmp(o->gpu_normal_view,"base") &&
                 strcmp(o->gpu_normal_view,"spawn") &&
                 strcmp(o->gpu_normal_view,"west-facility") &&
                 strcmp(o->gpu_normal_view,"west-empty") &&
                 strcmp(o->gpu_normal_view,"west-button") &&
                 strcmp(o->gpu_normal_view,"west-button-no-tank") &&
                 strcmp(o->gpu_normal_view,"map-wall") &&
                 strcmp(o->gpu_normal_view,"map-ramp") &&
                 strcmp(o->gpu_normal_view,"map-platform") &&
                 strcmp(o->gpu_normal_view,"map-label") &&
                 strcmp(o->gpu_normal_view,"map-sign") &&
                 strcmp(o->gpu_normal_view,"model-legacy") &&
                 strcmp(o->gpu_normal_view,"model-special") &&
                 strcmp(o->gpu_normal_view,"enemy-special") &&
                 strcmp(o->gpu_normal_view,"enemy-death") &&
                 strcmp(o->gpu_normal_view,"enemy-death-west") &&
                 strcmp(o->gpu_normal_view,"scene-effects-stress") &&
                 strcmp(o->gpu_normal_view,"enemy-fade") &&
                 strcmp(o->gpu_normal_view,"frame-effects") &&
                 strcmp(o->gpu_normal_view,"ui-pause") &&
                 strcmp(o->gpu_normal_view,"ui-scoreboard") &&
                 strcmp(o->gpu_normal_view,"ui-shop") &&
                 strcmp(o->gpu_normal_view,"ui-over") &&
                 strcmp(o->gpu_normal_view,"ui-won") &&
                 strcmp(o->gpu_normal_view,"enemy-tongue") &&
                 strcmp(o->gpu_normal_view,"actor-procedural") &&
                 strcmp(o->gpu_normal_view,"model-infected") &&
                 strcmp(o->gpu_normal_view,"actor-rifleman") &&
                 strcmp(o->gpu_normal_view,"actor-standard") &&
                 strcmp(o->gpu_normal_view,"actor-assault") &&
                 strcmp(o->gpu_normal_view,"projectile") &&
                 strcmp(o->gpu_normal_view,"pickup") &&
                 strcmp(o->gpu_normal_view,"map-gate-on") &&
                 strcmp(o->gpu_normal_view,"map-gate-off") &&
                 strcmp(o->gpu_normal_view,"map-near") &&
                 strcmp(o->gpu_normal_view,"map-thin") &&
                 strcmp(o->gpu_normal_view,"whu-a18") &&
                 strcmp(o->gpu_normal_view,"whu-b-plaza") &&
                 strcmp(o->gpu_normal_view,"whu-library") &&
                 strcmp(o->gpu_normal_view,"whu-d-ef")) ||
                ((!strcmp(o->gpu_normal_view,"west-button") ||
                  !strcmp(o->gpu_normal_view,"west-button-no-tank")) ?
                 (o->gpu_normal_enemies != 0 && o->gpu_normal_enemies != 16 &&
                  o->gpu_normal_enemies != 32 && o->gpu_normal_enemies != 64) :
                 !strcmp(o->gpu_normal_view,"west-empty") ? o->gpu_normal_enemies != 0 :
                 (!strcmp(o->gpu_normal_view,"enemy-cull-in") ||
                  !strcmp(o->gpu_normal_view,"enemy-cull-out") ||
                  !strcmp(o->gpu_normal_view,"enemy-cull-imported-in") ||
                  !strcmp(o->gpu_normal_view,"enemy-cull-imported-out")) ?
                 o->gpu_normal_enemies != 64 :
                 (o->gpu_normal_enemies != 0 &&
                 o->gpu_normal_enemies != 10 &&
                 o->gpu_normal_enemies != 20 &&
                 o->gpu_normal_enemies != 30 &&
                 o->gpu_normal_enemies != 60))) {
                __fprintf(2,"rasterfall: invalid --gpu-normal-scene view or enemy count\n");
                return -1;
            }
        } else if (!strcmp(option,"--gpu-normal-fixed-tick")) {
            o->gpu_normal_fixed_tick=1;
        } else if (!strcmp(option,"--gpu-character-vertex-diff")) {
            o->gpu_character_vertex_diff=1;
        } else if (!strcmp(option,"--gpu-character-skinning-off")) {
            o->gpu_character_skinning=0;
        } else if (!strcmp(option,"--gpu-wave-repro")) {
            o->gpu_wave_repro=1;
        } else if (!strcmp(option,"--environment-capture")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->environment_capture_dir=argv[++arg];
        } else if (!strcmp(option,"--normal-frame-audit")) {
            if (arg + 9 >= argc ||
                signed_int(argv[arg + 1], &o->normal_frame_audit_x) < 0 ||
                signed_int(argv[arg + 2], &o->normal_frame_audit_z) < 0 ||
                signed_int(argv[arg + 3], &o->normal_frame_audit_sy) < 0 ||
                signed_int(argv[arg + 4], &o->normal_frame_audit_cy) < 0 ||
                signed_int(argv[arg + 5], &o->normal_frame_audit_pitch_sy) < 0 ||
                signed_int(argv[arg + 6], &o->normal_frame_audit_pitch_cy) < 0 ||
                signed_int(argv[arg + 7], &o->normal_frame_audit_width) < 0 ||
                signed_int(argv[arg + 8], &o->normal_frame_audit_height) < 0) {
                __fprintf(2,"rasterfall: --normal-frame-audit expects pose, pitch, extent and output.bmp\n");
                return -1;
            }
            arg += 8;
            o->normal_frame_audit_output=argv[++arg];
            if ((!o->normal_frame_audit_sy && !o->normal_frame_audit_cy) ||
                o->normal_frame_audit_sy < -1024 || o->normal_frame_audit_sy > 1024 ||
                o->normal_frame_audit_cy < -1024 || o->normal_frame_audit_cy > 1024 ||
                o->normal_frame_audit_pitch_sy < -1024 || o->normal_frame_audit_pitch_sy > 1024 ||
                o->normal_frame_audit_pitch_cy < -1024 || o->normal_frame_audit_pitch_cy > 1024 ||
                o->normal_frame_audit_width <= 0 || o->normal_frame_audit_width > 7680 ||
                o->normal_frame_audit_height <= 0 || o->normal_frame_audit_height > 4320) {
                __fprintf(2,"rasterfall: invalid audit direction, pitch, or extent\n");
                return -1;
            }
        } else if (!strcmp(option,"--character-world-capture")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->character_world_capture_dir=argv[++arg];
        } else if (!strcmp(option,"--character-world-model")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->character_world_capture_model=argv[++arg];
        } else if (!strcmp(option,"--model-views")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->view_model_path=argv[++arg];o->view_output_dir=argv[++arg];
        } else if (!strcmp(option,"--model-views-supersample")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->model_views_supersample=positive_int(argv[++arg],0);
            if(o->model_views_supersample!=1&&o->model_views_supersample!=2){
                __fprintf(2,"rasterfall: supersample must be 1 or 2\n");return -1;}
        } else if (!strcmp(option,"--model-static-views")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->view_model_path=argv[++arg];o->view_output_dir=argv[++arg];o->model_skinning=0;
        } else if (!strcmp(option,"--model-pose-views")) {
            const char *pose;
            if(require_arguments(argc,argv,arg,3,option)<0)return -1;
            o->view_model_path=argv[++arg];o->view_output_dir=argv[++arg];pose=argv[++arg];
            o->model_skinning=1;
            if(!strcmp(pose,"bind"))o->model_pose=RASTERFALL_MODEL_POSE_BIND;
            else if(!strcmp(pose,"right-arm"))o->model_pose=RASTERFALL_MODEL_POSE_RIGHT_ARM;
            else if(!strcmp(pose,"arms"))o->model_pose=RASTERFALL_MODEL_POSE_ARMS;
            else if(!strcmp(pose,"body"))o->model_pose=RASTERFALL_MODEL_POSE_BODY_TURN;
            else if(!strcmp(pose,"rfchar-test"))o->model_pose=RASTERFALL_MODEL_POSE_RFCHAR_TEST;
            else {__fprintf(2,"rasterfall: invalid model pose %s\n",pose);return -1;}
        } else if (!strcmp(option,"--model-bones")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->bone_model_path=argv[++arg];
            if(arg+1<argc&&argv[arg+1][0]!='-')o->bone_search=argv[++arg];
        } else if (!strcmp(option,"--model-humanoid")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->humanoid_model_path=argv[++arg];
        } else if (!strcmp(option,"--model-humanoid-basis")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->humanoid_basis_model_path=argv[++arg];
        } else if (!strcmp(option,"--model-retarget-test")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->retarget_model_path=argv[++arg];o->retarget_action=argv[++arg];
        } else if (!strcmp(option,"--model-glb-animation")) {
            if(require_arguments(argc,argv,arg,3,option)<0)return -1;
            o->glb_animation_model=argv[++arg];o->glb_animation_path=argv[++arg];o->glb_animation_name=argv[++arg];
        } else if (!strcmp(option,"--model-glb-motion-diagnostic")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->glb_motion_model=argv[++arg];o->glb_motion_path=argv[++arg];
        } else if (!strcmp(option,"--action-info")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->action_info_path=argv[++arg];
        } else if (!strcmp(option,"--action-preview")) {
            if(require_arguments(argc,argv,arg,4,option)<0)return -1;
            o->action_preview_model=argv[++arg];o->action_preview_path=argv[++arg];
            o->action_time_ms=!strcmp(argv[++arg],"0")?0:positive_int(argv[arg],-1);
            o->action_preview_output=argv[++arg];
            if(o->action_time_ms<0){__fprintf(2,"rasterfall: invalid action time\n");return -1;}
        } else if (!strcmp(option,"--action-composition-capture")) {
            if(require_arguments(argc,argv,arg,8,option)<0)return -1;
            o->composition_capture_model=argv[++arg];
            o->composition_capture_lower=argv[++arg];
            o->composition_capture_lower_time=!strcmp(argv[++arg],"0")?0:positive_int(argv[arg],-1);
            o->composition_capture_upper=argv[++arg];
            o->composition_capture_upper_time=!strcmp(argv[++arg],"0")?0:positive_int(argv[arg],-1);
            o->composition_capture_additive=argv[++arg];
            o->composition_capture_additive_time=!strcmp(argv[++arg],"0")?0:positive_int(argv[arg],-1);
            o->composition_capture_output=argv[++arg];
            if(o->composition_capture_lower_time<0 || o->composition_capture_upper_time<0 ||
               o->composition_capture_additive_time<0)return -1;
        } else if (!strcmp(option,"--pose-debug")) {
            if(require_arguments(argc,argv,arg,4,option)<0)return -1;
            o->pose_debug_model=argv[++arg];o->pose_debug_action=argv[++arg];
            o->action_time_ms=!strcmp(argv[++arg],"0")?0:positive_int(argv[arg],-1);
            if (arg+5 < argc && argv[arg+1][0] != '-' &&
                argv[arg+2][0] != '-' && argv[arg+3][0] != '-' &&
                argv[arg+4][0] != '-' && argv[arg+5][0] != '-') {
                o->pose_debug_upper_action=argv[++arg];
                o->pose_debug_upper_time_ms=!strcmp(argv[++arg],"0")?0:
                    positive_int(argv[arg],-1);
                o->pose_debug_additive_action=argv[++arg];
                o->pose_debug_additive_time_ms=!strcmp(argv[++arg],"0")?0:
                    positive_int(argv[arg],-1);
                o->pose_debug_role=argv[++arg];
            } else if (arg+3 < argc && argv[arg+1][0] != '-' &&
                       argv[arg+2][0] != '-' && argv[arg+3][0] != '-') {
                o->pose_debug_upper_action=argv[++arg];
                o->pose_debug_upper_time_ms=!strcmp(argv[++arg],"0")?0:
                    positive_int(argv[arg],-1);
                o->pose_debug_role=argv[++arg];
            } else o->pose_debug_role=argv[++arg];
            if(o->action_time_ms<0){__fprintf(2,"rasterfall: invalid action time\n");return -1;}
            if(o->pose_debug_upper_action && o->pose_debug_upper_time_ms<0){
                __fprintf(2,"rasterfall: invalid upper action time\n");return -1;}
            if(o->pose_debug_additive_action && o->pose_debug_additive_time_ms<0){
                __fprintf(2,"rasterfall: invalid additive action time\n");return -1;}
        } else if (!strcmp(option,"--vmd-eula-walk")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->vmd_walk_model=argv[++arg];o->vmd_walk_path=argv[++arg];
        } else if (!strcmp(option,"--vmd-freeze-head"))o->vmd_freeze_head=1;
        else if (!strcmp(option,"--vmd-freeze-torso"))o->vmd_freeze_torso=1;
        else if (!strcmp(option,"--vmd-disable-ik"))o->vmd_disable_ik=1;
        else if (!strcmp(option,"--vmd-disable-grant"))o->vmd_disable_grant=1;
        else if (!strcmp(option,"--vmd-legacy-root-offset"))o->vmd_legacy_root_offset=1;
        else if (!strcmp(option,"--vmd-legacy-leg-ccd") ||
                 !strcmp(option,"--vmd-legacy-knee-ccd"))o->vmd_legacy_knee_ccd=1;
        else if (!strcmp(option,"--vmd-skin-trace"))o->vmd_skin_trace=1;
        else if (!strcmp(option,"--model-material-regression")) {
            if(require_arguments(argc,argv,arg,2,option)<0)return -1;
            o->view_model_path=argv[++arg];o->view_output_dir=argv[++arg];o->material_regression=1;
        } else if (!strcmp(option,"--model-performance")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->performance_model_path=argv[++arg];
            if(numeric_argument(argc,argv,arg))o->performance_iterations=positive_int(argv[++arg],o->performance_iterations);
            if(numeric_argument(argc,argv,arg))o->performance_workers=positive_int(argv[++arg],o->performance_workers);
        } else if (!strcmp(option,"--character-performance") ||
                   !strcmp(option,"--character-performance-suite")) {
            o->character_performance_suite=!strcmp(option,"--character-performance-suite");
            if(!o->character_performance_suite){
                if(require_arguments(argc,argv,arg,1,option)<0)return -1;
                o->character_performance_model=argv[++arg];
            }
            if(numeric_argument(argc,argv,arg))o->performance_warmup=positive_int(argv[++arg],o->performance_warmup);
            if(numeric_argument(argc,argv,arg))o->performance_iterations=positive_int(argv[++arg],o->performance_iterations);
            if(numeric_argument(argc,argv,arg))o->performance_repeats=positive_int(argv[++arg],o->performance_repeats);
            if(numeric_argument(argc,argv,arg))o->performance_workers=positive_int(argv[++arg],o->performance_workers);
        } else if (!strcmp(option,"--actor-performance")) {
            o->actor_performance=1;
            if(numeric_argument(argc,argv,arg))o->performance_iterations=positive_int(argv[++arg],o->performance_iterations);
            if(numeric_argument(argc,argv,arg))o->performance_workers=positive_int(argv[++arg],o->performance_workers);
            if(numeric_argument(argc,argv,arg))o->actor_raster_workers=positive_int(argv[++arg],o->actor_raster_workers);
        } else if (!strcmp(option,"--frames")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->frame_limit=positive_int(argv[++arg],0);
            if(!o->frame_limit){__fprintf(2,"rasterfall: invalid frame count\n");return -1;}
        } else {
            __fprintf(2,"rasterfall: unknown option %s\n",option);
            rasterfall_options_usage(2);
            return -1;
        }
    }
    if ((o->visual_scenario != 0) != (o->visual_output != 0)) {
        __fprintf(2,"rasterfall: --visual-capture and --visual-output are required together\n");
        return -1;
    }
    if (o->gpu_scene_play) {
        if (o->requested_net_mode != RASTERFALL_NET_OFF || o->legacy_map) {
            __fprintf(2,"rasterfall: --gpu-scene-play requires single-player Runtime Map\n");
            return -1;
        }
        o->renderer_mode=1;o->gpu_required=1;o->gpu_native_present=1;
        o->gpu_scene_independent_preview=1;o->gpu_scene_world_preview=1;
    }
    if (o->gpu_native_present && !o->renderer_mode) {
        __fprintf(2,"rasterfall: --gpu-native-present requires --renderer gpu-compute\n");
        return -1;
    }
    if (o->gpu_required && !o->gpu_native_present) {
        __fprintf(2,"rasterfall: --gpu-required requires --renderer gpu-compute --gpu-native-present\n");
        return -1;
    }
    if (o->gpu_scene_world_preview && (!o->renderer_mode || !o->gpu_required ||
            !o->gpu_native_present ||
            (o->gpu_frame_capture && !o->gpu_scene_independent_preview) ||
            o->gpu_character_vertex_diff ||
            o->gpu_rb0_stats || (!o->gpu_normal_view && !o->gpu_wave_repro && !o->gpu_scene_play))) {
        __fprintf(2,"rasterfall: Scene WORLD preview requires required native GPU and normal-scene/wave-repro; capture requires independent preview, and vertex diff and rb0 stats are unavailable\n");
        return -1;
    }
    if (o->gpu_frame_capture || o->gpu_capture_frame) {
        if (!o->gpu_frame_capture || (!o->gpu_normal_view && !o->gpu_wave_repro) || !o->renderer_mode ||
            !o->gpu_native_present || !o->gpu_required) {
            __fprintf(2,"rasterfall: GPU frame capture requires --gpu-normal-scene or --gpu-wave-repro, --renderer gpu-compute, --gpu-native-present and --gpu-required\n");
            return -1;
        }
        if (!o->gpu_capture_frame) o->gpu_capture_frame=30;
        if (!o->frame_limit) o->frame_limit=o->gpu_capture_frame;
        if (o->frame_limit < o->gpu_capture_frame) return -1;
    }
    if (o->gpu_normal_fixed_tick && !o->gpu_normal_view && !o->gpu_wave_repro) {
        __fprintf(2,"rasterfall: --gpu-normal-fixed-tick requires --gpu-normal-scene or --gpu-wave-repro\n");
        return -1;
    }
    if (o->gpu_character_vertex_diff &&
        (!o->gpu_normal_view || !o->renderer_mode || !o->gpu_native_present ||
         !o->gpu_required)) {
        __fprintf(2,"rasterfall: --gpu-character-vertex-diff requires --gpu-normal-scene, --renderer gpu-compute, --gpu-native-present and --gpu-required\n");
        return -1;
    }
    if (o->gpu_character_vertex_diff && !o->frame_limit) o->frame_limit=30;
    if (o->gpu_character_vertex_diff && o->frame_limit < 30) return -1;
    return 0;
}
