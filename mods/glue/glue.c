/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GLUE for the Digitakt mk1 (core 4.2, OS 1.54): a bus compressor for the whole mix after the SSL G-series
 * console's centre-section compressor (the "glue" one), on a master page (FUNC + LFO). Main, headphones and
 * USB, in stereo, the two sides linked (one gain for both, so the image stays put).
 *
 *   A  THR   threshold, -40 to 0 dB
 *   B  RAT   ratio: 2, 4, 10 (the G's three)
 *   C  ATK   attack: .1 .3 1 3 10 30 ms (the G's six)
 *   D  REL   release: .1 .3 .6 1.2 s, or AUTO (the G's program-dependent one: quick after a hit, slow
 *            when it's been squeezing a while)
 *   E  MKP   make-up gain, 0 to +15 dB
 *   F  MIX   dry to compressed (parallel compression; the G has none, 100 % is the console)
 *   G  SCF   a high pass on what it listens to, so the kick doesn't pump it all: OFF, 60, 90, 150 Hz
 *   H  CMP   the IN / OUT switch
 *
 * The page has the G's moving-needle gain reduction meter, and the curve: what comes out for what goes in,
 * with a dot where the mix is right now. Saved.
 *
 * In 32-bit integers (no 64-bit multiply here): samples Q24 (the Q31 mix / 128); levels and the gain
 * reduction in log2, Q16 (6.02 dB to the unit); gains Q15 with a shift. */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "../common/heapbuf.h"
#include "../common/mpui.h"
#include "../common/fmul.h"

enum { K_THR, K_RAT, K_ATK, K_REL, K_MKP, K_MIX, K_SCF, K_CMP, KNOBS };
static const char *const kname[KNOBS] = { "THR", "RAT", "ATK", "REL", "MKP", "MIX", "SCF", "CMP" };
static const int8_t kdef[KNOBS] = { 89, 0, 4, 4, 0, 127, 0, 0 };
static const int8_t kmax[KNOBS] = { 127, 2, 5, 4, 127, 127, 3, 1 };
static volatile int8_t kv[KNOBS] = { 89, 0, 4, 4, 0, 127, 0, 0 };
#define clampi ui_clamp

static inline int32_t mq15(int32_t a, int32_t x) { return a * (x >> 15) + ((a * (x & 0x7fff)) >> 15); }

/* ---- the sound ----------------------------------------------------------------------------------------- */
#define LG 65536                                     /* log2 1, Q16 */
#define DB(x) ((int32_t)((x) * 10886.0))             /* dB to log2 Q16: 65536 / 6.0206 */
/* the smoothing, per group of 4 frames (12 kHz): 1 - exp(-t / tau), Q15 */
static const int16_t a_atk[6] = { 18350, 7853, 2621, 889, 272, 91 };   /* .1 .3 1 3 10 30 ms */
static const int16_t a_rel[4] = { 27, 9, 5, 2 };                       /* .1 .3 .6 1.2 s */
static const int16_t a_scf[4] = { 0, 256, 384, 637 };                  /* the side chain's high pass */
static const uint8_t ratio[3] = { 2, 4, 10 };

static int32_t sc_lp[2];                             /* the side chain's high pass: a low pass taken away */
static int32_t gr;                                   /* the gain reduction now, log2 Q16 (>= 0) */
static volatile int32_t shown_gr, shown_in = -20 * LG, on_now;

static inline int32_t l2q16(uint32_t x)                     /* log2(x), Q16; straight between powers of two */
{
    if (!x) return -32 * LG;
    int32_t n = 31 - __builtin_clz(x);
    uint32_t f = n >= 16 ? (x >> (n - 16)) & 0xffff : (x << (16 - n)) & 0xffff;
    /* a quadratic bend makes it within 0.01 of the true log2 */
    int32_t fb = (int32_t)f + (int32_t)((f * (0x10000u - f)) >> 18) * 4 / 3;
    return n * LG + fb;
}
/* 2^(v) for v log2 Q16 as a Q15 mantissa (16384-32767) and a shift: gain = mant / 32768 * 2^(sh + 1) */
static inline void exp2q(int32_t v, int32_t *mant, int32_t *sh)
{
    int32_t n = v >> 16, fq = (v & 0xffff) >> 1;    /* fq: the fraction, Q15 */
    int32_t t = 21512 + ((11256 * fq) >> 15);        /* 2^f ~ 1 + f (0.6565 + 0.3435 f) */
    *mant = (32768 + ((fq * t) >> 15)) >> 1;
    *sh = n;
}
static inline int32_t apply(int32_t x, int32_t mant, int32_t sh)   /* mant Q31 */
{
    int32_t y = fm(mant, x);
    sh += 1;
    return sh >= 0 ? (sh > 4 ? y << 4 : y << sh) : (sh < -24 ? 0 : y >> -sh);
}

/* (GLUE's work is light, a multiply a sample: it stays in RAM and leaves the fast SRAM to G-EQ and SONIC
 * MAX, whose filters run many) */
static void gl_run_body(int32_t *pair);
__attribute__((noinline)) static void gl_run(int32_t *pair) { gl_run_body(pair); }
void gl_render(int32_t *pair)
{
    if (!kv[K_CMP]) {
        if (on_now) { gr = 0; sc_lp[0] = sc_lp[1] = 0; on_now = 0; }
        shown_gr = 0;
        return;
    }
    on_now = 1;
    gl_run(pair);
}
static inline __attribute__((always_inline)) void gl_run_body(int32_t *pair)
{
    int32_t thr = DB(-40) + kv[K_THR] * (DB(40) / 127), r = ratio[kv[K_RAT]];
    int32_t aa = a_atk[kv[K_ATK]], rel = kv[K_REL], scf = a_scf[kv[K_SCF]];
    int32_t mk = kv[K_MKP] * (DB(15) / 127), mix = kv[K_MIX];
    int32_t knee = DB(6), in_pk = 0, scf31 = Q31(scf);
    fm_start();
    for (int32_t g = 0; g < 8; g++) {                /* 8 groups of 4 frames */
        int32_t *p = pair + g * 8, peak = 0;
        for (int32_t i = 0; i < 8; i++) {
            int32_t c = i & 1, x = p[i] >> 7;
            if (scf) { sc_lp[c] += fm(scf31, x - sc_lp[c]); x -= sc_lp[c]; }
            if (x < 0) x = -x;
            if (x > peak) peak = x;
        }
        if (peak > in_pk) in_pk = peak;
        /* the curve: over the threshold by o, it takes o (1 - 1/r) off; a 6 dB soft knee around it */
        int32_t lvl = l2q16((uint32_t)peak) - 24 * LG, o = lvl - thr, want;
        if (o <= -knee / 2) want = 0;
        else if (o >= knee / 2) want = o - o / r;
        else { int32_t k = o + knee / 2; want = (int32_t)(((k >> 4) * (k >> 4)) / (2 * (knee >> 8))) * (r - 1) / r; }
        /* the needle: attack when it should squeeze more, release when less */
        int32_t a;
        if (want > gr) a = aa;
        else if (rel < 4) a = a_rel[rel];
        else {                                       /* AUTO: quick for a short hit, slow when it's been held */
            int32_t gap = gr - want;                 /* how far it has to let go: a lot after a hit */
            a = 2 + (gap > DB(6) ? 25 : gap * 25 / DB(6));
        }
        gr += mq15(a, want - gr);
        if (gr < 0) gr = 0;
        int32_t mant, sh;
        exp2q(mk - gr, &mant, &sh);
        mant = Q31(mant);
        for (int32_t i = 0; i < 8; i++) {
            int32_t dry = p[i] >> 7, y = apply(dry, mant, sh);
            if (mix < 127) y = dry + ((y - dry) >> 7) * mix;
            p[i] = clampi(y, -(1 << 24), (1 << 24) - 1) << 7;
        }
    }
    shown_gr = gr;
    shown_in = l2q16((uint32_t)in_pk) - 24 * LG;
}

/* ---- saving -------------------------------------------------------------------------------------------- */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
HEAP_BUF(block, 512)
#define MAGIC 0x474c5531u                            /* "GLU1" */
static int32_t loaded, save_wait;
static uint32_t sum_of(const uint8_t *b) { uint32_t s = 7; for (int32_t i = 8; i < 8 + KNOBS; i++) s = s * 31u + b[i]; return s; }
/* the settings go with the project (common/projsave.h, through PROJECT LINK): each slot's copy at
 * PJ_BASE + slot x PJ_NB */
#define PJ_BASE 0x62200
#define PJ_NB 1
#include "../common/projsave.h"
static void load(void)
{
    if (!block_ready() || EMMC_READ(SAVE_BLOCK, 512, block) != 0) return;
    uint32_t *w = (uint32_t *)block;
    if (w[0] != MAGIC || w[1] != sum_of(block)) return;
    for (int32_t k = 0; k < KNOBS; k++) kv[k] = (int8_t)clampi((int8_t)block[8 + k], 0, kmax[k]);
}
static void save(void)
{
    if (!block_ready()) return;
    for (int32_t i = 0; i < 512; i++) block[i] = 0;
    for (int32_t k = 0; k < KNOBS; k++) block[8 + k] = (uint8_t)kv[k];
    uint32_t *w = (uint32_t *)block;
    w[0] = MAGIC; w[1] = sum_of(block);
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}
static void pj_defaults(void) { for (int32_t k = 0; k < KNOBS; k++) kv[k] = kdef[k]; }

/* ---- the page: built small ----------------------------------------------------------------------------- */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns", "no-optimize-strlen")
static int32_t turned[KNOBS], hl_k = -1, hl_ticks;
static char *cat(char *b, const char *s) { while (*s) *b++ = *s++; *b = 0; return b; }
static int32_t db10(int32_t l2) { return l2 / 1089; }                /* log2 Q16 to tenths of a dB, about */
static char *dbt(char *b, int32_t d10, int32_t plus)                  /* "-4.5" */
{
    if (d10 < 0) { *b++ = '-'; d10 = -d10; } else if (plus) *b++ = '+';
    b = ui_num(b, d10 / 10); *b++ = '.';
    return ui_num(b, d10 % 10);
}

static void val_text(char *b, int32_t k)
{
    int32_t v = kv[k];
    switch (k) {
    case K_THR: ui_num(b, -40 + v * 40 / 127); return;
    case K_RAT: ui_num(b, ratio[v]); return;
    case K_ATK: { static const char *const t[6] = { ".1", ".3", "1", "3", "10", "30" }; cat(b, t[v]); return; }
    case K_REL: { static const char *const t[5] = { ".1", ".3", ".6", "1.2", "AUT" }; cat(b, t[v]); return; }
    case K_MKP: b = cat(b, "+"); ui_num(b, v * 15 / 127); return;
    case K_MIX: ui_num(b, v * 100 / 127); return;
    case K_SCF: { static const char *const t[4] = { "OFF", "60", "90", "150" }; cat(b, t[v]); return; }
    default: cat(b, v ? "IN" : "OUT"); return;
    }
}

/* ---- the layout (y 0 the bottom row; the title bar is 53-63, the core's) ----
 *   46-50  the readout: the gain reduction now, and the ratio
 *   28-44  left the meter (0 dB right, 20 dB left, the needle from the bottom); right the curve
 *   21-25  the knobs' names;  7-19 their dials;  0-5 their values */
static const int8_t arc[21][2] = { {13,7},{13,9},{12,11},{11,12},{9,13},{8,13},{7,14},{5,15},{3,16},{2,16},{0,16},
    {-2,16},{-3,16},{-5,15},{-7,14},{-8,13},{-9,13},{-11,12},{-12,11},{-13,9},{-13,7} };
#define MX 30                                        /* the meter's pivot */
#define MY 28
#define CX0 70                                       /* the curve's box: -48 .. 0 dB in, across; out, up */
#define CW 48
#define CY0 28
#define CH 16

static void gl_draw(void *view, void *bmp)
{
    char s[24], *p;
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    int32_t on = kv[K_CMP], g = on ? shown_gr : 0;

    p = cat(s, "GR "); p = dbt(p, -db10(g), 0); cat(p, "DB");
    ui_text(bmp, 2, 46, on ? s : "GLUE OUT");
    p = ui_num(s, ratio[kv[K_RAT]]); p = cat(p, " TO 1");
    ui_text(bmp, 126 - 4 * ui_len(s), 46, s);

    /* the meter: the scale (dots, a tick every 4 dB), the needle */
    for (int32_t i = 0; i <= 20; i++) {
        ui_dot(bmp, MX + arc[i][0], MY + arc[i][1]);
        if ((i & 3) == 0) ui_dot(bmp, MX + arc[i][0] * 7 / 8, MY + arc[i][1] * 7 / 8);
    }
    int32_t n = clampi((db10(g) + 5) / 10, 0, 20);
    ui_line(bmp, MX, MY, MX + arc[n][0] * 15 / 16, MY + arc[n][1] * 15 / 16);
    fw_fillrect(bmp, MX - 1, MY, MX + 1, MY + 1, 1);
    ui_text(bmp, MX + 15, MY, "0");
    ui_text(bmp, MX - 21, MY, "20");

    /* the curve: in -48 .. 0 dB across, out up; the threshold's corner; a dot for the mix now */
    int32_t thr10 = -400 + kv[K_THR] * 400 / 127, r = ratio[kv[K_RAT]], mk10 = kv[K_MKP] * 150 / 127, prev = -1;
    for (int32_t x = 0; x < CW; x += 2) ui_dot(bmp, CX0 + x, CY0 + x * CH / CW);   /* 1 to 1, dotted */
    for (int32_t x = 0; x <= CW; x++) {
        int32_t in10 = -480 + x * 480 / CW, out10 = in10 > thr10 ? thr10 + (in10 - thr10) / r : in10;
        if (on) out10 += mk10;
        int32_t y = clampi((out10 + 480) * CH / 480, 0, CH);
        int32_t a = prev < 0 ? y : prev < y ? prev : y, z = prev < 0 ? y : prev > y ? prev : y;
        fw_fillrect(bmp, CX0 + x, CY0 + a, CX0 + x, CY0 + z, 1);
        prev = y;
    }
    if (on) {
        int32_t in10 = clampi(db10(shown_in), -480, 0), x = (in10 + 480) * CW / 480;
        int32_t out10 = in10 - db10(g) + mk10, y = clampi((out10 + 480) * CH / 480, 0, CH);
        fw_fillrect(bmp, CX0 + x - 1, CY0 + y - 1, CX0 + x + 1, CY0 + y + 1, -1);   /* the mix now */
    }
    for (int32_t x = 0; x < 128; x += 2) ui_dot(bmp, x, 26);

    for (int32_t k = 0; k < KNOBS; k++) {
        val_text(s, k);
        ui_knob(bmp, k, kname[k], s, kv[k], kmax[k], kmax[k] <= 5 ? 2 : 0, k == hl_k && hl_ticks > 0);
    }
}

static int32_t gl_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > KNOBS) return 0;
    int32_t k = knob - 1, per = kmax[k] <= 5 ? 16 : 4;
    hl_k = k; hl_ticks = 40;
    if ((turned[k] > 0) != (turn > 0)) turned[k] = 0;
    turned[k] += turn;
    while (turned[k] >= per || turned[k] <= -per) {
        int32_t dir = turned[k] > 0 ? 1 : -1;
        turned[k] -= dir * per;
        kv[k] = (int8_t)clampi(kv[k] + dir, 0, kmax[k]);
    }
    save_wait = 120;
    fw_invalidate(view);
    return 1;
}
static void gl_clear(void *view)
{
    for (int32_t k = 0; k < KNOBS; k++) { kv[k] = kdef[k]; turned[k] = 0; }
    save_wait = 120;
    fw_invalidate(view);
}
static int32_t gl_key(void *view, void *event) { return mp_key(view, event); }   /* LEFT / RIGHT: the stock page step */

struct core_mpage gl_page;
void gl_tick(void *ctrl)
{
    static uint32_t draws, t;
    (void)ctrl;
    t++;
    if (!loaded && t > 50) { loaded = 1; load(); }
    if (loaded) pj_tick();                           /* a project loaded or saved */
    if (save_wait > 0 && --save_wait == 0) save();
    if (hl_ticks > 0) hl_ticks--;
    static uint32_t drawn_at;
    if (core_mview && core_mview_draws != draws) { draws = core_mview_draws; drawn_at = t; }   /* the master view is on the screen */
    if (core_mview && t - drawn_at < 15 && t % 3 == 0 && core_mpage_shown(core_mview) == &gl_page) {
        /* what the page shows: the needle (a quarter dB) and the dot on the curve (half a dB) */
        static int32_t shown_sig = -1;
        int32_t sig = kv[K_CMP] ? shown_gr / (LG / 24) + 4096 * (shown_in / (LG / 12)) : 0;
        if (sig != shown_sig || hl_ticks == 1 || t - drawn_at >= 12) { shown_sig = sig; fw_invalidate(core_mview); }   /* and now and then, to know it still shows */
    }
}

struct core_mpage gl_page = { 99, "GLU", "Glue comp", gl_draw, gl_key, gl_enc, 0, 0, gl_clear };
