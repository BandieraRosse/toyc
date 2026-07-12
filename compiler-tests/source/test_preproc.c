/* Take the working pp_full and add skip_cond + full do_directive */
typedef unsigned long size_t;
static long __sys_write(int fd, const char *buf, unsigned long len) {
    unsigned long ret;
    __asm__ __volatile__ ("syscall" : "=a"(ret) : "a"(1), "D"((long)fd), "S"((long)buf), "d"((long)len) : "rcx", "r11", "memory");
    return (long)ret;
}
static void __sys_exit(int code) {
    __asm__ __volatile__ ("syscall" : : "a"((long)60), "D"((long)code) : "rcx", "r11", "memory");
    for (;;) ;
}
static void print_str(const char *s) { int n=0; while(s[n])n++; __sys_write(1,s,n); }
#define HEAP_SIZE (262144)
static char heap_buf[HEAP_SIZE];
static unsigned long heap_used;
void *tlibc_malloc(unsigned long s) { unsigned long a=(s+7)&~7UL; if(heap_used+a>HEAP_SIZE){print_str("OOM\n");__sys_exit(1);}void*p=heap_buf+heap_used;heap_used+=a;{unsigned long i;for(i=0;i<s;i++) ((char*)p)[i]=0;}return p;}
void tlibc_free(void*p){(void)p;}
typedef struct{char*data;int len;int cap;}OutBuf;
static void out_putc(OutBuf*b,char c){if(b->data==0){b->cap=1024;b->data=(char*)tlibc_malloc(1024);}else if(b->len>=b->cap){b->cap=b->cap?b->cap*2:1024;char*nd=(char*)tlibc_malloc(b->cap);int i;for(i=0;i<b->len;i++)nd[i]=b->data[i];tlibc_free(b->data);b->data=nd;}b->data[b->len++]=c;}
static int pp_id(char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'||(c>='0'&&c<='9');}
static int pp_ws(char c){return c==' '||c=='\t'||c=='\n'||c=='\r';}
#define MAX_MACROS 64
#define MAX_FUNC_MACROS 64
#define MAX_MACRO_PARAMS 16
#define MAX_EXPAND_STACK 64
typedef struct{const char*name;const char*value;int value_len;}Macro;static Macro macros[MAX_MACROS];static int macro_count;
static void add_macro(const char*n,const char*v,int vl){if(macro_count<MAX_MACROS){macros[macro_count].name=n;macros[macro_count].value=v;macros[macro_count].value_len=vl;macro_count++;}}
static void undef_macro(const char*name){int i;for(i=0;i<macro_count;i++){int j;for(j=0;name[j];j++)if(macros[i].name[j]!=name[j])goto nx;if(macros[i].name[j]=='\0'){macros[i]=macros[--macro_count];return;}nx:;}}
static int macro_defined(const char*name){int i;for(i=0;i<macro_count;i++){int j;for(j=0;name[j];j++)if(macros[i].name[j]!=name[j])goto nx2;if(macros[i].name[j]=='\0')return 1;nx2:;}return 0;}
typedef struct{const char*name;const char*params[MAX_MACRO_PARAMS];int param_count;int is_variadic;const char*replacement;int repl_len;}FuncMacro;static FuncMacro func_macros[MAX_FUNC_MACROS];static int func_macro_count;
static const char*expand_stack[MAX_EXPAND_STACK];static int expand_stack_depth;
static int is_expanding(const char*name){int i;for(i=0;i<expand_stack_depth;i++)if(expand_stack[i]==name)return 1;return 0;}
static int pp_cond_skip=0;static int pp_cond_depth=0;static int pp_cond_emit[32];
static void get_name(const char*s,int start,int end,char*buf,int bufsz){int i;int n=end-start;if(n>bufsz-1)n=bufsz-1;for(i=0;i<n;i++)buf[i]=s[start+i];buf[n]='\0';}

/* Global arrays for func macro arg/expanded storage — avoids tcc local-array codegen bug */
static const char *g_arg_starts[16];
static int g_arg_lens[16];
static int g_arg_count;
static char *g_expanded_args[16];
static int g_expanded_lens[16];

/* Per-level stack for recursive func macro handling */
#define PP_STACK_MAX 64
static const char *g_pp_arg_starts[64][16];
static int g_pp_arg_lens[64][16];
static int g_pp_arg_count[64];
static char *g_pp_expanded[64][16];
static int g_pp_expanded_lens[64][16];
static int g_pp_level;  /* current stack level (0 = none, 1+ = inside func macro) */

/* Copy current global arrays to current stack level */
static void pp_push_level(void) {
    int lv = g_pp_level;
    g_pp_arg_count[lv] = g_arg_count;
    { int si; for (si = 0; si < g_arg_count; si++) { g_pp_arg_starts[lv][si] = g_arg_starts[si]; g_pp_arg_lens[lv][si] = g_arg_lens[si]; } }
    { int si; for (si = 0; si < g_arg_count; si++) { g_pp_expanded[lv][si] = g_expanded_args[si]; g_pp_expanded_lens[lv][si] = g_expanded_lens[si]; } }
    g_pp_level++;
}


/* Flat 1D stack arrays for nested func macro support (avoids tcc 2D array bug) */
static char *g_pp_ex[1024];
static int g_pp_ex_len[1024];
static const char *g_pp_as[1024];
static int g_pp_al[1024];
static int g_pp_ac[64];
static int g_pp_lv;

static void pp_push(void) {
    int lv = g_pp_lv;
    int base = lv * 16;
    g_pp_ac[lv] = g_arg_count;
    { int si; for (si = 0; si < g_arg_count; si++) {
        g_pp_as[base + si] = g_arg_starts[si];
        g_pp_al[base + si] = g_arg_lens[si];
        g_pp_ex[base + si] = g_expanded_args[si];
        g_pp_ex_len[base + si] = g_expanded_lens[si];
    } }
    g_pp_lv++;
}

/* Copy skip_cond from source test */
static void skip_cond(const char *s, int *pos, int len) {
    int d = 0;
    while (*pos < len) {
        int ls = *pos; while (*pos < len && s[*pos] != '\n') (*pos)++;
        if (*pos < len) (*pos)++;
        int j; int hh = 0;
        for (j = ls; j < *pos && j < len; j++) { if (s[j] == '#') { hh = 1; break; } if (!pp_ws(s[j])) break; }
        if (!hh) continue;
        int p = j + 1; while (p < len && pp_ws(s[p])) p++;
        if (s[p]=='e'&&s[p+1]=='n'&&s[p+2]=='d'&&s[p+3]=='i'&&s[p+4]=='f') { if (d==0) return; d--; }
        else if (s[p]=='e'&&s[p+1]=='l'&&s[p+2]=='s'&&s[p+3]=='e') { if (d==0) return; }
        else if (s[p]=='i'&&s[p+1]=='f') d++;
    }
}

/* Full do_directive with all handlers */
static void do_directive(const char *s, int ls, int le, OutBuf *out, int depth) {
    (void)out; (void)depth;
    int p = ls; while (p < le && pp_ws(s[p])) p++;
    if (p >= le || s[p] != '#') return;
    p++; while (p < le && pp_ws(s[p])) p++;
    int dw = p; while (p < le && !pp_ws(s[p]) && s[p] != '\n') p++;
    int dl = p - dw;

    if (dl == 6 && s[dw]=='d'&&s[dw+1]=='e'&&s[dw+2]=='f') {
        while (p < le && pp_ws(s[p])) p++; int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p == ms) return; int mnl = p - ms;
        int cp = p; while (cp < le && pp_ws(s[cp])) cp++;
        if (cp == p && cp < le && s[cp] == '(') {
            if (func_macro_count >= MAX_FUNC_MACROS) return;
            FuncMacro *fm = &func_macros[func_macro_count];
            char *mn2 = (char *)tlibc_malloc(mnl + 1);
            { int ci; for (ci = 0; ci < mnl; ci++) mn2[ci] = s[ms + ci]; mn2[mnl] = '\0'; }
            fm->name = mn2; fm->param_count = 0; fm->is_variadic = 0; cp++;
            while (cp < le && s[cp] != ')') {
                while (cp < le && pp_ws(s[cp])) cp++;
                if (cp >= le || s[cp] == ')') break;
                if (cp + 2 <= le && s[cp] == '.' && s[cp+1] == '.' && s[cp+2] == '.') { fm->is_variadic = 1; cp += 3; break; }
                int ps = cp; while (cp < le && pp_id(s[cp])) cp++;
                if (cp > ps && fm->param_count < MAX_MACRO_PARAMS) {
                    char *pn = (char *)tlibc_malloc(cp - ps + 1);
                    { int ci; for (ci = 0; ci < cp - ps; ci++) pn[ci] = s[ps + ci]; pn[cp - ps] = '\0'; }
                    fm->params[fm->param_count++] = pn;
                }
                while (cp < le && pp_ws(s[cp])) cp++;
                if (cp < le && s[cp] == ',') { cp++; continue; }
            }
            if (cp < le && s[cp] == ')') cp++;
            while (cp < le && pp_ws(s[cp])) cp++;
            { int vs = cp; int vl = le - cp; while (vl > 0 && pp_ws(s[vs+vl-1])) vl--;
                if (vl > 0) {
                    OutBuf b = {0,0,0}; int ii = 0;
                    while (ii < vl) {
                        if (s[vs+ii] == '\\' && ii+1 < vl) {
                            int ni = ii + 1;
                            if (s[vs+ni] == '\r') ni++;
                            if (ni < vl && s[vs+ni] == '\n') { ii = ni + 1; while (ii < vl && (s[vs+ii] == ' ' || s[vs+ii] == '\t')) ii++; continue; }
                        }
                        out_putc(&b, s[vs+ii]); ii++;
                    }
                    out_putc(&b, '\0'); fm->replacement = b.data; fm->repl_len = b.len - 1;
                } else { fm->replacement = 0; fm->repl_len = 0; }
            }
            func_macro_count++; return;
        }
        while (p < le && pp_ws(s[p])) p++;
        { int vs = p; int vl = le - p; while (vl > 0 && pp_ws(s[vs+vl-1])) vl--;
            char *n = (char *)tlibc_malloc(mnl+1); get_name(s, ms, ms+mnl, n, mnl+1);
            char *v = 0; int rvl = 0;
            if (vl > 0) {
                OutBuf b = {0,0,0}; int ii = 0;
                while (ii < vl) {
                    if (s[vs+ii] == '\\' && ii+1 < vl) {
                        int ni = ii + 1;
                        if (s[vs+ni] == '\r') ni++;
                        if (ni < vl && s[vs+ni] == '\n') { ii = ni + 1; while (ii < vl && (s[vs+ii] == ' ' || s[vs+ii] == '\t')) ii++; continue; }
                    }
                    out_putc(&b, s[vs+ii]); ii++;
                }
                out_putc(&b, '\0'); rvl = b.len - 1; v = b.data;
            }
            add_macro(n, v, rvl); return;
        }
    }

    if (dl == 6 && s[dw]=='i'&&s[dw+1]=='f'&&s[dw+2]=='d') {
        while (p < le && pp_ws(s[p])) p++; int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p > ms) { char mn[256]; get_name(s, ms, p, mn, 256); if (!macro_defined(mn)) skip_cond(s, &p, le); }
        return;
    }
    if (dl == 7 && s[dw]=='i'&&s[dw+1]=='f'&&s[dw+2]=='n') {
        while (p < le && pp_ws(s[p])) p++; int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p > ms) { char mn[256]; get_name(s, ms, p, mn, 256); if (macro_defined(mn)) skip_cond(s, &p, le); }
        return;
    }
    if ((dl == 4 && s[dw]=='e'&&s[dw+1]=='l'&&s[dw+2]=='s'&&s[dw+3]=='e') ||
        (dl == 5 && s[dw]=='e'&&s[dw+1]=='n'&&s[dw+2]=='d')) return;
    if (dl == 5 && s[dw]=='u'&&s[dw+1]=='n') {
        while (p < le && pp_ws(s[p])) p++; int ms = p; while (p < le && pp_id(s[p])) p++;
        if (p > ms) { char mn[256]; get_name(s, ms, p, mn, 256); undef_macro(mn); }
        return;
    }
    if (dl == 5 && s[dw]=='e'&&s[dw+1]=='r'&&s[dw+2]=='r') { return; }
}

