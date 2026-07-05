// EXPECT: 0
// tcc.c standalone test — stub pipeline, verify orchestration logic
//
// gcc -nostdlib -ffreestanding -O0 -Wall -Wextra -Wl,-e,__tlibc_start \
//     compiler-tests/source/tcc.c -o /tmp/test_tcc && /tmp/test_tcc

typedef unsigned long size_t; typedef long off_t;
#define NULL ((void*)0)

static long sw(int f,const void*b,size_t l){unsigned long r;__asm__("syscall":"=a"(r):"a"(1),"D"((long)f),"S"(b),"d"((long)l):"rcx","r11","memory");return r;}
static void se(int c){__asm__("syscall"::"a"((long)60),"D"((long)c):"rcx","r11","memory");for(;;);}

typedef struct AstNode{int kind;struct AstNode*next;const char*name;void*body,*expr;long ival;int type_size;}AstNode;

unsigned char code_buf[262144]; int code_size;
typedef struct{const char*n;int o,s,g,f,sh,si;}CgenSym; CgenSym syms[8192]; int sym_count;
typedef struct{int a,b,c;}Elf64_Rela; Elf64_Rela rels[16384]; int rel_count;
int elf_bss_size;

void*tlibc_malloc(size_t s){static char h[4*1024*1024];static unsigned long u;unsigned long a=(s+7)&~7UL;if(u+a>sizeof(h)){se(1);}void*p=h+u;u+=a;return p;}
void tlibc_free(void*p){(void)p;}
int __openat(int d,const char*p,int f,int m){(void)d;(void)p;(void)f;(void)m;return 3;}
long __read(int fd,void*b,size_t c){(void)fd;const char*d="int x;";int n=0;while(d[n]&&n<(int)c){((char*)b)[n]=d[n];n++;}return n;}
long __lseek(int fd,off_t o,int w){(void)fd;(void)o;(void)w;return 6;}
int __close(int fd){(void)fd;return 0;}
long __write(int f,const void*b,size_t l){return sw(f,b,l);}
void add_include_path(const char*p){(void)p;}
int preprocess_called=0;const char*preprocess_ret="";
char*preprocess(const char*s,int l,const char*f,int*o){preprocess_called=1;(void)s;(void)l;(void)f;if(!preprocess_ret){*o=0;return 0;}int n=0;while(preprocess_ret[n])n++;*o=n;char*p=tlibc_malloc(n+1);int i;for(i=0;i<n;i++)p[i]=preprocess_ret[i];return p;}
void __printf(const char*f,...){(void)f;}
void lexer_init(){}void parser_init(){}
AstNode*parse_program(void*pa){static AstNode p;p.kind=0;return &p;}
int cgen_init_called=0;void cgen_init(){cgen_init_called=1;}
int cgen_program_called=0;void cgen_program(AstNode*p){(void)p;cgen_program_called=1;}
int elf_write_called=0;int elf_write_ret=0;
int elf_write_object(const char*p){elf_write_called=1;(void)p;return elf_write_ret;}

static char*read_file(const char*path,int*len){
    int fd=__openat(-100,path,0,0);if(fd<0)return 0;
    long sz=__lseek(fd,0,2);__lseek(fd,0,0);
    if(sz<=0||sz>1048576){__close(fd);return 0;}
    char*b=tlibc_malloc(sz+2);int n=__read(fd,b,sz);__close(fd);
    if(n!=sz){tlibc_free(b);return 0;}b[sz]=0;*len=sz;return b;
}
static void make_output_path(const char*input,const char*output,char*buf,int bs){
    int i,j;if(output){for(i=0;output[i]&&i<bs-1;i++)buf[i]=output[i];buf[i]=0;return;}
    for(i=0;input[i]&&i<bs-3;i++)buf[i]=input[i];buf[i]=0;
    if(i>=2&&buf[i-2]=='.'&&buf[i-1]=='c')buf[i-2]=0;
    for(j=0;buf[j];j++);buf[j]='.';buf[j+1]='o';buf[j+2]=0;
}
int tcc_main(int argc,char*argv[]){
    const char*ip=0,*op=0;int debug=0,i;
    for(i=1;i<argc;i++){
        if(argv[i][0]=='-'){
            if(argv[i][1]=='o'&&argv[i][2]==0&&i+1<argc)op=argv[++i];
            else if(argv[i][1]=='-'&&argv[i][2]==0);
            else if(argv[i][1]=='d')debug=1;
        }else ip=argv[i];
    }
    if(!ip)return 1;
    add_include_path(".");add_include_path("./include");add_include_path("./include/posix");
    add_include_path("./include/tlibc");add_include_path("./arch");add_include_path("./arch/x86_64");
    int sl;char*s=read_file(ip,&sl);if(!s)return 1;
    int pl;char*pp=preprocess(s,sl,ip,&pl);tlibc_free(s);
    if(!pp)return 1;
    if(debug){int pi,ln=1;for(pi=0;pi<pl&&ln<=300;pi++){if(pp[pi]=='\n')ln++;}tlibc_free(pp);return 0;}
    void*arena=tlibc_malloc(16+65536);lexer_init();parser_init();
    AstNode*prog=parse_program(0);if(!prog){tlibc_free(pp);tlibc_free(arena);return 1;}
    cgen_init();cgen_program(prog);
    char opath[512];make_output_path(ip,op,opath,512);
    if(elf_write_object(opath)!=0){tlibc_free(pp);tlibc_free(arena);return 1;}
    tlibc_free(pp);tlibc_free(arena);return 0;
}

