#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_model.h"

int main(int argc,char **argv)
{
    struct rasterfall_model_asset m;struct rasterfall_model_attachment_transform a,b;
    unsigned int i;int p0[3],n0[3],p1[3],n1[3],moved=0,bdef1=0,bdef2=0;
    if(argc!=2){__printf("usage: rfchar-runtime-test character.rmesh\n");return 2;}
    if(rasterfall_model_load(&m,argv[1])<0||!m.has_character_contract)return 1;
    for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++)if(rasterfall_model_humanoid_bone(&m,(enum rasterfall_humanoid_bone)i)<0)return 2;
    for(i=0;i<m.vertex_count;i++){const unsigned char *w=m.skin_vertices+i*8;if(w[6])bdef2++;else bdef1++;}
    if(!bdef1||!bdef2){__printf("rfchar-runtime: missing weight class bdef1=%d bdef2=%d\n",bdef1,bdef2);return 3;}
    if(rasterfall_model_character_attachment_transform(&m,RASTERFALL_ATTACHMENT_WEAPON_R,&a)<0){__printf("rfchar-runtime: missing WEAPON_R attachment present=%d parent=%d\n",m.attachments[0].present,m.attachments[0].parent_bone);return 3;}
    for(i=0;i<m.vertex_count;i++){rasterfall_model_skin_vertex(&m,i,p0,n0);if(rasterfall_model_set_pose(&m,RASTERFALL_MODEL_POSE_RFCHAR_TEST)<0)return 4;rasterfall_model_skin_vertex(&m,i,p1,n1);if(p0[0]!=p1[0]||p0[1]!=p1[1]||p0[2]!=p1[2])moved++;rasterfall_model_set_pose(&m,RASTERFALL_MODEL_POSE_BIND);}
    rasterfall_model_set_pose(&m,RASTERFALL_MODEL_POSE_RFCHAR_TEST);
    if(!moved||rasterfall_model_character_attachment_transform(&m,RASTERFALL_ATTACHMENT_WEAPON_R,&b)<0||
       (a.position[0]==b.position[0]&&a.position[1]==b.position[1]&&a.position[2]==b.position[2]))return 5;
    __printf("rfchar-runtime: PASS version=%u vertices=%u bones=%u bdef1=%d bdef2=%d moved=%d bounds=(%d,%d,%d)-(%d,%d,%d) weapon_motion=(%.3f,%.3f,%.3f)\n",m.format_version,m.vertex_count,m.bone_count,bdef1,bdef2,moved,m.min_x,m.min_y,m.min_z,m.max_x,m.max_y,m.max_z,b.position[0]-a.position[0],b.position[1]-a.position[1],b.position[2]-a.position[2]);
    rasterfall_model_unload(&m);return 0;
}
