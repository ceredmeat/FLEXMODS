/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SONIC MAX for the Digitakt mk1 (core 4.2, OS 1.54): an enhancer for the whole mix after the BBE 822A
 * Sonic Maximizer, on a master page (FUNC + LFO, after DJ FILTER). Main, headphones and USB, in stereo.
 *
 * What the 822A does, and so this: it splits the sound in three (below 150 Hz, the middle, and the top
 * above about 1.2 kHz), holds the lows back about 2.5 ms and the mids about 0.5 ms so the top's
 * harmonics arrive first (the "phase" part), and turns the top up by how much the music lacks it right now
 * (the "process": a dynamic EQ that watches the mid and top levels). LO CONTOUR turns the lows up.
 *
 *   A  PRC   PROCESS: how much top it adds, 0-10 as on the panel
 *   B  LOW   LO CONTOUR: how much bottom, 0-10
 *   C  BBE   the IN / OUT switch
 *   D  OUT   output level, -inf to +6 dB (the middle is 0 dB)
 *   E  XOV   where the top band starts: 1.2K (the 822A), 2.4K, 4.8K
 *   F  PHS   the phase part: 0-200 % of the 822A's delays
 *   G  SPD   how fast the process follows: SLW, MED, FST
 *   H  SAT   the op-amps pushed: a soft saturation at the end, 0 is clean
 *
 * The page draws the response as it is right now: the top shelf rises and falls as the process works, with
 * the three bands' levels shaded under it. Saved.
 *
 * Sound in 32-bit integers (the CPU has no 64-bit multiply): samples Q24 (the Q31 mix / 128), gains Q15. */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "../common/heapbuf.h"
#include "../common/mpui.h"
#include "../common/fmul.h"

enum { K_PRC, K_LOW, K_BBE, K_OUT, K_XOV, K_PHS, K_SPD, K_SAT, KNOBS };
static const char *const kname[KNOBS] = { "PRC", "LOW", "BBE", "OUT", "XOV", "PHS", "SPD", "SAT" };
static const int8_t kdef[KNOBS] = { 64, 48, 0, 64, 0, 64, 1, 0 };
static const int8_t kmax[KNOBS] = { 127, 127, 1, 127, 2, 127, 2, 127 };
static volatile int8_t kv[KNOBS] = { 64, 48, 0, 64, 0, 64, 1, 0 };

#define clampi ui_clamp
/* (a x) >> 15 for a in Q15 (|a| <= 32768) and x up to about 2^30, without overflow */
static inline int32_t mq15(int32_t a, int32_t x) { return a * (x >> 15) + ((a * (x & 0x7fff)) >> 15); }

/* ---- the sound ----------------------------------------------------------------------------------------- */
/* one-pole low passes at 48 kHz: a = 1 - exp(-2 pi f / 48000), Q15 */
#define A_LO 637                                     /* 150 Hz, twice: the low band, 12 dB */
static const int16_t a_xov[3] = { 4763, 8833, 15287 };   /* 1.2, 2.4, 4.8 kHz: the top band, 6 dB (gentle) */
/* The phase part, as the 822A does it with filters: two first-order all-passes (the level stays flat, the
 * timing moves). Below its corner an all-pass delays by about 1 / (pi f): at 150 Hz that's 2.1 ms for the
 * lows, at 1.2 kHz another 0.27 ms for the lows and mids, the top through on time. PHS moves the corners:
 * half the frequency, twice the delay. (Plain delays on split bands cancel where the bands overlap.) */
static int32_t ap_coef(int32_t hz)                   /* (tan(pi f / fs) - 1) / (tan + 1), Q15 */
{
    int32_t t = hz * 70278 / 32768;                  /* pi / 48000 x 32768 x f, Q15 */
    if (t > 30000) t = 30000;
    return ((t - 32768) << 15) / (t + 32768);
}
static int32_t e_mid, e_hi, e_lo, gh, gl;           /* the bands' levels (Q24); the gains' extra (Q15) */
static volatile int32_t shown_gh, shown_lo, shown_mid, shown_hi, on_now;

static inline int32_t soft(int32_t x, int32_t sat)  /* x Q24 */
{
    int32_t s = clampi((x >> 9) * (32 + sat) >> 5, -32767, 32767);    /* Q15, up to x5 */
    int32_t y = s - (((s * s) >> 15) * s >> 15) / 3;
    return (y << 9) * 3 / 2 * 32 / (32 + sat / 2);
}

