/* SPDX-License-Identifier: GPL-2.0-or-later */
/* G-EQ for the Digitakt mk1 (core 4.2, OS 1.54): a four-band EQ for the whole mix after the SSL G-series
 * console's channel EQ, on a master page (FUNC + LFO). Main, headphones and USB, in stereo.
 *
 *   A  LF    low band's gain, -15 to +15 dB        B  HZ   its frequency, 30-450 Hz
 *   C  LMF   low mid's gain                        D  HZ   200 Hz - 2.5 kHz
 *   E  HMF   high mid's gain                       F  HZ   600 Hz - 7 kHz
 *   G  HF    high band's gain                      H  HZ   1.5 - 16 kHz
 *
 * Push a knob for the extras, as the G's buttons:
 *   B and H (the LF and HF frequencies)   BELL or shelf (the G's BELL buttons; a shelf to start)
 *   D and F (the mids' frequencies)       the width, Q: 0.7, 1.5, 3
 *   A (LF gain)                           the high pass: OFF, 30, 60, 120, 240 Hz
 *   E (HMF gain)                          the EQ IN / OUT
 * The bells are "proportional Q", as on the G: the more they boost or cut, the narrower.
 *
 * The page draws the EQ curve, with each band's number where its handle is; the band you touch shows its
 * settings in words. Saved.
 *
 * The filters are state-variable ones in the "topology preserving" form (Zavalishin; the bell and shelf
 * mixes after Andrew Simper's), stable and in tune right down to 30 Hz in 32-bit integers: samples Q24,
 * the filter's coefficients Q15, its output mix Q12. */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "../common/heapbuf.h"
#include "../common/mpui.h"
#include "tables.h"
#include "../common/fmul.h"

enum { K_LFG, K_LFF, K_LMG, K_LMF, K_HMG, K_HMF, K_HFG, K_HFF, KNOBS };
static const char *const kname[KNOBS] = { "LF", "HZ", "LMF", "HZ", "HMF", "HZ", "HF", "HZ" };
/* the bands' ranges in the 20 Hz - 20 kHz table: 30-450, 200-2.5k, 600-7k, 1.5k-16k */
static const uint8_t f_lo[4] = { 15, 85, 126, 159 }, f_hi[4] = { 115, 178, 216, 247 };
static const int8_t kdef[KNOBS] = { 64, 40, 64, 64, 64, 64, 64, 64 };
static volatile int8_t kv[KNOBS] = { 64, 40, 64, 64, 64, 64, 64, 64 };
/* the extras: [0] LF bell, [1] LMF Q, [2] HMF Q, [3] HF bell, [4] the high pass, [5] OUT */
enum { X_LFB, X_LMQ, X_HMQ, X_HFB, X_HPF, X_OUT, XS };
static const uint8_t xmax[XS] = { 1, 2, 2, 1, 4, 1 };
static volatile int8_t xv[XS] = { 0, 1, 1, 0, 0, 0 };
static const uint8_t hpf_idx[5] = { 0, 15, 41, 66, 92 };   /* OFF, 30, 60, 120, 240 Hz */
static const int16_t q_k[3] = { 23170, 10923, 5461 };      /* 1 / Q, Q14: Q 0.7, 1.5, 3 */
#define clampi ui_clamp

static inline int32_t mq15(int32_t a, int32_t x) { return a * (x >> 15) + ((a * (x & 0x7fff)) >> 15); }
static inline int32_t mq12(int32_t m, int32_t x) { return m * (x >> 12) + ((m * (x & 0xfff)) >> 12); }

/* ---- the filters ---------------------------------------------------------------------------------------- */
struct coef { int32_t a1, a2, a3, m0, m1, m2, on; };       /* worked out in Q15 / Q12, kept as a Q31, m Q28 */
#define ONE28 (1 << 28)
struct svf { int32_t ic1, ic2; };
static struct coef cf[2][5];                       /* two sets, swapped whole: the tick fills one */
static volatile int32_t cf_cur;
static struct svf st[5][2];                        /* [band: LF LMF HMF HF HPF][channel] */
static volatile int32_t eq_on;

/* One band over the block, both channels, its numbers and states in registers. The filter is stable as it
 * is (the samples are clamped going in and out), so no clamps inside. In fast SRAM. */
