#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "rasterfall_model.h"
#include "rasterfall_vmd.h"

static unsigned int u32(const unsigned char *p){return p[0]|p[1]<<8|p[2]<<16|p[3]<<24;}
static float f32(const unsigned char *p){union{unsigned int u;float f;}v;v.u=u32(p);return v.f;}

/* Deterministic CP932 decoding for the MMD vocabulary used by this spike.
 * Unknown names remain printable as '?' and are reported as missing. */
struct name_map{const char *hex;const char *utf8;};
static const struct name_map names[]={
 {"90c3e68d82cc836e83548393","静謐のハサン"},{"834f838b815b8375","グルーブ"},{"835a8393835e815b","センター"},{"8ef1","首"},{"894591ab8268826a","右足ＩＫ"},{"8db691ab8268826a","左足ＩＫ"},
 {"89459872","右腕"},{"8db69872","左腕"},{"8db698729d80","左腕捩"},{"8db68ee89d80","左手捩"},{"8db68ee88ef1","左手首"},
 {"894598729d80","右腕捩"},{"89458ee89d80","右手捩"},{"89458ee88ef1","右手首"},{"8db68ca8","左肩"},{"89458ca8","右肩"},
 {"8fe394bc9067","上半身"},{"8fe394bc906732","上半身2"},{"93aa","頭"},{"89ba94bc9067","下半身"},
 {"8db682d082b6","左ひじ"},{"894582d082b6","右ひじ"},{"8db691ab","左足"},{"894591ab","右足"},
 {"8db68fac8e778250","左小指１"},{"8db68fac8e778251","左小指２"},{"8db68fac8e778252","左小指３"},
 {"8db696f28e778250","左薬指１"},{"8db696f28e778251","左薬指２"},{"8db696f28e778252","左薬指３"},
 {"8db692868e778250","左中指１"},{"8db692868e778251","左中指２"},{"8db692868e778252","左中指３"},
 {"8db6906c8e778250","左人指１"},{"8db6906c8e778251","左人指２"},{"8db6906c8e778252","左人指３"},
 {"8db690658e77824f","左親指０"},{"8db690658e778250","左親指１"},{"8db690658e778251","左親指２"},
 {"89458fac8e778250","右小指１"},{"89458fac8e778251","右小指２"},{"89458fac8e778252","右小指３"},
 {"894596f28e778250","右薬指１"},{"894596f28e778251","右薬指２"},{"894596f28e778252","右薬指３"},
 {"894592868e778250","右中指１"},{"894592868e778251","右中指２"},{"894592868e778252","右中指３"},
 {"8945906c8e778250","右人指１"},{"8945906c8e778251","右人指２"},{"8945906c8e778252","右人指３"},
 {"894590658e77824f","右親指０"},{"894590658e778250","右親指１"},{"894590658e778251","右親指２"},
 {"8db682c882eb","左ひざ"},{"894582c882eb","右ひざ"},{"8db691ab82c982c1","左足首"},{"894591ab82c982c1","右足首"}
};
static int hexval(unsigned char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static int decode_name(const unsigned char *raw,char *out){
 int i,j,n=0,bytes=0; unsigned char hex[32];
 while(bytes<15&&raw[bytes]){hex[bytes]=raw[bytes];bytes++;}
 for(i=0;i<(int)(sizeof(names)/sizeof(names[0]));i++){
  int len=(int)strlen(names[i].hex)/2,ok=len==bytes;
  for(j=0;ok&&j<len;j++)if(hexval(names[i].hex[j*2])<<4|hexval(names[i].hex[j*2+1])){}
  if(ok){for(j=0;j<len;j++){int a=hexval(names[i].hex[j*2]),b=hexval(names[i].hex[j*2+1]);if(hex[j]!=(unsigned char)(a*16+b)){ok=0;break;}}}
  if(ok){strcpy(out,names[i].utf8);return 0;}
 }
 for(i=0;i<bytes&&n<RASTERFALL_VMD_MAX_NAME-1;i++)out[n++]=(raw[i]>=32&&raw[i]<128)?raw[i]:'?';out[n]=0;return 1;
}
static int track_find(const struct rasterfall_vmd_clip*c,const char*n){int i;for(i=0;i<c->track_count;i++)if(!strcmp(c->tracks[i].name,n))return i;return -1;}
static int ms(int frame){return frame*1000/30;}

int rasterfall_vmd_load(struct rasterfall_vmd_clip*c,const char*path){
 int fd,size,got=0,n,i,j,t;unsigned char*b;struct stat st;
 if(!c||!path)return -1;memset(c,0,sizeof(*c));fd=__openat(AT_FDCWD,path,O_RDONLY,0);
 if(fd<0||__fstat(fd,&st)<0||st.st_size<54||st.st_size>64*1024*1024){if(fd>=0)__close(fd);return -1;}
 size=(int)st.st_size;b=tlibc_malloc(size);if(!b){__close(fd);return -1;}while(got<size&&(n=__read(fd,b+got,size-got))>0)got+=n;__close(fd);
 if(got!=size||memcmp(b,"Vocaloid Motion Data 0002",24)!=0||54+(long)u32(b+50)*111>size){tlibc_free(b);return -1;}
 c->version=2;decode_name(b+30,c->model_name);c->motion_count=(int)u32(b+50);c->tracks=tlibc_malloc(RASTERFALL_VMD_MAX_BONES*sizeof(*c->tracks));if(!c->tracks){tlibc_free(b);return -1;}memset(c->tracks,0,RASTERFALL_VMD_MAX_BONES*sizeof(*c->tracks));
 /* Pass 1: names and per-track counts. */
 for(i=0;i<c->motion_count;i++){char name[RASTERFALL_VMD_MAX_NAME];const unsigned char*p=b+54+i*111;decode_name(p,name);t=track_find(c,name);if(t<0){if(c->track_count==RASTERFALL_VMD_MAX_BONES){tlibc_free(b);rasterfall_vmd_unload(c);return -1;}t=c->track_count++;strcpy(c->tracks[t].name,name);c->tracks[t].first_frame=0x7fffffff;c->tracks[t].target_bone=-1;c->tracks[t].is_ik=strstr(name,"ＩＫ")!=0;c->tracks[t].is_center=!strcmp(name,"センター");c->tracks[t].is_groove=!strcmp(name,"グルーブ");if(c->tracks[t].is_ik)c->ignored_ik_tracks++;}c->tracks[t].key_count++;}
 for(i=0;i<c->track_count;i++){c->tracks[i].keys=tlibc_malloc(c->tracks[i].key_count*sizeof(*c->tracks[i].keys));c->tracks[i].animation_keys=tlibc_malloc(c->tracks[i].key_count*sizeof(*c->tracks[i].animation_keys));if(!c->tracks[i].keys||!c->tracks[i].animation_keys){tlibc_free(b);rasterfall_vmd_unload(c);return -1;}c->tracks[i].key_count=0;}
 for(i=0;i<c->motion_count;i++){const unsigned char*p=b+54+i*111;char name[RASTERFALL_VMD_MAX_NAME];struct rasterfall_vmd_keyframe*k;int f;decode_name(p,name);t=track_find(c,name);k=&c->tracks[t].keys[c->tracks[t].key_count++];f=(int)u32(p+15);k->frame=f;k->tx=f32(p+19);k->ty=f32(p+23);k->tz=f32(p+27);k->rotation.x=f32(p+31);k->rotation.y=f32(p+35);k->rotation.z=f32(p+39);k->rotation.w=f32(p+43);k->rotation=rasterfall_animation_quat_normalize(k->rotation);memcpy(k->interpolation,p+47,64);if(f>c->max_frame)c->max_frame=f;if(f<c->tracks[t].first_frame)c->tracks[t].first_frame=f;if(f>c->tracks[t].last_frame)c->tracks[t].last_frame=f;if(k->tx||k->ty||k->tz)c->tracks[t].translation_changed=1;if(k->rotation.x||k->rotation.y||k->rotation.z||k->rotation.w<0.999f)c->tracks[t].rotation_changed=1;if(memcmp(k->interpolation,(unsigned char[64]){0},64))c->interpolation_tracks=1;}
 /* VMD files are not required to be ordered by bone/frame. */
 for(i=0;i<c->track_count;i++)for(j=1;j<c->tracks[i].key_count;j++){struct rasterfall_vmd_keyframe x=c->tracks[i].keys[j];int k=j-1;while(k>=0&&c->tracks[i].keys[k].frame>x.frame){c->tracks[i].keys[k+1]=c->tracks[i].keys[k];k--;}c->tracks[i].keys[k+1]=x;}
 for(i=0;i<c->track_count;i++)for(j=0;j<c->tracks[i].key_count;j++){c->tracks[i].animation_keys[j].time_ms=ms(c->tracks[i].keys[j].frame);c->tracks[i].animation_keys[j].rotation=c->tracks[i].keys[j].rotation;}
 c->duration_ms=ms(c->max_frame);tlibc_free(b);return 0;
}
void rasterfall_vmd_unload(struct rasterfall_vmd_clip*c){int i;if(!c)return;for(i=0;i<c->track_count;i++){if(c->tracks[i].keys)tlibc_free(c->tracks[i].keys);if(c->tracks[i].animation_keys)tlibc_free(c->tracks[i].animation_keys);}if(c->tracks)tlibc_free(c->tracks);memset(c,0,sizeof(*c));}
int rasterfall_vmd_map_eula(struct rasterfall_vmd_clip*c,const struct rasterfall_model_asset*a){int i,j,m=0;if(!c||!a)return -1;for(i=0;i<c->track_count;i++){struct rasterfall_vmd_bone_track*t=&c->tracks[i];t->target_bone=rasterfall_model_find_bone(a,t->name);t->mapping_status=t->target_bone<0?0:t->is_ik?2:1;for(j=0;j<i&&t->target_bone>=0;j++)if(c->tracks[j].target_bone==t->target_bone)t->mapping_status=3;if(t->mapping_status==1)m++;}return m;}
void rasterfall_vmd_dump(const struct rasterfall_vmd_clip*c,const struct rasterfall_model_asset*a){int i,m=0,d=0,ig=0,dup=0;if(!c)return;__printf("vmd: header=Vocaloid Motion Data 0002 version=%d model=\"%s\" bone_motions=%d unique_bones=%d max_frame=%d duration_ms=%d (30fps)\n",c->version,c->model_name,c->motion_count,c->track_count,c->max_frame,c->duration_ms);__printf("vmd: interpolation=%s runtime=LINEAR/NLERP IK tracks=%d status=IK tracks detected but ignored in v1\n",c->interpolation_tracks?"present":"not detected",c->ignored_ik_tracks);for(i=0;i<c->track_count;i++){const struct rasterfall_vmd_bone_track*t=&c->tracks[i];const char*s=t->mapping_status==1?"exact":t->mapping_status==2?"ignored":t->mapping_status==3?"duplicate":"missing";if(t->mapping_status==1)m++;if(t->mapping_status==0)d++;if(t->mapping_status==2)ig++;if(t->mapping_status==3)dup++;__printf("  %-16s keys=%-3d frames=%d-%d translation=%s rotation=%s",t->name,t->key_count,t->first_frame,t->last_frame,t->translation_changed?"changes":"static",t->rotation_changed?"changes":"static");if(a)__printf(" -> %-16s [%s]",t->target_bone>=0?a->bones[t->target_bone].name:"-",s);__printf("\n");}if(a)__printf("mapping: mapped=%d missing=%d ignored=%d duplicate=%d coverage=%d/%d\n",m,d,ig,dup,m,c->track_count);}
int rasterfall_vmd_build_animation(const struct rasterfall_vmd_clip*v,struct rasterfall_animation_clip*c,struct rasterfall_animation_track*t,int cap){int i,n=0;if(!v||!c||!t)return-1;for(i=0;i<v->track_count&&n<cap;i++)if(v->tracks[i].target_bone>=0&&!v->tracks[i].is_ik){t[n].target_bone=v->tracks[i].target_bone;t[n].keys=v->tracks[i].animation_keys;t[n].key_count=v->tracks[i].key_count;n++;}c->duration_ms=v->duration_ms;c->loop=1;c->tracks=t;c->track_count=n;return n;}
void rasterfall_vmd_sample_translation(const struct rasterfall_vmd_clip*v,int time_ms,int out[3]){int i,j;double t=0;float x=0,y=0,z=0;if(!out)return;out[0]=out[1]=out[2]=0;if(!v)return;if(v->duration_ms>0)time_ms%=v->duration_ms;for(i=0;i<v->track_count;i++)if(v->tracks[i].is_center||v->tracks[i].is_groove){const struct rasterfall_vmd_bone_track*q=&v->tracks[i];const struct rasterfall_vmd_keyframe*a=&q->keys[0],*b=a;for(j=1;j<q->key_count;j++){if(time_ms<q->keys[j].frame*1000/30){b=&q->keys[j];break;}a=&q->keys[j];}if(b!=a&&b->frame>a->frame)t=(time_ms-a->frame*1000/30)/(double)((b->frame-a->frame)*1000/30);x+=(float)(a->tx+(b->tx-a->tx)*t);y+=(float)(a->ty+(b->ty-a->ty)*t);z+=(float)(a->tz+(b->tz-a->tz)*t);}out[0]=(int)(x*232.0f);out[1]=(int)(y*232.0f);out[2]=(int)(z*232.0f);}
int rasterfall_vmd_logic_test(void){struct rasterfall_animation_quaternion q={1,2,3,4};q=rasterfall_animation_quat_normalize(q);return q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w<.999||q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w>1.001;}