/* pp_full from test_cond (EXACT same code - full expansion) */
static void pp_full(const char *s, int len, OutBuf *out, int depth) {
    int i = 0;
    while (i < len) {
        if (pp_cond_skip > 0 && s[i] != '#') {
            while (i < len && s[i] != '\n') i++;
            if (i < len && s[i] == '\n') i++;
            continue;
        }
        if (s[i] == '#') {
            int is_directive = 0;
            if (i == 0) is_directive = 1;
            else { int bi = i;
                while (bi > 0 && (s[bi-1] == ' ' || s[bi-1] == '\t')) bi--;
                if (bi == 0 || s[bi-1] == '\n' || s[bi-1] == '\r') is_directive = 1; }
            if (is_directive) {
                int ls = i; int le = i;
                while (le < len && s[le] != '\n') {
                    if (s[le] == '\\') { int nl = le+1; if (nl<len && s[nl]=='\r') nl++; if (nl<len && s[nl]=='\n') le=nl+1; else le++; }
                    else { le++; }
                }
                int cpos = ls;
                while (cpos < le && (s[cpos] == ' ' || s[cpos] == '\t')) cpos++;
                if (cpos < le && s[cpos] == '#') {
                    int cw = cpos + 1; while (cw < le && (s[cw] == ' ' || s[cw] == '\t')) cw++;
                    int wlen = le - cw;
                    if (wlen >= 4 && s[cw]=='e'&&s[cw+1]=='l'&&s[cw+2]=='s'&&s[cw+3]=='e') {
                        if (pp_cond_depth > 0) { if (pp_cond_emit[pp_cond_depth-1]) { pp_cond_skip++; } else { if (pp_cond_skip > 0) pp_cond_skip--; } pp_cond_emit[pp_cond_depth-1] = !pp_cond_emit[pp_cond_depth-1]; }
                        i = le; if (i < len && s[i] == '\n') i++; continue;
                    }
                    if (wlen >= 5 && s[cw]=='e'&&s[cw+1]=='n'&&s[cw+2]=='d'&&s[cw+3]=='i'&&s[cw+4]=='f') {
                        if (pp_cond_depth > 0) { if (!pp_cond_emit[pp_cond_depth-1] && pp_cond_skip > 0) pp_cond_skip--; pp_cond_depth--; }
                        i = le; if (i < len && s[i] == '\n') i++; continue;
                    }
                    if (wlen >= 5 && s[cw]=='i'&&s[cw+1]=='f') {
                        int is_ifndef = (wlen >= 6 && s[cw+2]=='n'); int is_ifdef = !is_ifndef && (wlen < 6 || s[cw+2]!='n');
                        if (is_ifdef || is_ifndef) {
                            int mp = cw + (is_ifndef ? 6 : 5);
                            while (mp < le && (s[mp] == ' ' || s[mp] == '\t')) mp++;
                            int ms = mp; while (mp < le && ((s[mp] >= 'a' && s[mp] <= 'z') || (s[mp] >= 'A' && s[mp] <= 'Z') || s[mp] == '_' || (s[mp] >= '0' && s[mp] <= '9'))) mp++;
                            int is_def = 0;
                            if (mp > ms) { char mn[256]; int ci; for (ci = 0; ci < mp-ms && ci < 255; ci++) mn[ci] = s[ms+ci]; mn[mp-ms] = '\0'; is_def = macro_defined(mn); }
                            int emit_block; if (is_ifdef) emit_block = is_def; else emit_block = !is_def;
                            if (pp_cond_depth < 32) { if (emit_block) { pp_cond_emit[pp_cond_depth] = 1; } else { pp_cond_emit[pp_cond_depth] = 0; pp_cond_skip++; } pp_cond_depth++; }
                            i = le; if (i < len && s[i] == '\n') i++; continue;
                        }
                    }
                }
                if (pp_cond_skip > 0) { i = le; if (i < len && s[i] == '\n') i++; continue; }
                do_directive(s, ls, le, out, depth);
                i = le; if (i < len && s[i] == '\n') i++; continue;
            }
        }
        if (s[i] == '"') {
            out_putc(out, '"'); i++;
            while (i < len && s[i] != '"') { if (s[i] == '\\' && i+1 < len) { out_putc(out, s[i]); i++; } out_putc(out, s[i]); i++; }
            if (i < len) { out_putc(out, '"'); i++; }
            continue;
        }
        if ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || s[i] == '_') {
            int start = i; while (i < len && pp_id(s[i])) i++; int id_len = i - start;
            int mi; int expanded = 0;
            for (mi = 0; mi < macro_count; mi++) {
                const char *mn = macros[mi].name; int j;
                for (j = 0; j < id_len; j++) if (mn[j] != s[start + j]) goto nm;
                if (mn[j] != '\0') goto nm;
                if (macros[mi].value && macros[mi].value_len > 0) {
                    if (depth < 64) { pp_full(macros[mi].value, macros[mi].value_len, out, depth + 1); }
                    else { int vi; for (vi = 0; vi < macros[mi].value_len; vi++) out_putc(out, macros[mi].value[vi]); }
                }
                expanded = 1; break; nm:;
            }
            if (expanded) continue;
            if (i < len && s[i] == '(' && func_macro_count > 0) {
                int fmi; int matched = 0;
                for (fmi = 0; fmi < func_macro_count; fmi++) {
                    const char *fn = func_macros[fmi].name; int j;
                    for (j = 0; j < id_len; j++) if (fn[j] != s[start + j]) goto fnm;
                    if (fn[j] != '\0') goto fnm;
                    matched = 1;
                    if (is_expanding(fn)) { int ix; for (ix = start; ix < i; ix++) out_putc(out, s[ix]); break; }
                    int ap = i + 1; int adepth = 1; int aprev = -1;
                    g_arg_count = 0;
                    g_arg_starts[0] = s + ap;
                    while (adepth > 0 && ap < len) {
                        if (ap == aprev) break; aprev = ap;
                        if (s[ap] == '"') { ap++; while (ap < len && s[ap] != '"') { if (s[ap] == '\\' && ap+1 < len) ap++; ap++; } if (ap < len) ap++; continue; }
                        if (s[ap] == '\'') { ap++; if (ap < len && s[ap] == '\\') ap++; if (ap < len) ap++; if (ap < len && s[ap] == '\'') ap++; continue; }
                        if (s[ap] == '(') adepth++;
                        if (s[ap] == ')') { adepth--; if (adepth == 0) break; }
                        if (adepth == 1 && s[ap] == ',' && g_arg_count < MAX_MACRO_PARAMS-1) {
                            g_arg_lens[g_arg_count] = (s + ap) - g_arg_starts[g_arg_count];
                            g_arg_count++; g_arg_starts[g_arg_count] = s + ap + 1;
                        }
                        ap++; if (ap >= len) break;
                    }
                    if (adepth == 0) {
                        g_arg_lens[g_arg_count] = (s + ap) - g_arg_starts[g_arg_count]; g_arg_count++;
                        { int ai; for (ai = 0; ai < g_arg_count; ai++) {
                            while (g_arg_lens[ai] > 0 && pp_ws(g_arg_starts[ai][0])) { g_arg_starts[ai]++; g_arg_lens[ai]--; }
                            while (g_arg_lens[ai] > 0 && pp_ws(g_arg_starts[ai][g_arg_lens[ai]-1])) g_arg_lens[ai]--; } }
                        int pplv = g_pp_lv;
                        pp_push();
                        { int pbase = pplv * 16;
                        { int ai; for (ai = 0; ai < g_arg_count; ai++) {
                            OutBuf ab; ab.data = 0; ab.len = 0; ab.cap = 0;
                            expand_stack[expand_stack_depth++] = fn;
                            pp_full(g_pp_as[pbase + ai], g_pp_al[pbase + ai], &ab, depth + 1);
                            expand_stack_depth--;
                            if (ab.data && ab.len > 0) { g_expanded_args[ai] = ab.data; g_expanded_lens[ai] = ab.len; }
                            else { g_expanded_args[ai] = 0; g_expanded_lens[ai] = 0; }
                            g_pp_ex[pbase + ai] = g_expanded_args[ai];
                            g_pp_ex_len[pbase + ai] = g_expanded_lens[ai];
                        } }
                        }
                        OutBuf temp = {0,0,0};
                        if (func_macros[fmi].replacement) {
                            const char *rp = func_macros[fmi].replacement; int rl = func_macros[fmi].repl_len; int ri = 0;
                            while (ri < rl) {
                                if (rp[ri] == '"') { out_putc(&temp, rp[ri]); ri++; while (ri < rl && rp[ri] != '"') { if (rp[ri] == '\\' && ri+1 < rl) { out_putc(&temp, rp[ri]); ri++; } out_putc(&temp, rp[ri]); ri++; } if (ri < rl) { out_putc(&temp, rp[ri]); ri++; } continue; }
                                if (rp[ri] == '\'') { out_putc(&temp, rp[ri]); ri++; if (ri < rl && rp[ri] == '\\') { out_putc(&temp, rp[ri]); ri++; } if (ri < rl) { out_putc(&temp, rp[ri]); ri++; } if (ri < rl && rp[ri] == '\'') { out_putc(&temp, rp[ri]); ri++; } continue; }
                                if (ri+1 < rl && rp[ri]=='#' && rp[ri+1]=='#') { ri += 2; while (ri < rl && (rp[ri] == ' ' || rp[ri] == '\t')) ri++; while (temp.len > 0 && (temp.data[temp.len-1] == ' ' || temp.data[temp.len-1] == '\t')) temp.len--; continue; }
                                if (pp_id(rp[ri])) { int rs = ri; while (ri < rl && pp_id(rp[ri])) ri++;
                                    int pi; int matched2 = 0;
                                    for (pi = 0; pi < func_macros[fmi].param_count && pi < g_pp_ac[pplv]; pi++) {
                                        const char *pn = func_macros[fmi].params[pi]; int jj;
                                        for (jj = 0; jj < ri - rs; jj++) if (pn[jj] != rp[rs+jj]) goto pnm;
                                        if (pn[jj] != '\0') goto pnm;
                                        { int _eb = pplv * 16 + pi; char *ea = g_pp_ex[_eb]; int el = g_pp_ex_len[_eb];
                                          if (ea) { int vj; for (vj = 0; vj < el; vj++) out_putc(&temp, ea[vj]); }
                                          else { int _ab = pplv * 16 + pi; const char *as = g_pp_as[_ab]; int al = g_pp_al[_ab]; int vj; for (vj = 0; vj < al; vj++) out_putc(&temp, as[vj]); } }
                                        matched2 = 1; break; pnm:;
                                    }
                                    if (!matched2) {
                                        if (func_macros[fmi].is_variadic && (ri - rs) == 11) {
                                            int vam = 1; const char *va = "__VA_ARGS__"; int vk;
                                            for (vk = 0; vk < 11; vk++) if (va[vk] != rp[rs+vk]) { vam = 0; break; }
                                            if (vam) {
                                                int vai = func_macros[fmi].param_count; int vfirst = 1;
                                                while (vai < g_pp_ac[pplv]) {
                                                    if (!vfirst) out_putc(&temp, ',');
                                                    vfirst = 0;
                                                    { int _eb = pplv * 16 + vai; char *ea = g_pp_ex[_eb]; int el = g_pp_ex_len[_eb];
                                                      if (ea) { int vj; for (vj = 0; vj < el; vj++) out_putc(&temp, ea[vj]); }
                                                      else { int _ab = pplv * 16 + vai; const char *as = g_pp_as[_ab]; int al = g_pp_al[_ab]; int vj; for (vj = 0; vj < al; vj++) out_putc(&temp, as[vj]); } }
                                                    vai++;
                                                }
                                                continue;
                                            }
                                        }
                                        int idx; for (idx = rs; idx < ri; idx++) out_putc(&temp, rp[idx]);
                                    }
                                    continue;
                                }
                                out_putc(&temp, rp[ri]); ri++;
                            }
                        }
                        int used_follow_on = 0;
                        if (temp.data && temp.len > 0) {
                            if (depth < 64) {
                                OutBuf pp_result = { 0, 0, 0 };
                                expand_stack[expand_stack_depth++] = fn;
                                pp_full(temp.data, temp.len, &pp_result, depth + 1);
                                expand_stack_depth--;
                                { int ri2 = 0; while (ri2 < pp_result.len && pp_ws(pp_result.data[ri2])) ri2++;
                                    if (ri2 < pp_result.len && pp_id(pp_result.data[ri2])) {
                                        int rs2 = ri2; while (ri2 < pp_result.len && pp_id(pp_result.data[ri2])) ri2++;
                                        int fmi2; for (fmi2 = 0; fmi2 < func_macro_count; fmi2++) {
                                            const char *fn2 = func_macros[fmi2].name; int jn;
                                            for (jn = 0; jn < ri2 - rs2; jn++) if (fn2[jn] != pp_result.data[rs2 + jn]) goto fnm2;
                                            if (fn2[jn] != '\0') goto fnm2;
                                            if (ap + 1 < len && s[ap + 1] == '(' && !is_expanding(fn2)) {
                                                int paren_start = ap + 1; int paren_depth = 1; int scan_pos = paren_start + 1;
                                                while (paren_depth > 0 && scan_pos < len) {
                                                    if (s[scan_pos] == '"') { scan_pos++; while (scan_pos < len && s[scan_pos] != '"') { if (s[scan_pos] == '\\' && scan_pos + 1 < len) scan_pos++; scan_pos++; } if (scan_pos < len) scan_pos++; continue; }
                                                    if (s[scan_pos] == '(') paren_depth++;
                                                    if (s[scan_pos] == ')') { paren_depth--; if (paren_depth == 0) break; }
                                                    scan_pos++;
                                                }
                                                if (paren_depth == 0) {
                                                    OutBuf combined = { 0, 0, 0 }; int ci;
                                                    for (ci = 0; ci < pp_result.len; ci++) out_putc(&combined, pp_result.data[ci]);
                                                    for (ci = paren_start; ci <= scan_pos; ci++) out_putc(&combined, s[ci]);
                                                    pp_full(combined.data, combined.len, out, depth + 1);
                                                    tlibc_free(combined.data); i = scan_pos + 1; used_follow_on = 1; break;
                                                }
                                            } fnm2:;
                                        }
                                    }
                                }
                                if (!used_follow_on) { int wi; for (wi = 0; wi < pp_result.len; wi++) out_putc(out, pp_result.data[wi]); }
                                tlibc_free(pp_result.data);
                            } else { int ti; for (ti = 0; ti < temp.len; ti++) out_putc(out, temp.data[ti]); }
                            tlibc_free(temp.data);
                        }
                        { int ai; for (ai = 0; ai < g_pp_ac[pplv]; ai++) { int _eb = pplv * 16 + ai; if (g_pp_ex[_eb]) tlibc_free(g_pp_ex[_eb]); } }
                        g_pp_lv--;
						    if (!used_follow_on) { i = ap + 1; }
                    }
                    break; fnm:;
                }
                if (matched) continue;
            }
            { int j; for (j = start; j < i; j++) out_putc(out, s[j]); }
            continue;
        }
        if (s[i] != 13) { out_putc(out, s[i]); } i++;
    }
}