static inline __attribute__((always_inline)) void ge_band_body(int32_t *p, const struct coef *k, struct svf *s)
{
    const int32_t a1 = k->a1, a2 = k->a2, a3 = k->a3, m0 = k->m0, m1 = k->m1, m2 = k->m2;
    int32_t l1 = s[0].ic1, l2 = s[0].ic2, r1 = s[1].ic1, r2 = s[1].ic2;
    for (int32_t n = 0; n < 32; n++, p += 2) {      /* left and right side by side (ACC0, ACC1) */
        int32_t vl = p[0], vr = p[1], ul = vl - l2, ur = vr - r2, v1l, v1r, v2l, v2r, yl, yr;
        fmx2(a1, a2, l1, ul, r1, ur, v1l, v1r);
        fmx2(a2, a3, l1, ul, r1, ur, v2l, v2r);
        v2l += l2; v2r += r2;
        l1 = 2 * v1l - l1; l2 = 2 * v2l - l2;
        r1 = 2 * v1r - r1; r2 = 2 * v2r - r2;
        fmx3(m0, m1, m2, vl, v1l, v2l, vr, v1r, v2r, yl, yr);
        p[0] = yl << 3; p[1] = yr << 3;
    }
    s[0].ic1 = l1; s[0].ic2 = l2; s[1].ic1 = r1; s[1].ic2 = r2;
}

FAST_CODE static void ge_band_fast(int32_t *p, const struct coef *k, struct svf *s) { ge_band_body(p, k, s); }
__attribute__((noinline)) static void ge_band_ram(int32_t *p, const struct coef *k, struct svf *s) { ge_band_body(p, k, s); }
typedef void (*ge_band_fn)(int32_t *, const struct coef *, struct svf *);
static volatile ge_band_fn ge_band = ge_band_ram;     /* the fast one once it's checked to be there */

void ge_render(int32_t *pair)
{
    if (!eq_on) return;
    struct coef *c = cf[cf_cur];
    int32_t any = 0;
    for (int32_t b = 0; b < 5; b++) any |= c[b].on;
    if (!any) return;
    fm_start();
    for (int32_t i = 0; i < 64; i++) pair[i] >>= 7;          /* Q24 */
    for (int32_t b = 0; b < 5; b++)
        if (c[b].on) ge_band(pair, &c[b], st[b]);
    for (int32_t i = 0; i < 64; i++) pair[i] = clampi(pair[i], -(1 << 24), (1 << 24) - 1) << 7;
}

/* ---- the coefficients, worked out in the UI task when a knob moves -------------------------------------- */
/* 2^(v / 65536) in Q12 (v log2, Q16), within about 0.1 % */
static int32_t ex12(int32_t v)
{
    int32_t n = v >> 16, fq = (v & 0xffff) >> 1;
    int32_t t = 21512 + ((11256 * fq) >> 15);
    int32_t m = 32768 + ((fq * t) >> 15);                    /* 2^f, Q15 */
    n -= 3;                                                  /* to Q12 */
    return n >= 0 ? m << n : m >> -n;
}
static int32_t gain10(int32_t band) { return clampi((kv[band * 2] - 64) * 150 / 63, -150, 150); }   /* tenths of a dB */
static int32_t freq_idx(int32_t band) { return f_lo[band] + kv[band * 2 + 1] * (f_hi[band] - f_lo[band]) / 127; }
static int32_t is_bell(int32_t band) { return band == 1 || band == 2 || (band == 0 && xv[X_LFB]) || (band == 3 && xv[X_HFB]); }

