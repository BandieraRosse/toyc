/* toyasset v0.1: offline PNG/JPEG/WAV/OBJ conversion and asset validation. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zlib.h>
#include "jpg_decode.h"

static uint16_t g16(const unsigned char *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t g32(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint32_t be32(const unsigned char *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void p16(FILE*f,uint16_t v){fputc(v&255,f);fputc(v>>8,f);}
static void p32(FILE*f,uint32_t v){fputc(v&255,f);fputc((v>>8)&255,f);fputc((v>>16)&255,f);fputc(v>>24,f);}
static unsigned char *load(const char *path,size_t *n){FILE*f=fopen(path,"rb");long z;unsigned char*p;if(!f)return NULL;if(fseek(f,0,SEEK_END)|| (z=ftell(f))<0||z>64*1024*1024||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}p=malloc((size_t)z);if(!p||fread(p,1,(size_t)z,f)!=(size_t)z){free(p);fclose(f);return NULL;}fclose(f);*n=(size_t)z;return p;}

static int png_to_ttex(const char*in,const char*out,uint32_t target_w,uint32_t target_h){
 size_t n,pos=8,raw_n=0,cap=0;unsigned char*p=load(in,&n),*raw=NULL,*idat=NULL,*pix=NULL;uint32_t w=0,h=0;int bd=0,ct=0,inter=0;int rc=-1;
 if(!p||n<24||memcmp(p,"\x89PNG\r\n\x1a\n",8))goto done;
 while(pos+12<=n){uint32_t len;if(pos+12>n)goto done;len=be32(p+pos);if(len>n-pos-12)goto done;
  if(!memcmp(p+pos+4,"IHDR",4)&&len>=13){w=be32(p+pos+8);h=be32(p+pos+12);bd=p[pos+16];ct=p[pos+17];inter=p[pos+20];}
  else if(!memcmp(p+pos+4,"IDAT",4)){if(raw_n+len>cap){cap=(raw_n+len)*2+64;idat=realloc(idat,cap);if(!idat)goto done;}memcpy(idat+raw_n,p+pos+8,len);raw_n+=len;}
  else if(!memcmp(p+pos+4,"IEND",4)) break;
  pos+=len+12;
 }
 if(!w||!h||bd!=8||inter||!(ct==2||ct==6||ct==0||ct==4)||w>8192||h>8192||!idat)goto done;
 {int bpp=(ct==2?3:ct==6?4:ct==0?1:2),stride=w*bpp;uLongf z=(uLongf)(h*(stride+1));raw=malloc((size_t)z);if(!raw||uncompress(raw,&z,idat,raw_n)!=Z_OK||z!=h*(stride+1))goto done;pix=malloc((size_t)w*h*3);if(!pix)goto done;
  for(uint32_t y=0;y<h;y++){unsigned char*cur=raw+y*(stride+1)+1,*prev=y?raw+(y-1)*(stride+1)+1:NULL;int f=raw[y*(stride+1)];for(int x=0;x<stride;x++){int a=x>=bpp?cur[x-bpp]:0,b=prev?prev[x]:0,c=prev&&x>=bpp?prev[x-bpp]:0;switch(f){case 1:cur[x]+=a;break;case 2:cur[x]+=b;break;case 3:cur[x]+=(unsigned char)((a+b)/2);break;case 4:{int q=a+b-c,pa=abs(q-a),pb=abs(q-b),pc=abs(q-c);cur[x]+=(unsigned char)(pa<=pb&&pa<=pc?a:pb<=pc?b:c);break;}case 0:break;default:goto done;}}for(uint32_t x=0;x<w;x++){unsigned char*r=cur+x*bpp;pix[(y*w+x)*3]=r[0];pix[(y*w+x)*3+1]=ct==0?r[0]:ct==4?r[0]:r[1];pix[(y*w+x)*3+2]=ct==0?r[0]:ct==4?r[0]:r[2];}}
 }
 {uint32_t ow=target_w?target_w:w,oh=target_h?target_h:h;unsigned char*outpix=NULL;FILE*f;
  if(!ow||!oh||ow>8192||oh>8192)goto done;
  if(ow!=w||oh!=h){outpix=malloc((size_t)ow*oh*3);if(!outpix)goto done;
   for(uint32_t y=0;y<oh;y++)for(uint32_t x=0;x<ow;x++){uint32_t sx=x*w/ow,sy=y*h/oh;memcpy(outpix+(y*ow+x)*3,pix+(sy*w+sx)*3,3);}
  }else outpix=pix;
  f=fopen(out,"wb");if(!f){if(outpix!=pix)free(outpix);goto done;}fwrite("TTEX",1,4,f);p16(f,1);p16(f,32);p32(f,ow);p32(f,oh);p16(f,3);p16(f,1);p32(f,32);p32(f,ow*oh*3);p32(f,0);fwrite(outpix,1,(size_t)ow*oh*3,f);fclose(f);if(outpix!=pix)free(outpix);rc=0;}
done: free(p);free(idat);free(raw);free(pix);return rc;
}

static int jpg_to_ttex(const char*in,const char*out,uint32_t target_w,uint32_t target_h){size_t n;unsigned char*p=load(in,&n),*pix=NULL,*outpix=NULL;uint32_t w=0,h=0,ow,oh;int rc=-1;if(!p)goto done;if(jpg_to_rgb(p,n,&pix,&w,&h)<0||!pix)goto done;ow=target_w?target_w:w;oh=target_h?target_h:h;if(!ow||!oh||ow>8192||oh>8192)goto done;if(ow!=w||oh!=h){outpix=malloc((size_t)ow*oh*3);if(!outpix)goto done;for(uint32_t y=0;y<oh;y++)for(uint32_t x=0;x<ow;x++){uint32_t sx=x*w/ow,sy=y*h/oh;memcpy(outpix+(y*ow+x)*3,pix+(sy*w+sx)*3,3);}}else outpix=pix;{FILE*f=fopen(out,"wb");if(!f){if(outpix!=pix)free(outpix);goto done;}fwrite("TTEX",1,4,f);p16(f,1);p16(f,32);p32(f,ow);p32(f,oh);p16(f,3);p16(f,1);p32(f,32);p32(f,ow*oh*3);p32(f,0);fwrite(outpix,1,(size_t)ow*oh*3,f);fclose(f);if(outpix!=pix)free(outpix);rc=0;}done:free(p);free(pix);return rc;}

static int wav_to_tsnd(const char*in,const char*out){size_t n;unsigned char*p=load(in,&n),*d=NULL;size_t dn=0,pos=12;uint16_t fmt=0,ch=0,bits=0;uint32_t rate=0;int rc=-1;if(!p||n<12||memcmp(p,"RIFF",4)||memcmp(p+8,"WAVE",4))goto done;while(pos+8<=n){uint32_t z=g32(p+pos+4);if(z>n-pos-8)goto done;if(!memcmp(p+pos,"fmt ",4)&&z>=16){fmt=g16(p+pos+8);ch=g16(p+pos+10);rate=g32(p+pos+12);bits=g16(p+pos+22);}else if(!memcmp(p+pos,"data",4)){d=p+pos+8;dn=z;}pos+=8+z+(z&1);}if(fmt!=1||!d||!rate||ch<1||ch>2||bits!=16||dn%(ch*2))goto done;{FILE*f=fopen(out,"wb");if(!f)goto done;fwrite("TSND",1,4,f);p16(f,1);p16(f,32);p32(f,rate);p16(f,ch);p16(f,16);p32(f,(uint32_t)(dn/(ch*2)));p32(f,32);p32(f,(uint32_t)dn);p32(f,0);fwrite(d,1,dn,f);fclose(f);rc=0;}done:free(p);return rc;}

struct V{int32_t x,y,z;uint32_t color;};
static int obj_to_tmesh(const char*in,const char*out){FILE*f=fopen(in,"r");char line[512],*s;struct V*vs=NULL;uint32_t vn=0,vc=0,*ix=NULL,in_=0,ic=0;int rc=-1;if(!f)return -1;while(fgets(line,sizeof(line),f)){s=line;while(*s==' '||*s=='\t')s++;if(s[0]=='v'&&s[1]==' '){long x,y,z;if(sscanf(s+2,"%ld%ld%ld",&x,&y,&z)!=3||vn==vc){if(vn==vc){vc=vc?vc*2:16;vs=realloc(vs,vc*sizeof(*vs));}if(!vs)goto done;}vs[vn++]=(struct V){(int32_t)x,(int32_t)y,(int32_t)z,0xffffffffu};}else if(s[0]=='f'&&s[1]==' '){unsigned a,b,c;if(sscanf(s+2,"%u/%*[^ ] %u/%*[^ ] %u",&a,&b,&c)!=3&&sscanf(s+2,"%u %u %u",&a,&b,&c)!=3)goto done;if(!a||!b||!c||a>vn||b>vn||c>vn)goto done;if(in_+3>ic){ic=ic?ic*2:24;ix=realloc(ix,ic*sizeof(*ix));}if(!ix)goto done;ix[in_++]=a-1;ix[in_++]=b-1;ix[in_++]=c-1;}}
 if(!vn||!in_) goto done;
 {FILE*o=fopen(out,"wb");if(!o)goto done;fwrite("TMES",1,4,o);p16(o,1);p16(o,40);p32(o,vn);p32(o,in_);p32(o,16);p32(o,4);p32(o,40);p32(o,40+vn*16);p32(o,0);p32(o,0);for(uint32_t i=0;i<vn;i++){p32(o,(uint32_t)vs[i].x);p32(o,(uint32_t)vs[i].y);p32(o,(uint32_t)vs[i].z);p32(o,vs[i].color);}for(uint32_t i=0;i<in_;i++)p32(o,ix[i]);fclose(o);rc=0;}
done:fclose(f);free(vs);free(ix);return rc;}

static int validate(const char*path){size_t n;unsigned char*p=load(path,&n);int ok=0;if(!p||n<32)goto done;if(!memcmp(p,"TTEX",4)&&g16(p+4)==1&&g16(p+16)==3&&g16(p+18)==1&&g32(p+20)==32&&g32(p+24)==n-32)ok=1;else if(!memcmp(p,"TSND",4)&&g16(p+4)==1&&g16(p+14)==16&&g32(p+20)==32&&g32(p+24)==n-32&&g32(p+16)<=0xffffffffu/(g16(p+12)*2u))ok=1;else if(!memcmp(p,"TMES",4)&&g16(p+4)==1&&g32(p+16)==16&&g32(p+20)==4&&g32(p+24)==40&&g32(p+28)>=40+g32(p+8)*16&&g32(p+12)%3==0)ok=1;done:free(p);return ok?0:-1;}
static int inspect(const char*path){size_t n;unsigned char*p=load(path,&n);if(!p)return -1;printf("%s: %zu bytes, ",path,n);if(!memcmp(p,"TTEX",4)&&n>=32)printf("TTEX v%u %ux%u RGB888\n",g16(p+4),g32(p+8),g32(p+12));else if(!memcmp(p,"TSND",4)&&n>=32)printf("TSND v%u %u Hz %u ch %u frames\n",g16(p+4),g32(p+8),g16(p+12),g32(p+16));else if(!memcmp(p,"TMES",4)&&n>=40)printf("TMES v%u %u vertices %u indices\n",g16(p+4),g32(p+8),g32(p+12));else{free(p);return -1;}free(p);return 0;}
static void usage(void){fprintf(stderr,"usage: toyasset convert <png|jpg|jpg128|wav|obj> input output | inspect file | validate file\n");}
int main(int argc,char**argv){if(argc==3&&!strcmp(argv[1],"inspect"))return inspect(argv[2]);if(argc==3&&!strcmp(argv[1],"validate"))return validate(argv[2]);if(argc==5&&!strcmp(argv[1],"convert")){if(!strcmp(argv[2],"png"))return png_to_ttex(argv[3],argv[4],0,0)?1:0;if(!strcmp(argv[2],"jpg"))return jpg_to_ttex(argv[3],argv[4],0,0)?1:0;if(!strcmp(argv[2],"jpg128"))return jpg_to_ttex(argv[3],argv[4],128,128)?1:0;if(!strcmp(argv[2],"wav"))return wav_to_tsnd(argv[3],argv[4])?1:0;if(!strcmp(argv[2],"obj"))return obj_to_tmesh(argv[3],argv[4])?1:0;}usage();return 2;}