/* strip_all_comments: replace block/line comments with spaces */
static char *strip_all_comments(const char *src, int len, int *out_len) {
    OutBuf out = { 0, 0, 0 };
    int ci = 0;
    while (ci < len) {
        if (src[ci] == '"') {
            out_putc(&out, src[ci]); ci++;
            while (ci < len && src[ci] != '"') {
                if (src[ci] == '\\' && ci+1 < len) { out_putc(&out, src[ci]); ci++; }
                out_putc(&out, src[ci]); ci++;
            }
            if (ci < len) { out_putc(&out, src[ci]); ci++; }
            continue;
        }
        if (src[ci] == '\'') {
            out_putc(&out, src[ci]); ci++;
            if (ci < len && src[ci] == '\\') { out_putc(&out, src[ci]); ci++; }
            if (ci < len) { out_putc(&out, src[ci]); ci++; }
            if (ci < len && src[ci] == '\'') { out_putc(&out, src[ci]); ci++; }
            continue;
        }
        if (src[ci] == '/' && ci+1 < len) {
            if (src[ci+1] == '/') {
                ci += 2; while (ci < len && src[ci] != '\n') { out_putc(&out, ' '); ci++; }
                continue;
            }
            if (src[ci+1] == '*') {
                ci += 2;
                while (ci < len) {
                    if (src[ci]=='*' && ci+1<len && src[ci+1]=='/') { ci+=2; break; }
                    if (src[ci] == '\n') out_putc(&out, '\n');
                    else out_putc(&out, ' ');
                    ci++;
                }
                continue;
            }
        }
        out_putc(&out, src[ci]);
        ci++;
    }
    out_putc(&out, '\0');
    *out_len = out.len - 1;
    return out.data;
}

