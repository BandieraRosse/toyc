/* Portable, content-addressed lighting artifacts. All reads are bounded and
 * checked before installation; cache failure is a cold bake, never a load error. */
#define RF_BAKE_LIMIT (256u*1024u*1024u)
static uint64_t bake_hash(uint64_t h,const void *data,size_t bytes)
{
    const unsigned char *p=data;
    for(size_t i=0;i<bytes;++i)h=(h^p[i])*1099511628211ull;
    return h;
}
static uint64_t bake_seed(void) {return 14695981039346656037ull;}
static int bake_path(char *path,size_t size,uint64_t key,unsigned kind)
{
    const char *root=getenv("RF_GPU_BAKE_CACHE");
    if(root && !strcmp(root,"-"))return 0;
    if(!root || !*root)root="build";
    int n=snprintf(path,size,"%s/rf-light-%u-%016llx.bin",root,kind,(unsigned long long)key);
    return n>0 && (size_t)n<size;
}
static void *bake_read(uint64_t key,unsigned kind,size_t expected,size_t *bytes)
{
    char path[1024];uint64_t h[5];void *data=NULL;*bytes=0;
    if(!bake_path(path,sizeof(path),key,kind))return NULL;
    FILE *file=fopen(path,"rb");if(!file)return NULL;
    if(fread(h,sizeof(h),1,file)==1 && h[0]==0x31454b41424652ull && h[1]==key &&
       h[2]==kind && h[3]>=16 && h[3]<=RF_BAKE_LIMIT && (!expected || h[3]==expected)) {
        data=malloc((size_t)h[3]);
        if(data && fread(data,1,(size_t)h[3],file)==h[3] && fgetc(file)==EOF &&
           bake_hash(bake_seed(),data,(size_t)h[3])==h[4])*bytes=(size_t)h[3];
        else {free(data);data=NULL;}
    }
    fclose(file);return data;
}
static void bake_write(uint64_t key,unsigned kind,const void *data,size_t bytes)
{
    char path[1024],temporary[1100];
    if(bytes<16 || bytes>RF_BAKE_LIMIT || !bake_path(path,sizeof(path),key,kind))return;
    const char *root=getenv("RF_GPU_BAKE_CACHE");if(!root || !*root)root="build";
#if defined(_WIN32)
    CreateDirectoryA(root,NULL);
    unsigned long process=(unsigned long)GetCurrentProcessId();
#else
    mkdir(root,0700);
    unsigned long process=(unsigned long)getpid();
#endif
    snprintf(temporary,sizeof(temporary),"%s.%lu.tmp",path,process);
    uint64_t h[5]={0x31454b41424652ull,key,kind,bytes,bake_hash(bake_seed(),data,bytes)};
    FILE *file=fopen(temporary,"wb");int ok=0;
    if(file) {
        ok=fwrite(h,sizeof(h),1,file)==1 && fwrite(data,1,bytes,file)==bytes;
        if(fclose(file))ok=0;
        if(ok) {
#if defined(_WIN32)
            ok=MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING)!=0;
#else
            ok=rename(temporary,path)==0;
#endif
        }
        if(!ok)remove(temporary);
    }
    fprintf(stderr,"rf-gpu-bake: save kind=%u bytes=%llu result=%s\n",kind,(unsigned long long)bytes,ok?"ok":"unavailable");
}