/* One channel's 32 samples (every other word of the block), its state and numbers in registers. The
 * three bands always add back up to the sound (lo + mid + hi = x), so the output is the sound plus the two
 * boosts: y = x + 4 (g_lo lo + g_hi hi). Peaks of the bands every fourth frame, for the process and the
 * page. In fast SRAM. */
struct mx_k { int32_t alo, ax, c_lo, c_mid, g_lo, g_hi, outg, phs; };
struct mx_s { int32_t a0x, a0y, a1x, a1y, lp1, lp2, lpx; };
static struct mx_s mx_st[2];
static inline __attribute__((always_inline)) void mx_chan_body(int32_t *p, struct mx_s *st, const struct mx_k *k, int32_t *pk)
{
    /* both channels side by side (left in ACC0, right in ACC1): st[0] left, st[1] right */
    int32_t la0x = st[0].a0x, la0y = st[0].a0y, la1x = st[0].a1x, la1y = st[0].a1y, ll1 = st[0].lp1, ll2 = st[0].lp2, llx = st[0].lpx;
    int32_t ra0x = st[1].a0x, ra0y = st[1].a0y, ra1x = st[1].a1x, ra1y = st[1].a1y, rl1 = st[1].lp1, rl2 = st[1].lp2, rlx = st[1].lpx;
    const int32_t alo = k->alo, ax = k->ax, c_lo = k->c_lo, c_mid = k->c_mid, g_lo = k->g_lo, g_hi = k->g_hi, outg = k->outg, phs = k->phs;
    int32_t pl = pk[0], pm = pk[1], ph = pk[2];
    for (int32_t n = 0; n < 32; n++, p += 2) {
        int32_t xl = p[0] >> 7, xr = p[1] >> 7, dl, dr;
        if (phs) {                                   /* the phase part: two all-passes */
            fmx1(c_lo, xl - la0y, xr - ra0y, dl, dr);
            dl += la0x; dr += ra0x; la0x = xl; ra0x = xr; la0y = dl; ra0y = dr;
            fmx1(c_mid, dl - la1y, dr - ra1y, xl, xr);
            xl += la1x; xr += ra1x; la1x = dl; ra1x = dr; la1y = xl; ra1y = xr;
        }
        fmx1(alo, xl - ll1, xr - rl1, dl, dr); ll1 += dl; rl1 += dr;
        fmx1(alo, ll1 - ll2, rl1 - rl2, dl, dr); ll2 += dl; rl2 += dr;
        fmx1(ax, xl - llx, xr - rlx, dl, dr); llx += dl; rlx += dr;
        int32_t hl = xl - llx, hr = xr - rlx;
        if (!(n & 3)) {                              /* the levels, from every fourth frame (left + right) */
            int32_t a = ll2 < 0 ? -ll2 : ll2, m = llx - ll2;
            if (a > pl) pl = a;
            a = m < 0 ? -m : m; if (a > pm) pm = a;
            a = hl < 0 ? -hl : hl; if (a > ph) ph = a;
            a = rl2 < 0 ? -rl2 : rl2; m = rlx - rl2;
            if (a > pl) pl = a;
            a = m < 0 ? -m : m; if (a > pm) pm = a;
            a = hr < 0 ? -hr : hr; if (a > ph) ph = a;
        }
        fmx2(g_lo, g_hi, ll2, hl, rl2, hr, dl, dr);
        xl += dl << 2; xr += dr << 2;
        if (outg != 0x40000000) { fmx1(outg, xl, xr, dl, dr); xl = dl << 1; xr = dr << 1; }
        p[0] = xl; p[1] = xr;                        /* Q24 for now: clamped (and saturated) by the caller */
    }
    st[0].a0x = la0x; st[0].a0y = la0y; st[0].a1x = la1x; st[0].a1y = la1y; st[0].lp1 = ll1; st[0].lp2 = ll2; st[0].lpx = llx;
    st[1].a0x = ra0x; st[1].a0y = ra0y; st[1].a1x = ra1x; st[1].a1y = ra1y; st[1].lp1 = rl1; st[1].lp2 = rl2; st[1].lpx = rlx;
    pk[0] = pl; pk[1] = pm; pk[2] = ph;
}
FAST_CODE static void mx_chan_fast(int32_t *p, struct mx_s *st, const struct mx_k *k, int32_t *pk) { mx_chan_body(p, st, k, pk); }
__attribute__((noinline)) static void mx_chan_ram(int32_t *p, struct mx_s *st, const struct mx_k *k, int32_t *pk) { mx_chan_body(p, st, k, pk); }
typedef void (*mx_chan_fn)(int32_t *, struct mx_s *, const struct mx_k *, int32_t *);
static volatile mx_chan_fn mx_chan = mx_chan_ram;     /* the fast one once it's checked to be there */