/* preprocess: top-level entry point, strips comments then expands macros */
static char *preprocess(const char *src, int len, const char *fname, int *out_len) {
    macro_count = 0;
    func_macro_count = 0;
    expand_stack_depth = 0;
    pp_cond_skip = 0;
    pp_cond_depth = 0;
    g_pp_lv = 0;
    if (fname) {
        int fnl = 0; while (fname[fnl]) fnl++;
        char *fv = (char *)tlibc_malloc(fnl + 3);
        fv[0] = '"'; { int fi; for (fi = 0; fi < fnl; fi++) fv[fi+1] = fname[fi]; }
        fv[fnl+1] = '"'; fv[fnl+2] = '\0';
        add_macro("__FILE__", fv, fnl + 2);
    } else {
        add_macro("__FILE__", "\"<unknown>\"", 11);
    }
    add_macro("__LINE__", "0", 1);
    add_macro("__x86_64__", 0, 0);
    add_macro("X86_64_TLIBC", "1", 1);
    int clean_len;
    char *clean = strip_all_comments(src, len, &clean_len);
    OutBuf out = { 0, 0, 0 };
    pp_full(clean, clean_len, &out, 0);
    tlibc_free(clean);
    out_putc(&out, '\0');
    *out_len = out.len > 0 ? out.len - 1 : 0;
    return out.data;
}