static void svf_coef(struct coef *c, int32_t g16, int32_t k14)
{
    int32_t g12 = g16 >> 4, den = 4096 + ((g12 * (g12 + (k14 >> 2))) >> 12);
    c->a1 = (int32_t)((4096u << 15) / (uint32_t)den);
    c->a2 = ((g16 >> 2) * (c->a1 >> 1)) >> 13;
    c->a3 = ((g16 >> 2) * (c->a2 >> 1)) >> 13;
}
static void band_coef(struct coef *c, int32_t band)
{
    int32_t db = gain10(band);
    c->on = db != 0;
    if (!c->on) return;
    int32_t la = db * 544, A = ex12(la), A2 = ex12(2 * la), sq = ex12(la / 2), g = (int32_t)df_g[freq_idx(band)];
    if (is_bell(band)) {                                     /* proportional Q: k = 1 / (Q A) */
        int32_t k14 = (band == 1 || band == 2 ? q_k[xv[band == 1 ? X_LMQ : X_HMQ]] : q_k[0]) * 4096 / A;
        svf_coef(c, g, k14);
        c->m0 = 4096; c->m1 = (k14 * (A2 - 4096)) >> 14; c->m2 = 0;
    } else if (band == 0) {                                  /* low shelf */
        svf_coef(c, g * 4096 / sq, 23170);
        c->m0 = 4096; c->m1 = (23170 * (A - 4096)) >> 14; c->m2 = A2 - 4096;
    } else {                                                 /* high shelf */
        svf_coef(c, (g >> 4) * sq >> 8, 23170);
        c->m0 = A2; c->m1 = (23170 * (((4096 - A) * A) >> 12)) >> 14; c->m2 = 4096 - A2;
    }
}
static int32_t dirty = 1;
static void update(void)
{
    struct coef *c = cf[cf_cur ^ 1];
    for (int32_t b = 0; b < 4; b++) band_coef(&c[b], b);
    int32_t h = hpf_idx[xv[X_HPF]];
    c[4].on = h != 0;
    if (h) { svf_coef(&c[4], (int32_t)df_g[h], 23170); c[4].m0 = 4096; c[4].m1 = -5793; c[4].m2 = -4096; }
    for (int32_t b = 0; b < 5; b++) {                         /* to the render's forms: a Q31, m Q28 */
        c[b].a1 = Q31(c[b].a1); c[b].a2 = Q31(c[b].a2); c[b].a3 = Q31(c[b].a3);
        c[b].m0 *= 65536; c[b].m1 *= 65536; c[b].m2 *= 65536;
    }
    cf_cur ^= 1;
    eq_on = !xv[X_OUT];
}

/* ---- saving -------------------------------------------------------------------------------------------- */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
HEAP_BUF(block, 512)
#define MAGIC 0x47455131u                            /* "GEQ1" */
static int32_t loaded, save_wait;
static uint32_t sum_of(const uint8_t *b) { uint32_t s = 7; for (int32_t i = 8; i < 8 + KNOBS + XS; i++) s = s * 31u + b[i]; return s; }
/* the settings go with the project (common/projsave.h, through PROJECT LINK): each slot's copy at
 * PJ_BASE + slot x PJ_NB */
#define PJ_BASE 0x62180
#define PJ_NB 1
#include "../common/projsave.h"
static void load(void)
{
    if (!block_ready() || EMMC_READ(SAVE_BLOCK, 512, block) != 0) return;
    uint32_t *w = (uint32_t *)block;
    if (w[0] != MAGIC || w[1] != sum_of(block)) return;
    for (int32_t k = 0; k < KNOBS; k++) kv[k] = (int8_t)clampi((int8_t)block[8 + k], 0, 127);
    for (int32_t k = 0; k < XS; k++) xv[k] = (int8_t)clampi((int8_t)block[8 + KNOBS + k], 0, xmax[k]);
    dirty = 1;
}
static void save(void)
{
    if (!block_ready()) return;
    for (int32_t i = 0; i < 512; i++) block[i] = 0;
    for (int32_t k = 0; k < KNOBS; k++) block[8 + k] = (uint8_t)kv[k];
    for (int32_t k = 0; k < XS; k++) block[8 + KNOBS + k] = (uint8_t)xv[k];
    uint32_t *w = (uint32_t *)block;
    w[0] = MAGIC; w[1] = sum_of(block);
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}
static void pj_defaults(void) { static const int8_t xdef[XS] = { 0, 1, 1, 0, 0, 0 }; for (int32_t k = 0; k < KNOBS; k++) kv[k] = kdef[k]; for (int32_t k = 0; k < XS; k++) xv[k] = xdef[k]; dirty = 1; }

