#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "rasterfall_net.h"
#include "rasterfall_model.h"
#include "rasterfall_options.h"

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
    o->edge_pass_enabled = 1;
    o->stats_enabled = 1;
    o->model_views_supersample = 1;
    o->model_skinning = -1;
    o->model_pose = RASTERFALL_MODEL_POSE_BIND;
    o->performance_iterations = 5;
    o->actor_raster_workers = 8;
}

void rasterfall_options_usage(int fd)
{
    __fprintf(fd,
        "usage: rasterfall [runtime options]\n"
        "  --host | --connect <ip> [--port <port>] [--net-loss <percent>]\n"
        "  --textures | --no-textures  --no-edge-pass  --no-stats\n"
        "  --texture-stats  --frames <count>  --dump-frame <path>\n"
        "  --logic-test  --input-test  --auto\n"
        "  --model-views <model> <dir> [--model-views-supersample <1|2>]\n"
        "  --model-static-views <model> <dir>\n"
        "  --model-pose-views <model> <dir> <bind|right-arm|arms|body>\n"
        "  --model-material-regression <model> <dir>\n"
        "  --model-performance <model> [iterations] [workers]\n"
        "  --actor-performance [iterations] [frontend-workers] [raster-workers]\n"
        "  --model-bones <model> [search]  --model-humanoid <model>\n"
        "  --model-humanoid-basis <model>\n"
        "  --model-retarget-test <model> <right-arm|left-arm|right-leg|chest>\n"
        "  --model-glb-animation <model> <glb> <clip>\n"
        "  --model-glb-motion-diagnostic <model> <glb>\n"
        "  --vmd-eula-walk <model> <vmd>\n"
        "  --vmd-freeze-head | --vmd-freeze-torso\n"
        "  --vmd-disable-ik | --vmd-disable-grant\n"
        "  --vmd-legacy-root-offset | --vmd-legacy-leg-ccd\n"
        "  --vmd-skin-trace\n");
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
        else if (!strcmp(option,"--textures")) o->textures_enabled=1;
        else if (!strcmp(option,"--no-textures")) o->textures_enabled=0;
        else if (!strcmp(option,"--edge-pass")) o->edge_pass_enabled=1;
        else if (!strcmp(option,"--no-edge-pass")) o->edge_pass_enabled=0;
        else if (!strcmp(option,"--no-stats")) o->stats_enabled=0;
        else if (!strcmp(option,"--texture-stats")) o->texture_stats=1;
        else if (!strcmp(option,"--dump-frame")) {
            if(require_arguments(argc,argv,arg,1,option)<0)return -1;
            o->dump_path=argv[++arg];
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
    return 0;
}