static int test_passed = 0;
static int test_failed = 0;

static void print_dec(long n) {
    char buf[32];
    int i = 0;
    if (n < 0) { print_str("-"); n = -n; }
    if (n == 0) { buf[i++] = '0'; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) { __sys_write(1, &buf[--i], 1); }
}

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        print_str("  FAIL "); print_str(msg); print_str("\n"); \
        test_failed++; \
    } else { \
        test_passed++; \
    } \
} while (0)

/* 预处理辅助：直接调用 preprocess，返回输出 buffer */
typedef struct {
    char *data;
    int len;
} PPResult;

static PPResult pp(const char *input) {
    int len = 0;
    while (input[len]) len++;
    int out_len;
    char *out = preprocess(input, len, "test.c", &out_len);
    PPResult r = { out, out_len };
    return r;
}


/* 字符串比较（忽略尾部空白） */
static int eq_trim(const char *a, int alen, const char *b) {
    int i = alen;
    while (i > 0 && (a[i-1] == ' ' || a[i-1] == '\t' || a[i-1] == '\n' || a[i-1] == '\r'))
        i--;
    int blen = 0; while (b[blen]) blen++;
    while (blen > 0 && (b[blen-1] == ' ' || b[blen-1] == '\t' || b[blen-1] == '\n' || b[blen-1] == '\r'))
        blen--;
    if (i != blen) return 0;
    int j;
    for (j = 0; j < i; j++) if (a[j] != b[j]) return 0;
    return 1;
}

