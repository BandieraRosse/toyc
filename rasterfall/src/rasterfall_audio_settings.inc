/* Included by the audio owner; standalone tests use the same implementation. */
void rf_audio_settings_default(struct rf_audio_settings *s)
{
    for(int i=0;i<RF_AUDIO_CONTROL_COUNT;++i)s->volume[i]=100;
    s->volume[RF_AUDIO_MASTER]=80;
    s->range=RF_AUDIO_STANDARD;
}
int rf_audio_volume_q8(int percent)
{
    if(percent<0)percent=0;
    if(percent>100)percent=100;
    return (percent*percent*256+5000)/10000;
}
const char *rf_audio_control_name(int control)
{
    static const char *names[]={"MASTER","MUSIC","GAME EFFECTS","WEAPONS / EXPLOSIONS",
        "OWN GUNSHOTS","ENVIRONMENT / MACHINES","COMBAT FEEDBACK","INTERFACE"};
    return control>=0 && control<RF_AUDIO_CONTROL_COUNT?names[control]:"UNKNOWN";
}
const char *rf_audio_range_name(int range)
{
    static const char *names[]={"WIDE","STANDARD","COMFORT"};
    return range>=0 && range<=RF_AUDIO_COMFORT?names[range]:"STANDARD";
}
static int rf_audio_settings_decode(const char *text,struct rf_audio_settings *s)
{
    if(strncmp(text,"RFAUDIO ",8))return -1;
    text+=8;
    int values[RF_AUDIO_CONTROL_COUNT+2];
    for(int i=0;i<RF_AUDIO_CONTROL_COUNT+2;++i) {
        while(*text==' ' || *text=='\r' || *text=='\n' || *text=='\t')++text;
        if(*text<'0' || *text>'9')return -1;
        int value=0;
        do {value=value*10+(*text++-'0');if(value>100)return -1;}
        while(*text>='0' && *text<='9');
        if(*text && *text!=' ' && *text!='\r' && *text!='\n' && *text!='\t')return -1;
        values[i]=value;
    }
    while(*text==' ' || *text=='\r' || *text=='\n' || *text=='\t')++text;
    if(*text || values[0]!=1 || values[1]>RF_AUDIO_COMFORT)return -1;
    s->range=values[1];
    for(int i=0;i<RF_AUDIO_CONTROL_COUNT;++i)s->volume[i]=values[i+2];
    return 0;
}
int rf_audio_settings_load(struct rf_audio_settings *s,const char *path)
{
    char text[192];int count=0;
    if(!s || !path || !*path)return -1;
    int fd=__openat(AT_FDCWD,path,O_RDONLY,0);
    if(fd<0)return -1;
    while(count<(int)sizeof(text)-1) {
        int n=(int)__read(fd,text+count,sizeof(text)-1-count);
        if(n<0){__close(fd);return -1;}
        if(!n)break;
        count+=n;
    }
    if(__close(fd)<0 || count==(int)sizeof(text)-1)return -1;
    text[count]=0;
    if((int)strlen(text)!=count)return -1;
    return rf_audio_settings_decode(text,s);
}
int rf_audio_settings_save(const struct rf_audio_settings *s,const char *path)
{
    char text[192],temp[512];struct rf_audio_settings check;
    if(!s || !path || !*path || strlen(path)>sizeof(temp)-6)return -1;
    int n=snprintf(text,sizeof(text),"RFAUDIO 1 %d %d %d %d %d %d %d %d %d\n",s->range,
        s->volume[0],s->volume[1],s->volume[2],s->volume[3],s->volume[4],
        s->volume[5],s->volume[6],s->volume[7]);
    if(n<0 || n>=(int)sizeof(text) || rf_audio_settings_decode(text,&check))return -1;
    snprintf(temp,sizeof(temp),"%s.tmp",path);
    int fd=__openat(AT_FDCWD,temp,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(fd<0)return -1;
    int written=0;
    while(written<n) {
        int count=(int)__write(fd,text+written,n-written);
        if(count<=0){__close(fd);return -1;}
        written+=count;
    }
    if(__close(fd)<0)return -1;
    return __rename(temp,path);
}
void rf_audio_spatial_gain(struct rf_audio_spatial *g,
    int dx,int dy,int dz,int sy,int cy,int near_distance,int far_distance)
{
    double x=dx,y=dy,z=dz,distance=sqrt(x*x+y*y+z*z);
    double gain=0.0;
    if(far_distance>near_distance && near_distance>=0 && distance<far_distance) {
        gain=distance<=near_distance?1.0:(far_distance-distance)/(far_distance-near_distance);
        gain*=gain;
    }
    double pan=distance>1.0?(x*cy-z*sy)/(1024.0*distance):0.0;
    if(pan>1.0)pan=1.0;
    if(pan< -1.0)pan=-1.0;
    g->left_q8=(int)(256.0*gain*(pan>0.0?1.0-.55*pan:1.0));
    g->right_q8=(int)(256.0*gain*(pan<0.0?1.0+.55*pan:1.0));
}
