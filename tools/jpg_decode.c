/* Hand-written baseline JPEG decoder for toyasset.
 *
 * Supports: SOF0 (baseline), 8-bit precision, grayscale (1 component) and
 * YCbCr (3 components, H/V sampling 2x2/2x1/1x1), 8/16-bit DQT, up to four
 * DHT tables with 16-bit codes, DRI restart intervals. Rejects progressive
 * (SOF2) and other variants with a clean error. Fixed-point separable IDCT
 * (2^13 scale, round-to-nearest), box chroma upsampling, BT.601 YCbCr->RGB.
 */
#include "jpg_decode.h"
#include <stdlib.h>
#include <string.h>

#define JD_BAD (-1)
#define LUT_INVALID 0xFFFFu

/* Fixed-point IDCT table: T[k][n] = round(0.5 * C(k) * cos((2n+1)k*pi/16) * 2^13),
 * C(0) = 1/sqrt(2), C(k) = 1 otherwise. */
static const int T[8][8] = {
  {2896, 2896, 2896, 2896, 2896, 2896, 2896, 2896},
  {4017, 3406, 2276,  799, -799, -2276, -3406, -4017},
  {3784, 1567, -1567, -3784, -3784, -1567, 1567, 3784},
  {3406, -799, -4017, -2276, 2276, 4017,  799, -3406},
  {2896, -2896, -2896, 2896, 2896, -2896, -2896, 2896},
  {2276, -4017,  799, 3406, -3406, -799, 4017, -2276},
  {1567, -3784, 3784, -1567, -1567, 3784, -3784, 1567},
  { 799, -2276, 3406, -4017, 4017, -3406, 2276, -799},
};