/* 检查预处理输出是否等于预期字符串（忽略尾部空白） */
#define CHECK_PP(input, expected, msg) do { \
    PPResult r = pp(input); \
    if (!eq_trim(r.data, r.len, expected)) { \
        print_str("  FAIL "); print_str(msg); print_str(":\n"); \
        print_str("    got:      \""); \
        { int _i; for (_i = 0; _i < r.len; _i++) __sys_write(1, &r.data[_i], 1); } \
        print_str("\"\n"); \
        print_str("    expected: \""); print_str(expected); print_str("\"\n"); \
        test_failed++; \
    } else { \
        test_passed++; \
    } \
    heap_used = 0; \
} while (0)

/* 精确比较（含尾部空白） — 未被测试引用，保留以备将来使用 */
#define CHECK_PP_EXACT(input, expected, msg) do { \
    PPResult r = pp(input); \
    if (!eq_trim(r.data, r.len, expected)) { \
        print_str("  FAIL "); print_str(msg); print_str(":\n"); \
        print_str("    got:      \""); \
        { int _i; for (_i = 0; _i < r.len; _i++) __sys_write(1, &r.data[_i], 1); } \
        print_str("\"\n"); \
        print_str("    expected: \""); print_str(expected); print_str("\"\n"); \
        test_failed++; \
    } else { \
        test_passed++; \
    } \
    heap_used = 0; \
} while (0)