void mx_render(int32_t *pair)
{
    if (!kv[K_BBE]) {                                /* OUT: untouched */
        if (on_now) {
            for (int32_t c = 0; c < 2; c++) { struct mx_s z = { 0, 0, 0, 0, 0, 0, 0 }; mx_st[c] = z; }
            gh = 0; on_now = 0;
        }
        shown_gh = 0;
        return;
    }
    on_now = 1;
    struct mx_k k;
    int32_t phs = kv[K_PHS], sat = kv[K_SAT];
    k.alo = Q31(A_LO); k.ax = Q31(a_xov[kv[K_XOV]]); k.phs = phs;
    k.c_lo = phs ? Q31(ap_coef(150 * 64 / phs)) : 0; k.c_mid = phs ? Q31(ap_coef(1200 * 64 / phs)) : 0;
    k.outg = Q31(kv[K_OUT] * 256);                   /* 64 is 0x40000000: 0 dB, skipped */
    /* the gains: the bottom from LO CONTOUR (up to x3), the top from the process (up to x4) */
    gl = kv[K_LOW] * 129;                            /* 0 .. 16383: extra x4 >> 15, so up to +2 */
    k.g_hi = Q31(gh); k.g_lo = Q31(gl);
    int32_t pk[3] = { 0, 0, 0 };
    fm_start();
    mx_chan(pair, mx_st, &k, pk);                   /* both channels at once */
    for (int32_t i = 0; i < 64; i++) {
        int32_t y = pair[i];
        if (sat) y = soft(y, sat);
        pair[i] = clampi(y, -(1 << 24), (1 << 24) - 1) << 7;
    }
    int32_t pk_lo = pk[0], pk_mid = pk[1], pk_hi = pk[2];
    /* the levels: quick up, slower down */
    e_lo += pk_lo > e_lo ? (pk_lo - e_lo) >> 2 : (pk_lo - e_lo) >> 5;
    e_mid += pk_mid > e_mid ? (pk_mid - e_mid) >> 2 : (pk_mid - e_mid) >> 5;
    e_hi += pk_hi > e_hi ? (pk_hi - e_hi) >> 2 : (pk_hi - e_hi) >> 5;
    /* the process: the less top there is next to the mids, the more it adds (a third always, for PRC) */
    int32_t m = e_mid >> 8, h = e_hi >> 8, d;
    if (m < 16) d = 16384;                           /* silence: halfway */
    else {                                           /* top / mids: 0.3 or less is dull (all of it), 0.9 bright (none) */
        int32_t r = h >= 2 * m ? 65536 : (h << 15) / m;
        d = clampi((29491 - r) * 5 / 3, 0, 32768);
    }
    int32_t want = (kv[K_PRC] * (10923 + ((21845 * (d >> 3)) >> 12)) / 127) * 3 / 4;   /* .. 24576: x4 */
    static const uint8_t att[3] = { 6, 4, 2 }, rel[3] = { 9, 7, 5 };
    int32_t sp = kv[K_SPD];
    gh += want > gh ? (want - gh) >> att[sp] : (want - gh) >> rel[sp];
    shown_gh = gh; shown_lo = e_lo; shown_mid = e_mid; shown_hi = e_hi;
}

/* ---- saving -------------------------------------------------------------------------------------------- */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
HEAP_BUF(block, 512)
#define MAGIC 0x534d5831u                            /* "SMX1" */
static int32_t loaded, save_wait;
static uint32_t sum_of(const uint8_t *b) { uint32_t s = 7; for (int32_t i = 8; i < 8 + KNOBS; i++) s = s * 31u + b[i]; return s; }
/* the settings go with the project (common/projsave.h, through PROJECT LINK): each slot's copy at
 * PJ_BASE + slot x PJ_NB */