/* ---- the page: built small ----------------------------------------------------------------------------- */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns", "no-optimize-strlen")
static int32_t turned[KNOBS], hl_k = -1, hl_ticks, last_band = -1, word_ticks;
static char *cat(char *b, const char *s) { while (*s) *b++ = *s++; *b = 0; return b; }
static char *hz_text(char *b, int32_t idx)
{
    int32_t hz = df_hz[idx];
    if (hz < 1000) return ui_num(b, hz);
    b = ui_num(b, hz / 1000);
    if (hz < 10000) { *b++ = '.'; b = ui_num(b, hz / 100 % 10); }
    return cat(b, "K");
}
static char *db_text(char *b, int32_t d10)
{
    if (d10 < 0) { *b++ = '-'; d10 = -d10; } else *b++ = '+';
    b = ui_num(b, d10 / 10); *b++ = '.';
    return ui_num(b, d10 % 10);
}
static void val_text(char *b, int32_t k)
{
    int32_t band = k / 2;
    if (k & 1) hz_text(b, freq_idx(band));
    else { int32_t d = gain10(band); if (d > -10 && d < 10) cat(b, "0"); else { b = db_text(b, d); b[-2] = 0; } }
}

/* ---- the layout (y 0 the bottom row; the title bar is 53-63, the core's) ----
 *   46-50  the readout: the band you touched, in words; otherwise IN / OUT and the high pass
 *   28-44  the curve, 20 Hz - 20 kHz across 120 columns; 0 dB at 36, a dot for every 2 dB; the bands'
 *          numbers at their handles
 *   21-25  the knobs' names;  7-19 their dials;  0-5 their values */
#define GX0 4
#define GY 36
static int32_t band_db(int32_t band, int32_t at)          /* the band's part at table index at, in tenths of a dB */
{
    int32_t db = gain10(band), f = freq_idx(band), d = at - f;
    if (!db) return 0;
    if (is_bell(band)) {
        static const int16_t q100[3] = { 70, 150, 300 };
        int32_t q = band == 1 || band == 2 ? q100[xv[band == 1 ? X_LMQ : X_HMQ]] : 70;
        int32_t z = d * q * 3 / 52;                       /* about 1.5 Q octaves, x 100 */
        return db * 10000 / (10000 + z * z);
    }
    int32_t t = clampi((band == 0 ? -d : d) + 26, 0, 52);    /* a shelf: over an octave either side */
    t = t * t * (156 - 2 * t) / (52 * 52);                   /* smoothstep, 0 .. 52 */
    return db * t / 52;
}
static void ge_draw(void *view, void *bmp)
{
    char s[28], *p;
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    int32_t out = xv[X_OUT];
    if (last_band >= 0 && word_ticks > 0) {
        static const char *const bn[4] = { "LF ", "LMF ", "HMF ", "HF " };
        int32_t b = last_band;
        p = cat(s, bn[b]); p = hz_text(p, freq_idx(b)); *p++ = ' '; p = db_text(p, gain10(b)); p = cat(p, "DB ");
        if (b == 1 || b == 2) { static const char *const q[3] = { "Q.7", "Q1.5", "Q3" }; cat(p, q[xv[b == 1 ? X_LMQ : X_HMQ]]); }
        else cat(p, is_bell(b) ? "BELL" : "SHELF");
        ui_text(bmp, 2, 46, s);
    } else {
        ui_text(bmp, 2, 46, out ? "EQ OUT" : "EQ IN");
        if (xv[X_HPF]) { static const char *const hn[5] = { "", "30", "60", "120", "240" }; p = cat(s, "HPF "); cat(p, hn[xv[X_HPF]]); ui_text(bmp, 126 - 4 * ui_len(s), 46, s); }
    }
    /* the 0 dB line, dotted; 100 Hz, 1 kHz, 10 kHz marks */
    for (int32_t x = 0; x < 120; x += 3) ui_dot(bmp, GX0 + x, GY);
    static const uint8_t grid[3] = { 59, 144, 229 };
    for (int32_t g = 0; g < 3; g++) for (int32_t y = 28; y <= 44; y += 4) ui_dot(bmp, GX0 + grid[g] * 120 / 256, y);
    /* the curve */
    int32_t prev = -99;
    for (int32_t x = 0; x < 120; x++) {
        int32_t at = x * 255 / 119, d = 0;
        if (!out) {
            for (int32_t b = 0; b < 4; b++) d += band_db(b, at);
            int32_t h = hpf_idx[xv[X_HPF]];
            if (h && at < h) d -= 30 + (h - at) * 120 / 26;      /* 12 dB an octave below it */
            else if (h && at < h + 13) d -= 30 * (h + 13 - at) / 13;
        }
        int32_t y = clampi(GY + d / 20, 28, 44);
        int32_t a = prev == -99 ? y : prev < y ? prev : y, z = prev == -99 ? y : prev > y ? prev : y;
        fw_fillrect(bmp, GX0 + x, a, GX0 + x, z, 1);
        prev = y;
    }
    /* the handles: each band's number at its frequency and gain; the one you touched lit */
    if (!out)
        for (int32_t b = 0; b < 4; b++) {
            int32_t x = GX0 + freq_idx(b) * 119 / 255 - 1, y = clampi(GY + gain10(b) / 20, 28, 44);
            y = y > GY ? y + 2 : y - 6;                       /* above a boost, under a cut */
            y = clampi(y, 27, 39);                            /* clear of the readout */
            char n[2] = { (char)('1' + b), 0 };
            fw_fillrect(bmp, x - 1, y - 1, x + 3, y + 5, 0);
            ui_text(bmp, x, y, n);
            if (b == last_band && word_ticks > 0) fw_fillrect(bmp, x - 1, y - 1, x + 3, y + 5, -1);
        }
    for (int32_t x = 0; x < 128; x += 2) ui_dot(bmp, x, 26);
    for (int32_t k = 0; k < KNOBS; k++) {
        val_text(s, k);
        ui_knob(bmp, k, kname[k], s, kv[k], 127, k & 1 ? 0 : 1, k == hl_k && hl_ticks > 0);
    }
}