static void run_section(const char *name) {
    print_str("\n--- "); print_str(name); print_str(" ---\n");
    test_passed = 0;
    test_failed = 0;
}

static void print_section_result(void) {
    print_str("  -> ");
    print_dec(test_passed); print_str(" passed, ");
    print_dec(test_failed); print_str(" failed\n");
}

// ============================================================
// 预处理器测试用例
// ============================================================

/* 每次调用前重置堆 */
static void reset(void) {
    heap_used = 0;
    macro_count = 0;
    func_macro_count = 0;
    expand_stack_depth = 0;
    pp_cond_skip = 0;
    pp_cond_depth = 0;
    g_pp_lv = 0;
}

/* 1. 基础对象宏 */
static void test_object_macros(void) {
    run_section("Object-like Macros");

    reset();
    /* 简单常量替换 */
    CHECK_PP("#define FOO 42\nFOO\n", "42", "basic #define FOO 42");

    reset();
    /* 多行文本 */
    CHECK_PP("#define MSG hello world\nMSG\n", "hello world", "#define MSG multi-word");

    reset();
    /* 空定义（仅定义标记） */
    CHECK_PP("#define EMPTY\nEMPTY\n", "", "#define EMPTY expands to nothing");

    reset();
    /* 宏值中包含空格 */
    CHECK_PP("#define VAL a + b\nVAL\n", "a + b", "#define VAL with spaces");

    reset();
    /* 宏中出现其他宏名（预展开） */
    CHECK_PP("#define X 1\n#define Y X\nY\n", "1", "nested macro X in Y");
}

/* 2. 函数式宏 */
static void test_func_macros(void) {
    run_section("Function-like Macros");

    reset();
    CHECK_PP("#define ADD(a,b) ((a)+(b))\nADD(1,2)\n", "((1)+(2))", "basic function macro");

    reset();
    CHECK_PP("#define MUL(a,b) a*b\nMUL(3,4)\n", "3*4", "func macro no parens");

    reset();
    CHECK_PP("#define ID(x) x\nID(42)\n", "42", "identity macro");

    reset();
    /* 多层嵌套 — 参数先展开再替换：ADD(1,2) → ((1)+(2)) 代入 MUL */
    CHECK_PP("#define ADD(a,b) ((a)+(b))\n#define MUL(a,b) ((a)*(b))\nMUL(ADD(1,2),ADD(3,4))\n",
             "((((1)+(2)))*(((3)+(4))))", "nested func macros");

    reset();
    /* 空参数 */
    CHECK_PP("#define EMPTY() \nEMPTY()\n", "", "empty func macro");

    reset();
    /* 逗号分隔参数 */
    CHECK_PP("#define F(a,b) a b\nF(hello,world)\n", "hello world", "two params");
}

/* 3. #undef */
static void test_undef(void) {
    run_section("#undef");

    reset();
    CHECK_PP("#define FOO 42\nFOO\n#undef FOO\nFOO\n", "42\nFOO", "#define then #undef then raw ident");

    reset();
    /* undef 不存在的宏不应报错 */
    CHECK_PP("#undef NOTDEFINED\nint x;\n", "int x;", "#undef non-existent macro");
}

/* 4. #ifdef / #ifndef / #else / #endif */
static void test_cond_compile(void) {
    run_section("Conditional Compilation");

    reset();
    CHECK_PP("#define FOO\n#ifdef FOO\nkeep\n#endif\n", "keep", "#ifdef defined");

    reset();
    CHECK_PP("#define FOO\n#ifndef FOO\nskip\n#endif\n", "", "#ifndef defined → skip");

    reset();
    CHECK_PP("#ifdef UNDEF\nskip\n#endif\nkeep\n", "keep", "#ifdef undefined → skip");

    reset();
    CHECK_PP("#ifndef UNDEF\nkeep\n#endif\n", "keep", "#ifndef undefined → keep");

    reset();
    CHECK_PP("#define FOO\n#ifdef FOO\nkeep\n#else\nskip\n#endif\n", "keep", "#ifdef + #else keep");

    reset();
    CHECK_PP("#ifdef UNDEF\nskip\n#else\nkeep\n#endif\n", "keep", "#ifdef undef + #else keep");

    reset();
    /* 嵌套条件 */
    CHECK_PP("#define A\n#define B\n#ifdef A\n#ifdef B\nboth\n#endif\n#endif\n",
             "both", "nested #ifdef A + #ifdef B");
}

/* 5. 字符串字面量中的宏名不展开 */
static void test_string_preserve(void) {
    run_section("String Literal Preservation");

    reset();
    CHECK_PP("#define FOO 42\n\"FOO\"\n", "\"FOO\"", "macro not expanded in string");

    reset();
    CHECK_PP("#define BAR hello\n\"BAR world\"\n", "\"BAR world\"", "macro not expanded in string 2");
}