#define PJ_BASE 0x62100
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

/* ---- the page: everything from here on is the screen and the knobs, built small ----------------------- */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns", "no-optimize-strlen")
static int32_t turned[KNOBS], hl_k = -1, hl_ticks;

#define put_num ui_num
#define txt ui_text
#define tlen ui_len
#define dot ui_dot
static char *cat(char *b, const char *s) { while (*s) *b++ = *s++; *b = 0; return b; }

/* log2(x) x 256, roughly (straight between the powers of two: within half a dB) */
static int32_t l2q8(uint32_t x)
{
    if (!x) return 0;
    int32_t n = 31 - __builtin_clz(x);
    uint32_t f = n >= 8 ? (x >> (n - 8)) & 255 : (x << (8 - n)) & 255;
    return n * 256 + (int32_t)f;
}
/* a gain of 1 + 4 e (e Q15) in tenths of a dB */
static int32_t gain_db10(int32_t e) { return (l2q8(32768u + 4u * (uint32_t)e) - 15 * 256) * 602 / 2560; }
/* a level (Q24) in tenths of a dB below full scale */
static int32_t level_db10(int32_t v) { return v <= 0 ? -900 : (l2q8((uint32_t)v) - 24 * 256) * 602 / 2560; }

static char *db_text(char *b, int32_t db10)          /* "+6.5" */
{
    if (db10 >= 0) *b++ = '+'; else { *b++ = '-'; db10 = -db10; }
    b = put_num(b, db10 / 10);
    *b++ = '.';
    return put_num(b, db10 % 10);
}

static void val_text(char *b, int32_t k)
{
    int32_t v = kv[k];
    switch (k) {
    case K_PRC: case K_LOW: { int32_t t = v * 100 / 127; b = put_num(b, t / 10); *b++ = '.'; put_num(b, t % 10); return; }
    case K_BBE: cat(b, v ? "IN" : "OUT"); return;
    case K_OUT:
        if (!v) { cat(b, "OFF"); return; }
        { int32_t d = (l2q8((uint32_t)v) - l2q8(64)) * 602 / 2560; b = d >= 0 ? cat(b, "+") : b; put_num(b, (d + (d >= 0 ? 5 : -5)) / 10); }
        return;
    case K_XOV: { static const char *const x[3] = { "1.2K", "2.4K", "4.8K" }; cat(b, x[v]); return; }
    case K_PHS: put_num(b, v * 200 / 127); return;
    case K_SPD: { static const char *const s[3] = { "SLW", "MED", "FST" }; cat(b, s[v]); return; }
    default: put_num(b, v); return;
    }
}

/* ---- the layout (y 0 the bottom row; the title bar is 53-63, the core's) ----
 *   46-50  the readout: the top's boost right now, and the bottom's
 *   32-44  the response, 20 Hz - 20 kHz across 120 columns, shaded under it
 *   28-29  each band's level right now, a meter under its part of the curve
 *   21-25  the knobs' names;  7-19 their dials;  0-5 their values */
#define GX0 4
#define GY0 32
#define GH  12
/* columns of 150 Hz and the top band's start (log: 20 Hz at 0, 20 kHz at 120) */
#define C_LO 35
static const uint8_t c_xov[3] = { 71, 83, 95 };

static int32_t col_h(int32_t x, int32_t lo_db10, int32_t hi_db10, int32_t cx)
{
    /* each shelf eases in over about a third of an octave either side of its corner */
    int32_t l = x <= C_LO - 6 ? lo_db10 : x >= C_LO + 6 ? 0 : lo_db10 * (C_LO + 6 - x) / 12;
    int32_t h = x >= cx + 8 ? hi_db10 : x <= cx - 8 ? 0 : hi_db10 * (x - cx + 8) / 16;
    return clampi(2 + (l + h) * 10 / 125, 0, GH);      /* 0 dB 2 dots up, then 0.8 of a dot a dB */
}