static int32_t ge_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > KNOBS) return 0;
    int32_t k = knob - 1;
    hl_k = k; hl_ticks = 40; last_band = k / 2; word_ticks = 150;
    if ((turned[k] > 0) != (turn > 0)) turned[k] = 0;
    turned[k] += turn;
    while (turned[k] >= 4 || turned[k] <= -4) {
        int32_t dir = turned[k] > 0 ? 1 : -1;
        turned[k] -= dir * 4;
        kv[k] = (int8_t)clampi(kv[k] + dir, 0, 127);
    }
    dirty = 1; save_wait = 120;
    fw_invalidate(view);
    return 1;
}
static void ge_clear(void *view)
{
    for (int32_t k = 0; k < KNOBS; k++) { kv[k] = kdef[k]; turned[k] = 0; }
    dirty = 1; save_wait = 120;
    fw_invalidate(view);
}
/* the knobs' pushes (keys 40-47) for the extras; LEFT / RIGHT the stock page step */
static int32_t ge_key(void *view, void *event)
{
    int32_t key = *(int32_t *)((char *)event + 12), fl = *(int32_t *)((char *)event + 16);
    if (key >= 40 && key <= 47) {
        static const int8_t xk[8] = { X_HPF, X_LFB, -1, X_LMQ, X_OUT, X_HMQ, -1, X_HFB };
        int32_t x = xk[key - 40];
        if (x < 0) return 0;
        if ((fl & 1) && !(fl & 0x18)) {
            xv[x] = (int8_t)(xv[x] >= xmax[x] ? 0 : xv[x] + 1);
            hl_k = key - 40; hl_ticks = 40;
            last_band = x == X_LFB ? 0 : x == X_LMQ ? 1 : x == X_HMQ ? 2 : x == X_HFB ? 3 : -1;
            word_ticks = last_band >= 0 ? 150 : 0;
            dirty = 1; save_wait = 120;
            fw_invalidate(view);
        }
        return 1;
    }
    return mp_key(view, event);
}

struct core_mpage ge_page;
void ge_tick(void *ctrl)
{
    static uint32_t t;
    (void)ctrl;
    t++;
    if (t % 32 == 1) ge_band = FAST_PICK(ge_band_fast, ge_band_ram);   /* fast SRAM while it holds our code */
    if (!loaded && t > 50) { loaded = 1; load(); }
    if (loaded && pj_tick()) dirty = 1;              /* a project loaded or saved */
    if (dirty) { dirty = 0; update(); }
    if (save_wait > 0 && --save_wait == 0) save();
    if (hl_ticks > 0) hl_ticks--;
    if (word_ticks > 0 && --word_ticks == 0 && core_mview && core_mpage_shown(core_mview) == &ge_page)
        fw_invalidate(core_mview);                   /* the words go back to IN / OUT */
}

struct core_mpage ge_page = { 99, "GEQ", "G-EQ", ge_draw, ge_key, ge_enc, 0, 0, ge_clear };
