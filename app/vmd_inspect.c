#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_model.h"
#include "rasterfall_vmd.h"

int main(int argc,char**argv){struct rasterfall_vmd_clip v;struct rasterfall_model_asset m;int have=0,result,ordered=1,normalized=1,i,j;double norm;
 if(argc<2){__printf("usage: vmd-inspect <motion.vmd> [eula.rmesh]\n");return 2;}
 if(rasterfall_vmd_load(&v,argv[1])<0){__fprintf(2,"vmd-inspect: malformed or truncated VMD\n");return 1;}
 memset(&m,0,sizeof(m));if(argc>2){if(rasterfall_model_load(&m,argv[2])<0){rasterfall_vmd_unload(&v);return 1;}have=1;rasterfall_vmd_map_eula(&v,&m);}
 rasterfall_vmd_dump(&v,have?&m:0);
 for(i=0;i<v.track_count;i++)for(j=1;j<v.tracks[i].key_count;j++){if(v.tracks[i].keys[j].frame<v.tracks[i].keys[j-1].frame)ordered=0;norm=v.tracks[i].keys[j].rotation.x*v.tracks[i].keys[j].rotation.x+v.tracks[i].keys[j].rotation.y*v.tracks[i].keys[j].rotation.y+v.tracks[i].keys[j].rotation.z*v.tracks[i].keys[j].rotation.z+v.tracks[i].keys[j].rotation.w*v.tracks[i].keys[j].rotation.w;if(norm<.999||norm>1.001)normalized=0;}
 result=rasterfall_vmd_logic_test()||!ordered||!normalized||v.duration_ms!=v.max_frame*1000/30;__printf("vmd validation: frame_order=%s quaternion_normalization=%s duration=%s loop_sampling=existing AnimationClip nlerp\n",ordered?"pass":"FAIL",normalized?"pass":"FAIL",v.duration_ms==v.max_frame*1000/30?"pass":"FAIL");if(have)rasterfall_model_unload(&m);rasterfall_vmd_unload(&v);return result;
}