static void mx_draw(void *view, void *bmp)
{
    char s[24], *p;
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    int32_t on = kv[K_BBE], cx = c_xov[kv[K_XOV]];
    int32_t hi_db = on ? gain_db10(shown_gh) : 0, lo_db = on ? gain_db10(gl) : 0;

    /* the readout */
    if (!on) txt(bmp, 2, 46, "BBE OUT");
    else {
        p = cat(s, "TOP "); p = db_text(p, hi_db); cat(p, "DB");
        txt(bmp, 2, 46, s);
        p = cat(s, "LOW "); p = db_text(p, lo_db);
        txt(bmp, 126 - 4 * tlen(s), 46, s);
    }

    /* each band's level: a meter under its part of the curve, filling from its left (-48 .. 0 dB) */
    if (on) {
        int32_t lv[3] = { shown_lo, shown_mid, shown_hi }, x0[3] = { 0, C_LO, cx }, x1[3] = { C_LO, cx, 120 };
        for (int32_t b = 0; b < 3; b++) {
            int32_t w = x1[b] - x0[b] - 3, n = clampi((level_db10(lv[b]) + 480) * w / 480, 0, w);
            fw_fillrect(bmp, GX0 + x0[b] + 1, 28, GX0 + x0[b] + w, 28, 1);       /* the scale */
            if (n) fw_fillrect(bmp, GX0 + x0[b] + 1, 28, GX0 + x0[b] + n, 30, 1);
        }
    }
    /* the corners, dotted */
    for (int32_t y = GY0; y <= GY0 + GH; y += 3) { dot(bmp, GX0 + C_LO, y); dot(bmp, GX0 + cx, y); }
    /* the response, solid on top */
    int32_t prev = -1;
    for (int32_t x = 0; x < 120; x++) {
        int32_t h = col_h(x, lo_db, hi_db, cx);
        int32_t a = prev < 0 ? h : prev < h ? prev : h, z = prev < 0 ? h : prev > h ? prev : h;
        fw_fillrect(bmp, GX0 + x, GY0 + a, GX0 + x, GY0 + z, 1);
        prev = h;
    }
    for (int32_t x = 0; x < 128; x += 2) dot(bmp, x, 26);

    /* the knobs: name, dial, value; the one just turned lit */
    for (int32_t k = 0; k < KNOBS; k++) {
        val_text(s, k);
        ui_knob(bmp, k, kname[k], s, kv[k], kmax[k], kmax[k] <= 2 ? 2 : 0, k == hl_k && hl_ticks > 0);
    }
}

static int32_t mx_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > KNOBS) return 0;
    int32_t k = knob - 1, per = kmax[k] <= 2 ? 16 : 4;
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

static void mx_clear(void *view)
{
    for (int32_t k = 0; k < KNOBS; k++) { kv[k] = kdef[k]; turned[k] = 0; }
    save_wait = 120;
    fw_invalidate(view);
}

static int32_t mx_key(void *view, void *event) { return mp_key(view, event); }   /* LEFT / RIGHT: the stock page step */

struct core_mpage mx_page;
void mx_tick(void *ctrl)
{
    static uint32_t draws, t;
    (void)ctrl;
    t++;
    if (t % 32 == 1) mx_chan = FAST_PICK(mx_chan_fast, mx_chan_ram);   /* fast SRAM while it holds our code */
    if (!loaded && t > 50) { loaded = 1; load(); }
    if (loaded) pj_tick();                           /* a project loaded or saved */
    if (save_wait > 0 && --save_wait == 0) save();
    if (hl_ticks > 0) hl_ticks--;
    static uint32_t drawn_at;
    if (core_mview && core_mview_draws != draws) { draws = core_mview_draws; drawn_at = t; }   /* the master view is on the screen */
    if (core_mview && t - drawn_at < 15 && t % 3 == 0 && core_mpage_shown(core_mview) == &mx_page) {
        /* what the page shows, coarsely: the top's boost (half dB), the three meters (a dot) */
        static int32_t shown_sig = -1;
        int32_t sig = kv[K_BBE] ? (gain_db10(shown_gh) / 5) + 97 * (level_db10(shown_lo) / 25) +
                                  9409 * (level_db10(shown_mid) / 25) + 912673 * (level_db10(shown_hi) / 25) : 0;
        if (sig != shown_sig || hl_ticks == 1 || t - drawn_at >= 12) { shown_sig = sig; fw_invalidate(core_mview); }   /* and now and then, to know it still shows */
    }
}

/* after DJ FILTER's page */
struct core_mpage mx_page = { 99, "SMX", "Sonic max", mx_draw, mx_key, mx_enc, 0, 0, mx_clear };