/* Zigzag index -> natural (row-major) order, T.81 Table K.1. */
static const int zigzag[64] = {
  0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

struct jd {
    const unsigned char *in;
    size_t n, pos;
    uint32_t w, h;
    int ncomp;                 /* 1 or 3 */
    int hv[3], qsel[3];        /* per component: (H<<4)|V, quant table id */
    int hmax, vmax;
    int have_q[2];
    int16_t qtab[2][64];
    const uint16_t *lut[4];    /* [cls*2+id]: DC0 AC0 DC1 AC1 */
    int lutlen[4], have_ht[4];
    int nscan;
    int scid[3], sdc[3], sac[3];
    int dri;
    uint64_t bits;             /* bit buffer, MSB first */
    int bitcnt;
    int mark;                  /* marker byte seen in scan data, 0 = none */
    int eoi_seen;              /* EOI consumed by the scan decoder */
    int pred[3];
    int mcus_x;
    unsigned char *pix;
};

static int jd_byte(struct jd *j) {
    if (j->pos >= j->n) return JD_BAD;
    return j->in[j->pos++];
}

static uint16_t jd_be16(struct jd *j) {
    if (j->pos + 2 > j->n) return 0;
    uint16_t v = (uint16_t)((j->in[j->pos] << 8) | j->in[j->pos + 1]);
    j->pos += 2;
    return v;
}

/* Read the next marker code (skipping 0xFF fill bytes). */
static int jd_marker(struct jd *j) {
    int b = jd_byte(j);
    if (b != 0xFF) return JD_BAD;
    do {
        b = jd_byte(j);
        if (b < 0) return JD_BAD;
    } while (b == 0xFF);
    if (b == 0x00) return JD_BAD;   /* fill byte outside scan data */
    return b;
}

static int jd_dqt(struct jd *j, uint16_t len) {
    uint16_t end = j->pos + len - 2;
    if (len < 2 + 65 || end > j->n) return JD_BAD;
    while (j->pos + 1 < end) {
        int pq = j->in[j->pos] >> 4, tq = j->in[j->pos] & 15;
        j->pos++;
        if (pq > 1 || tq > 1) return JD_BAD;
        if (pq == 0) {
            if (j->pos + 64 > end) return JD_BAD;
            for (int i = 0; i < 64; i++) j->qtab[tq][i] = j->in[j->pos++];
        } else {
            if (j->pos + 128 > end) return JD_BAD;
            for (int i = 0; i < 64; i++) {
                j->qtab[tq][i] = (int16_t)((j->in[j->pos] << 8) | j->in[j->pos + 1]);
                j->pos += 2;
            }
        }
        j->have_q[tq] = 1;
    }
    return 0;
}

/* Build a decode LUT of 1<<maxlen entries; LUT_INVALID = invalid code. */
static int huff_build(struct jd *j, int tbl, uint16_t len) {
    int cnt[16], n = 0, maxlen = 0;
    uint16_t end = j->pos + len - 2;
    if (len < 2 + 1 + 16 + 1 || end > j->n) return JD_BAD;
    j->pos++;                              /* TcTh, id is implicit in tbl */
    for (int i = 0; i < 16; i++) {
        cnt[i] = j->in[j->pos++];
        if (cnt[i]) maxlen = i + 1;
        n += cnt[i];
    }
    if (n > 256 || j->pos + n > end) return JD_BAD;
    unsigned long long kraft = 0;
    for (int L = 1; L <= 16; L++) kraft += (unsigned long long)cnt[L - 1] << (16 - L);
    if (kraft > (1ULL << 16)) return JD_BAD;
    uint16_t *lut = malloc(((size_t)1 << maxlen) * 2);
    if (!lut) return JD_BAD;
    for (unsigned i = 0; i < (1u << maxlen); i++) lut[i] = LUT_INVALID;
    int code = 0;
    for (int L = 1; L <= 16; L++) {
        for (int i = 0; i < cnt[L - 1]; i++) {
            unsigned s = j->in[j->pos++];
            unsigned start = (unsigned)code << (maxlen - L);
            unsigned span = 1u << (maxlen - L);
            unsigned e = s | ((unsigned)L << 8);   /* symbol + code length */
            for (unsigned x = start; x < start + span; x++) lut[x] = (uint16_t)e;
            code++;
        }
        code <<= 1;
    }
    j->lut[tbl] = lut;
    j->lutlen[tbl] = maxlen;
    return 0;
}

static int jd_sof(struct jd *j, uint16_t len) {
    uint16_t end = j->pos + len - 2;
    if (len < 2 + 6 + 3 || end > j->n) return JD_BAD;
    int prec = j->in[j->pos];
    uint32_t h = (uint32_t)j->in[j->pos + 1] << 8 | j->in[j->pos + 2];
    uint32_t w = (uint32_t)j->in[j->pos + 3] << 8 | j->in[j->pos + 4];
    int ncomp = j->in[j->pos + 5];
    j->pos += 6;
    if (prec != 8 || !w || !h || w > 8192 || h > 8192) return JD_BAD;
    if (ncomp != 1 && ncomp != 3) return JD_BAD;
    if (len != 2 + 6 + 3 * ncomp || j->pos + 3 * ncomp > end) return JD_BAD;
    j->w = w;
    j->h = h;
    j->ncomp = ncomp;
    j->hmax = j->vmax = 0;
    for (int c = 0; c < ncomp; c++) {
        int id = j->in[j->pos];
        int hv = j->in[j->pos + 1];
        int tq = j->in[j->pos + 2];
        j->pos += 3;
        if (id < 1 || id > 3 || tq > 1 || !j->have_q[tq]) return JD_BAD;
        int H = hv >> 4, V = hv & 15;
        if (H < 1 || H > 2 || V < 1 || V > 2) return JD_BAD;
        if (ncomp == 1 && hv != 0x11) return JD_BAD;
        if (ncomp == 3 && !((H == 2 && V == 2) || (H == 2 && V == 1) || (H == 1 && V == 1)))
            return JD_BAD;
        j->hv[id - 1] = hv;
        j->qsel[id - 1] = tq;
        if (H > j->hmax) j->hmax = H;
        if (V > j->vmax) j->vmax = V;
    }
    return 0;
}

static int jd_sos(struct jd *j, uint16_t len) {
    uint16_t end = j->pos + len - 2;
    if (len < 2 + 1 + 2 || end > j->n) return JD_BAD;
    int nscan = j->in[j->pos++];
    if (nscan != j->ncomp || nscan < 1 || nscan > 3) return JD_BAD;
    if (len != 2 + 1 + 2 * nscan + 3) return JD_BAD;    /* baseline layout */
    j->nscan = nscan;
    for (int c = 0; c < nscan; c++) {
        int id = j->in[j->pos];
        int tb = j->in[j->pos + 1];
        j->pos += 2;
        if (id < 1 || id > 3 || !j->hv[id - 1]) return JD_BAD;
        int dc = tb >> 4, ac = tb & 15;
        if (dc > 1 || ac > 1 || !j->have_ht[dc] || !j->have_ht[2 + ac]) return JD_BAD;
        j->scid[c] = id - 1;
        j->sdc[c] = dc;
        j->sac[c] = 2 + ac;
    }
    if (j->pos + 3 > end) return JD_BAD;
    int ss = j->in[j->pos], se = j->in[j->pos + 1], ahal = j->in[j->pos + 2];
    j->pos += 3;
    if (ss != 0 || se != 63 || ahal != 0) return JD_BAD;   /* baseline only */
    return 0;
}

static int jd_dri(struct jd *j, uint16_t len) {
    if (len != 4 || j->pos + 2 > j->n) return JD_BAD;
    j->dri = j->in[j->pos] << 8 | j->in[j->pos + 1];
    j->pos += 2;
    return 0;
}

/* Refill the bit buffer from the stream, handling FF 00 stuffing and
 * scan-data markers (which stop the refill; j->mark records them). */
static int jd_refill(struct jd *j) {
    while (j->bitcnt <= 56 && !j->mark) {
        int b = jd_byte(j);
        if (b < 0) return JD_BAD;
        if (b == 0xFF) {
            int b2 = jd_byte(j);
            if (b2 < 0) return JD_BAD;
            if (b2 == 0x00) {                /* stuffed FF data byte */
                j->bits = (j->bits << 8) | 0xFFu;
                j->bitcnt += 8;
            } else if (b2 == 0xFF) {         /* padded FF FF */
                j->pos--;
                j->bits = (j->bits << 8) | 0xFFu;
                j->bitcnt += 8;
            } else {
                j->mark = b2;                /* restart / EOI / other */
                break;
            }
        } else {
            j->bits = (j->bits << 8) | (unsigned)b;
            j->bitcnt += 8;
        }
    }
    /* a marker blocks further bytes, but short-code LUT lookups still need a
     * full window: pad with 1s (the byte-align convention). Prefix-free codes
     * keep any window inside its own subtree, so this cannot mis-decode. */
    if (j->mark && j->bitcnt < 56) {
        int left = 56 - j->bitcnt;
        j->bits = (j->bits << left) | ((1ULL << left) - 1);
        j->bitcnt = 56;
    }
    return 0;
}

static int jd_bits(struct jd *j, int k) {
    if (j->bitcnt < k && jd_refill(j) < 0) return JD_BAD;
    if (j->bitcnt < k) return JD_BAD;        /* marker blocked the stream */
    unsigned v = (unsigned)(j->bits >> (j->bitcnt - k)) & ((1u << k) - 1);
    j->bitcnt -= k;
    return (int)v;
}

static int jd_sym(struct jd *j, int tbl) {
    int L = j->lutlen[tbl];
    if (j->bitcnt < L && jd_refill(j) < 0) return JD_BAD;
    if (j->bitcnt < L) return JD_BAD;
    unsigned idx = (unsigned)(j->bits >> (j->bitcnt - L)) & ((1u << L) - 1);
    unsigned v = j->lut[tbl][idx];
    if (v == LUT_INVALID) return JD_BAD;
    j->bitcnt -= v >> 8;              /* consume the actual code length */
    return (int)(v & 0xFF);
}

static int rnd13(int x) {
    return x >= 0 ? (x + 4096) >> 13 : -((-x + 4096) >> 13);
}

/* Two-pass separable fixed-point IDCT; block is natural (row-major) order,
 * dequantized coefficients in, 0..255 pixels out. */
static void idct8(int v[64]) {
    int t[64];
    for (int r = 0; r < 8; r++) {
        const int *row = v + 8 * r;
        int *o = t + 8 * r;
        for (int n = 0; n < 8; n++) {
            int sum = 0;
            for (int k = 0; k < 8; k++) sum += row[k] * T[k][n];
            o[n] = rnd13(sum);
        }
    }
    for (int c = 0; c < 8; c++) {
        for (int n = 0; n < 8; n++) {
            int sum = 0;
            for (int k = 0; k < 8; k++) sum += t[8 * k + c] * T[k][n];
            int out = rnd13(sum) + 128;
            v[8 * n + c] = out < 0 ? 0 : out > 255 ? 255 : out;
        }
    }
}

/* Decode one 8x8 block of scan component c into v (natural order). */
static int jd_block(struct jd *j, int c, int *v) {
    int sym = jd_sym(j, j->sdc[c]);
    if (sym < 0 || sym > 11) return JD_BAD;
    int diff = 0;
    if (sym > 0) {
        int m = jd_bits(j, sym);
        if (m < 0) return JD_BAD;
        diff = (m & (1 << (sym - 1))) ? m : m - ((1 << sym) - 1);
    }
    j->pred[j->scid[c]] += diff;
    const int16_t *q = j->qtab[j->qsel[j->scid[c]]];
    const int *zz = zigzag;
    memset(v, 0, 64 * sizeof *v);
    v[zz[0]] = j->pred[j->scid[c]] * q[zz[0]];
    int n = 1;
    while (n < 64) {
        sym = jd_sym(j, j->sac[c]);
        if (sym < 0) return JD_BAD;
        if (sym == 0x00) break;              /* EOB */
        if (sym == 0xF0) {                   /* ZRL: 16 zeros */
            n += 16;
            continue;
        }
        int run = sym >> 4, size = sym & 15;
        if (size == 0 || size > 10 || n + run >= 64) return JD_BAD;
        n += run;
        int m = jd_bits(j, size);
        if (m < 0) return JD_BAD;
        int val = (m & (1 << (size - 1))) ? m : m - ((1 << size) - 1);
        v[zz[n]] = val * q[zz[n]];
        n++;
    }
    idct8(v);
    return 0;
}

static int clamp16(int x) {
    int v = x >= 0 ? (x + 32768) >> 16 : -((-x + 32768) >> 16);
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* Write one MCU into the RGB output. Grayscale copies directly; color does
 * box upsampling of chroma (sample replicated hmax/H_c times) plus BT.601
 * YCbCr->RGB. Block c, grid (H_c x V_c) tiles the MCU rect; a block spans
 * 8*hmax/H_c output pixels per sample. */
static int jd_put_mcu(struct jd *j, int m, const int blk[3][4][64]) {
    int w = (int)j->w, h = (int)j->h;
    int mcu_x = (m % j->mcus_x) * 8 * j->hmax;
    int mcu_y = (m / j->mcus_x) * 8 * j->vmax;
    if (j->ncomp == 1) {
        for (int y = 0; y < 8 && mcu_y + y < h; y++)
            for (int x = 0; x < 8 && mcu_x + x < w; x++) {
                unsigned char *p = j->pix + ((uint32_t)(mcu_y + y) * (uint32_t)w + (uint32_t)(mcu_x + x)) * 3;
                unsigned char g = (unsigned char)blk[0][0][y * 8 + x];
                p[0] = p[1] = p[2] = g;
            }
        return 0;
    }
    for (int y = 0; y < 8 * j->vmax && mcu_y + y < h; y++) {
        for (int x = 0; x < 8 * j->hmax && mcu_x + x < w; x++) {
            int v[3];
            for (int c = 0; c < 3; c++) {
                int H = j->hv[c] >> 4, V = j->hv[c] & 15;
                int stride_x = 8 * j->hmax / H, stride_y = 8 * j->vmax / V;
                int bx = x / stride_x, by = y / stride_y;
                int sx = (x % stride_x) / (j->hmax / H);
                int sy = (y % stride_y) / (j->vmax / V);
                v[c] = blk[c][by * H + bx][sy * 8 + sx];
            }
            unsigned char *p = j->pix + ((uint32_t)(mcu_y + y) * (uint32_t)w + (uint32_t)(mcu_x + x)) * 3;
            int cb = v[1] - 128, cr = v[2] - 128;
            p[0] = (unsigned char)clamp16((v[0] << 16) + 91881 * cr);
            p[1] = (unsigned char)clamp16((v[0] << 16) - 22554 * cb - 46802 * cr);
            p[2] = (unsigned char)clamp16((v[0] << 16) + 116130 * cb);
        }
    }
    return 0;
}

/* Handle a marker that appeared inside the scan data. */
static int jd_scan_marker(struct jd *j, int mcu, int *done) {
    int mk = j->mark;
    j->mark = 0;
    if (mk >= 0xD0 && mk <= 0xD7) {          /* restart */
        if (!j->dri || mcu % j->dri != 0 || mcu == 0) return JD_BAD;
        if (mk != 0xD0 + ((mcu / j->dri - 1) & 7)) return JD_BAD;
        memset(j->pred, 0, sizeof j->pred);
        j->bits = 0;
        j->bitcnt = 0;
        return 0;
    }
    if (mk == 0xD9) {                        /* EOI before all MCUs */
        *done = 1;
        return 0;
    }
    return JD_BAD;                           /* unexpected marker */
}

static int jd_scan(struct jd *j) {
    int total = (int)(((uint64_t)j->w + 8 * (uint64_t)j->hmax - 1) / (8 * (uint64_t)j->hmax))
              * (int)(((uint64_t)j->h + 8 * (uint64_t)j->vmax - 1) / (8 * (uint64_t)j->vmax));
    j->mcus_x = (int)(((uint64_t)j->w + 8 * (uint64_t)j->hmax - 1) / (8 * (uint64_t)j->hmax));
    memset(j->pred, 0, sizeof j->pred);
    j->bits = 0;
    j->bitcnt = 0;
    j->mark = 0;
    j->eoi_seen = 0;
    int blk[3][4][64];
    for (int m = 0; m < total; m++) {
        if (j->mark) {
            int done = 0;
            if (jd_scan_marker(j, m, &done) < 0) return JD_BAD;
            if (done) return JD_BAD;         /* EOI in the middle of the image */
        }
        for (int c = 0; c < j->nscan; c++) {
            int ci = j->scid[c];
            int H = j->hv[ci] >> 4, V = j->hv[ci] & 15;
            for (int by = 0; by < V; by++)
                for (int bx = 0; bx < H; bx++)
                    if (jd_block(j, c, blk[c][by * H + bx]) < 0) return JD_BAD;
        }
        if (jd_put_mcu(j, m, blk) < 0) return JD_BAD;
    }
    /* if the bit reader stopped short of a marker (padding bits remain),
     * skip trailing entropy data until the next real marker */
    while (!j->mark) {
        int b = jd_byte(j);
        if (b < 0) return JD_BAD;        /* truncated: no EOI */
        if (b == 0xFF) {
            int b2 = jd_byte(j);
            if (b2 < 0) return JD_BAD;
            if (b2 == 0x00) continue;    /* stuffed data FF */
            j->mark = b2;
        }
    }
    if (j->mark >= 0xD0 && j->mark <= 0xD7 && j->dri && total % j->dri == 0)
        return 0;                        /* trailing restart, harmless */
    if (j->mark == 0xD9) {
        j->eoi_seen = 1;                 /* EOI consumed here */
        return 0;
    }
    return JD_BAD;                       /* unexpected marker */
}

int jpg_to_rgb(const unsigned char *in, size_t n,
               unsigned char **pix, uint32_t *w, uint32_t *h) {
    *pix = NULL;
    *w = *h = 0;
    struct jd j;
    memset(&j, 0, sizeof j);
    j.in = in;
    j.n = n;
    if (n < 4 || in[0] != 0xFF || in[1] != 0xD8) return JD_BAD;
    j.pos = 2;
    int saw_sof = 0, saw_sos = 0;
    unsigned char *out = NULL;
    for (;;) {
        int mk = jd_marker(&j);
        if (mk < 0) goto fail;
        if (mk == 0xD9) break;               /* EOI */
        if (mk == 0xDA) {                    /* SOS: parse then decode scan */
            uint16_t len = jd_be16(&j);
            if (!saw_sof || saw_sos || jd_sos(&j, len) < 0) goto fail;
            saw_sos = 1;
            if (!out) goto fail;
            j.pix = out;
            if (jd_scan(&j) < 0) goto fail;
            if (j.eoi_seen) break;           /* EOI consumed inside the scan */
            continue;
        }
        if (mk == 0xC0) {                    /* SOF0 baseline */
            if (saw_sof) goto fail;
            uint16_t len = jd_be16(&j);
            if (jd_sof(&j, len) < 0) goto fail;
            saw_sof = 1;
            uint64_t npix = (uint64_t)j.w * j.h * 3;
            if (npix > 0xFFFFFFFFull) goto fail;
            out = malloc((size_t)npix);
            if (!out) goto fail;
            continue;
        }
        if (mk == 0xC1 || mk == 0xC2 || mk == 0xC3 || mk == 0xC5 || mk == 0xC6 ||
            mk == 0xC7 || mk == 0xC9 || mk == 0xCA || mk == 0xCB || mk == 0xCD ||
            mk == 0xCE || mk == 0xCF)
            goto fail;                       /* non-baseline SOF */
        uint16_t len = jd_be16(&j);
        if (len < 2 || j.pos + len - 2 > j.n) goto fail;
        uint16_t end = j.pos + len - 2;
        if (mk == 0xDB) { if (jd_dqt(&j, len) < 0) goto fail; }
        else if (mk == 0xC4) {
            if (len < 2 + 1 + 16 + 1) goto fail;
            int tc = j.in[j.pos] >> 4, tid = j.in[j.pos] & 15;
            if (tc > 1 || tid > 1) goto fail;
            if (huff_build(&j, tc * 2 + tid, len) < 0) goto fail;
            j.have_ht[tc * 2 + tid] = 1;
        } else if (mk == 0xDD) { if (jd_dri(&j, len) < 0) goto fail; }
        j.pos = end;
    }
    if (!saw_sof || !saw_sos) goto fail;
    *pix = out;
    *w = j.w;
    *h = j.h;
    for (int i = 0; i < 4; i++) free((void *)j.lut[i]);
    return 0;
fail:
    for (int i = 0; i < 4; i++) free((void *)j.lut[i]);
    free(out);
    return JD_BAD;
}