// === Tests ===
static int tp=0,tf=0;
#define P(S) do{int n=0;while((S)[n])n++;sw(1,(S),n);}while(0)
#define CK(c,m) do{if(!(c)){P("  FAIL ");P(m);P("\n");tf++;}else tp++;}while(0)
#define SEC(N) do{P("\n--- ");P(N);P(" ---\n");tp=0;tf=0;}while(0)
#define PR do{P("  -> ");}while(0)
#define PR2 do{char b[32];int i=0,n=tp;if(n==0)b[i++]='0';while(n>0){b[i++]='0'+(n%10);n/=10;}while(i>0){char c=b[--i];sw(1,&c,1);}P(" passed, ");i=0;n=tf;if(n==0)b[i++]='0';while(n>0){b[i++]='0'+(n%10);n/=10;}while(i>0){char c=b[--i];sw(1,&c,1);}P(" failed\n");}while(0)
#define RST preprocess_ret="";elf_write_ret=0;cgen_init_called=0;cgen_program_called=0;elf_write_called=0

void __tlibc_start(void){
    P("=== tcc.c standalone tests ===\n");
    int tt=0,tf2=0;

    SEC("make_output_path");{char b[64];
    make_output_path("foo.c",0,b,64);CK(b[0]=='f'&&b[3]=='.',"foo.c→foo.o");
    make_output_path("dir/f.c",0,b,64);CK(b[0]=='d'&&b[5]=='.',"dir/f.c→dir/f.o");
    make_output_path("foo",0,b,64);CK(b[0]=='f'&&b[3]=='.',"foo→foo.o");
    make_output_path("x.c","out.o",b,64);CK(b[0]=='o',"-o out.o");}
    PR;PR2;tt+=tp;tf2+=tf;

    SEC("tcc_main: no args");RST;
    {char*a[]={"tcc",0};int r=tcc_main(1,a);CK(r==1,"no args→1");}
    PR;PR2;tt+=tp;tf2+=tf;

    SEC("tcc_main: basic");RST;
    {char*a[]={"tcc","in.c",0};int r=tcc_main(2,a);CK(r==0,"basic");}
    CK(cgen_init_called==1,"cgen_init");CK(cgen_program_called==1,"cgen_program");
    CK(elf_write_called==1,"elf_write");PR;PR2;tt+=tp;tf2+=tf;

    SEC("tcc_main: -d");RST;
    {char*a[]={"tcc","-d","in.c",0};int r=tcc_main(3,a);CK(r==0,"-d");}
    RST;{char*a[]={"tcc","--debug","in.c",0};int r=tcc_main(3,a);CK(r==0,"--debug");}
    PR;PR2;tt+=tp;tf2+=tf;

    SEC("tcc_main: errors");RST;
    preprocess_ret=0;{char*a[]={"tcc","in.c",0};int r=tcc_main(2,a);CK(r==1,"pp fail");}
    RST;elf_write_ret=-1;{char*a[]={"tcc","in.c",0};int r=tcc_main(2,a);CK(r==1,"elf fail");}
    PR;PR2;tt+=tp;tf2+=tf;

    P("\n=== ");P(tf2==0?"ALL PASSED":"SOME FAILED");P(" ===\n");
    se(tf2!=0?1:0);
}