/* 6. 注释剥离 */
static void test_comment_strip(void) {
    run_section("Comment Stripping");

    reset();
    /* 块注释中的每个字符变成空格（保留行号对应）——9 inside chars = 9 spaces */
    CHECK_PP("int /* comment */ x;\n", "int           x;", "block comment stripped");

    reset();
    reset();
    /* 行注释：int(空格) + "// line comment"(13 chars→13spaces) + newline(保留) + x; */
    /* 精确验证：int(3) + 1space + 13spaces + \n + x; + \n = 21 字节 */
    { PPResult r = pp("int // line comment\nx;\n");
    int ok = (r.len == 21) && (r.data[0]=='i') && (r.data[1]=='n') && (r.data[2]=='t');
    int si;
    for (si = 4; si < 17; si++) { if (r.data[si] != ' ') ok = 0; }
    ok = ok && (r.data[17] == '\n') && (r.data[18]=='x') && (r.data[19]==';') && (r.data[20]=='\n');
    CHECK(ok, "line comment stripped (exact byte check)");
    heap_used = 0; }

    reset();
    /* 宏定义中的注释被剥离 */
    CHECK_PP("#define FOO 42 /* comment */\nFOO\n", "42", "comment in #define stripped");
}

/* 7. ## token 粘贴 */
static void test_token_paste(void) {
    run_section("## Token Pasting");

    reset();
    CHECK_PP("#define CAT(a,b) a##b\nCAT(foo,bar)\n", "foobar", "basic ## paste");

    reset();
    CHECK_PP("#define CONCAT3(a,b,c) a##b##c\nCONCAT3(x,y,z)\n", "xyz", "triple ## paste");

    /* 注意：空参数 + ## 不会出错（GCC 扩展） */
    reset();
    CHECK_PP("#define CAT2(a,b) a##b\nCAT2(x,)\n", "x", "## with empty second param");
}

/* 8. 变参宏 */
static void test_variadic(void) {
    run_section("Variadic Macros");

    reset();
    /* 基本变参：__VA_ARGS__ 替换为额外参数 */
    CHECK_PP("#define PRINT(fmt,...) fmt __VA_ARGS__\nPRINT(\"hi\",42)\n",
             "\"hi\" 42", "basic __VA_ARGS__");

    reset();
    /* 只有一个 fmt 参数时，__VA_ARGS__ 为空 */
    CHECK_PP("#define PRINT(fmt,...) fmt __VA_ARGS__\nPRINT(\"hi\")\n",
             "\"hi\" ", "empty __VA_ARGS__");
}

/* 9. #error 指令 */
static void test_error_directive(void) {
    run_section("#error Directive");

    reset();
    /* #error 行在输出中消失，错误消息打印到 stderr */
    PPResult r = pp("#error this is wrong\nstill here\n");
    CHECK(eq_trim(r.data, r.len, "still here"), "#error removed from output, rest passes through");
    heap_used = 0;
}

/* 10. 预定义宏 */
static void test_predefined(void) {
    run_section("Predefined Macros");

    reset();
    { PPResult r = pp("__FILE__\n");
    CHECK(eq_trim(r.data, r.len, "\"test.c\""), "__FILE__ expands to \"test.c\"");
    heap_used = 0; }

    reset();
    { PPResult r2 = pp("__LINE__\n");
    CHECK(eq_trim(r2.data, r2.len, "0"), "__LINE__ expands to 0");
    heap_used = 0; }

    reset();
    /* 不传 fname 时 __FILE__ 为 "<unknown>" */
    { int len; char *out = preprocess("__FILE__\n", 8, 0, &len);
    CHECK(eq_trim(out, len, "\"<unknown>\""), "__FILE__ without fname");
    heap_used = 0; }

    reset();
    CHECK_PP("__x86_64__\n", "", "__x86_64__ expands to nothing");

    reset();
    CHECK_PP("X86_64_TLIBC\n", "1", "X86_64_TLIBC expands to 1");
}

/* 11. 普通文本保留 */
static void test_plain_text(void) {
    run_section("Plain Text Passthrough");

    reset();
    CHECK_PP("int main(void) { return 0; }\n",
             "int main(void) { return 0; }", "plain C code preserved");

    reset();
    CHECK_PP("", "", "empty input");

    reset();
    CHECK_PP("\n\n\n", "", "whitespace-only input");
}

/* 12. 续行符处理 */
static void test_line_continuation(void) {
    run_section("Line Continuation");

    reset();
    /* \ 续行拼接宏定义 */
    CHECK_PP("#define FOO \\\n42\nFOO\n", "42", "line continuation in #define");

    reset();
    /* 连续多行续行 */
    CHECK_PP("#define SUM a+\\\nb+\\\nc\nSUM\n", "a+b+c", "multiple line continuations");
}

/* 13. 空参数预处理 */
static void test_token_paste_followon(void) {
    run_section("Token Paste Follow-on");

    reset();
    /* ## 后的宏名与外部 (args) 结合 */
    CHECK_PP("#define ID(x) x\n#define CAT(a,b) a##b\nCAT(ID,(42))\n",
             "42", "## paste + follow-on function call");
}

// ============================================================
// 入口
// ============================================================

void __tlibc_start(void) {
    print_str("=== preproc.c standalone tests ===\n");

    test_plain_text();
    print_section_result();

    test_object_macros();
    print_section_result();

    test_func_macros();
    print_section_result();

    test_undef();
    print_section_result();

    test_cond_compile();
    print_section_result();

    test_string_preserve();
    print_section_result();

    test_comment_strip();
    print_section_result();

    test_token_paste();
    print_section_result();

    test_variadic();
    print_section_result();

    test_error_directive();
    print_section_result();

    test_predefined();
    print_section_result();

    test_line_continuation();
    print_section_result();

    test_token_paste_followon();
    print_section_result();

    /* 汇总 */
    print_str("\n=== ");
    if (test_failed == 0) {
        print_str("ALL PASSED");
    } else {
        print_str("SOME FAILED");
    }
    print_str(" ===\n");

    __sys_exit(test_failed != 0 ? 1 : 0);
}
