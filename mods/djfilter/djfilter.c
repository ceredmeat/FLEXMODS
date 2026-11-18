/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DJ FILTER for the Digitakt mk1 (core 4.1): a master filter after the Electrix FilterFactory, on a page of
 * the master pages (FUNC + LFO, after the internal mixer). It filters the whole mix, in stereo, on every
 * output (main, headphones, USB).
 *
 *   A  DJ / FREQ  in DJ mode one knob: the middle is no filter, left a low pass closing, right a high pass
 *                 opening; in the other modes the cutoff, 20 Hz - 20 kHz
 *   B  RES        resonance
 *   C  TYPE       DJ, DJ4, LP, LP4, HP, HP4, BP (band pass), NTCH (notch); the 4s are 24 dB (the
 *                 FilterFactory's 4 POLE, kept in stereo here), the others 12 dB
 *   D  BUZZ       drive before the filter (the FilterFactory's BUZZ), level kept about even
 *   E  WAVE       the LFO's shape: TRI, SAW, SQR, RND (a new random level each cycle) or ENV (the
 *                 mix's loudness, an envelope follower, instead of an LFO)
 *   F  RATE       the LFO's speed, 0.05 - 20 Hz
 *   G  DPTH       how far the LFO (or ENV) moves the cutoff; left of the middle it moves it down
 *   H  MIX        the effect mix: dry to filtered
 *
 * CLEAR (FUNC + held LFO + PLAY) on the page: everything back, filter off. It starts off at power-on.
 *
 * The filter is a state-variable one in its "topology preserving" form (Zavalishin), which stays stable
 * and in tune right up to 20 kHz: per channel two integrators, the coefficients worked out once a block.
 * All in 32-bit integers (the CPU has no 64-bit multiply): samples in Q24 (the Q31 mix / 128, headroom
 * for resonance), coefficients in Q15. */
#include "digitakt-mk1/core3.h"
#include "tables.h"

enum { K_DJ, K_RES, K_TYPE, K_BUZZ, K_WAVE, K_RATE, K_DPTH, K_MIX, KNOBS };
enum { T_DJ, T_DJ4, T_LP, T_LP4, T_HP, T_HP4, T_BP, T_NOTCH, TYPES };
enum { W_TRI, W_SAW, W_SQR, W_RND, W_ENV, WAVES };
static const char *const kname[KNOBS] = { "DJ", "RES", "TYP", "BUZ", "WAV", "SPD", "DEP", "MIX" };
static const char *const tname[TYPES] = { "DJ", "DJ4", "LP", "LP4", "HP", "HP4", "BP", "NCH" };
static const char *const wname[WAVES] = { "TRI", "SAW", "SQR", "RND", "ENV" };
static const int8_t kdef[KNOBS] = { 64, 30, T_DJ, 0, W_TRI, 40, 64, 127 };
static const int8_t kmax[KNOBS] = { 127, 127, TYPES - 1, 127, WAVES - 1, 127, 127, 127 };
/* 0.6: shared as djfilter_kv, so other mods can follow the filter (ledcolors' HEAT reads it, weakly) */
volatile int8_t djfilter_kv[KNOBS] = { 64, 30, T_DJ, 0, W_TRI, 40, 64, 127 };
#define kv djfilter_kv
static int is_dj(int32_t t) { return t == T_DJ || t == T_DJ4; }

static int32_t clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* (a x) >> 15 for a coefficient a in Q15 (|a| <= 32768) and a sample x up to about 2^30, without overflow */
static inline int32_t mq15(int32_t a, int32_t x)
{
    return a * (x >> 15) + ((a * (x & 0x7fff)) >> 15);
}

/* ---- the filter ---------------------------------------------------------------------------------------- */
struct svf { int32_t ic1, ic2; };
static struct svf f[2][2];                          /* [stage][channel] */
static int32_t cur_idx = -1;                        /* the cutoff now, 0-255 x 16 (smoothed) */
static uint32_t lfo_ph;

/* 0.7: SYNC. YES on the page switches the LFO between FREE (RATE in Hz) and SYNC (RATE in note values, locked to
 * the sequencer). The tempo is timed from track 1's steps (seq-events' ev_gen_trig), in audio blocks; on each
 * step the LFO is set where it should be in its cycle, counting steps from PLAY, so it never drifts. Stopped,
 * it keeps running at the last tempo (120 BPM until there is one). */
static volatile int8_t sync;
#define DIVS 15
static const uint16_t div_q8[DIVS] = { 32768, 16384, 8192, 4096, 3072, 2048, 1536, 1024, 768, 683, 512, 341, 256, 171, 128 };
static const char *const div_name[DIVS] = { "8BAR", "4BAR", "2BAR", "1BAR", "1/2D", "1/2", "1/4D", "1/4", "1/8D", "1/4T",
                                            "1/8", "1/8T", "1/16", "16T", "1/32" };   /* the cycle, in steps x 256 */
static volatile uint32_t blk, step_at, nsteps, sync_inc;
static volatile uint32_t step_blk_q8 = 48000;      /* audio blocks a step, x 256 (120 BPM) */
static int32_t div_of(int32_t v) { return v * DIVS / 128; }
static uint32_t inc_for(int32_t d)
{
    uint32_t period = (uint32_t)((step_blk_q8 >> 4) * div_q8[d]) >> 12;   /* blocks a cycle */
    return period ? 0xffffffffu / period : 0;
}

void df_step(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    (void)step; (void)first; (void)trig;
    if (track != 0) return;
    uint32_t now = blk, d = now - step_at;
    if (d < 8) return;                                         /* the same step again */
    if (d > (step_blk_q8 >> 7)) nsteps = 0;                    /* the first step after a stop */
    else {
        step_blk_q8 += ((int32_t)(d << 8) - (int32_t)step_blk_q8) >> 2;
        nsteps++;
    }
    step_at = now;
    if (sync) {
        int32_t dv = div_of(kv[K_RATE]);
        uint32_t c = div_q8[dv], at = (nsteps * 256u) % c;
        sync_inc = inc_for(dv);
        lfo_ph = ((at << 16) / c) << 16;                       /* where it should be in its cycle */
    }
}
static int32_t env;                                 /* the mix's loudness, Q24 */
static volatile int32_t shown_idx = 128 * 16, shown_env;

/* one sample through one stage: returns low, band and high pass */
static inline void svf_tick(struct svf *s, int32_t v0, int32_t a1, int32_t a2, int32_t a3, int32_t k,
                            int32_t *lo, int32_t *bp, int32_t *hp)
{
    int32_t v3 = v0 - s->ic2;
    int32_t v1 = mq15(a1, s->ic1) + mq15(a2, v3);
    int32_t v2 = s->ic2 + mq15(a2, s->ic1) + mq15(a3, v3);
    s->ic1 = clampi(2 * v1 - s->ic1, -(1 << 29), 1 << 29);
    s->ic2 = clampi(2 * v2 - s->ic2, -(1 << 29), 1 << 29);
    *lo = v2;
    *bp = v1;
    *hp = v0 - mq15(k, v1) * 2 - v2;                /* k is Q15 for half the damping */
}

/* the coefficients for cutoff index idx (0-255) and damping kd (Q15, half of k: 0.025 - 1.0) */
static void coefs(int32_t idx, int32_t kd, int32_t *a1, int32_t *a2, int32_t *a3)
{
    int32_t g = (int32_t)df_g[clampi(idx, 0, 255)];                 /* Q16 */
    int32_t g12 = g >> 4, gk12 = (g >> 4) + (kd >> 2);              /* g + k in Q12 (k = 2 kd) */
    int32_t den = 4096 + ((g12 * (gk12 >> 2)) >> 10);               /* 1 + g (g + k), Q12 */
    *a1 = (int32_t)((4096u << 15) / (uint32_t)den);                 /* Q15 */
    *a2 = ((g >> 2) * (*a1 >> 1)) >> 13;                            /* g a1 */
    *a3 = ((g >> 2) * (*a2 >> 1)) >> 13;                            /* g a2 */
}

static inline int32_t buzz(int32_t x, int32_t drive)                 /* x Q24; drive 0-127 */
{
    if (!drive)
        return x;
    int32_t s = clampi((x >> 9) * (16 + drive) >> 4, -32767, 32767);   /* Q15, up to x9 */
    int32_t y = s - (((s * s) >> 15) * s >> 15) / 3;                 /* a soft clip, at most 2/3 */
    return (y << 9) * 3 / 2 * 16 / (16 + drive / 2);                 /* back up, and roughly even */
}

void df_render(int32_t *pair)
{
    int32_t type = kv[K_TYPE], dj = kv[K_DJ], base;
    int32_t depth = kv[K_DPTH] - 64, drive = kv[K_BUZZ], wave = kv[K_WAVE], mix = kv[K_MIX];
    if ((is_dj(type) && dj >= 62 && dj <= 66 && depth == 0 && !drive) || !mix) {
        cur_idx = -1;                               /* the middle: no filter at all */
        f[0][0].ic1 = f[0][0].ic2 = f[0][1].ic1 = f[0][1].ic2 = 0;
        f[1][0].ic1 = f[1][0].ic2 = f[1][1].ic1 = f[1][1].ic2 = 0;
        shown_idx = -1;
        return;
    }
    int32_t mode;                                   /* 0 low pass, 1 high pass, 2 band, 3 notch, 4 none */
    if (is_dj(type)) {
        if (dj < 62) { mode = 0; base = 40 + dj * 215 / 61; }        /* ~60 Hz .. 20 kHz */
        else if (dj > 66) { mode = 1; base = (dj - 67) * 210 / 60; } /* 20 Hz .. ~6 kHz */
        else { mode = 4; base = 128; }
    } else {
        mode = type == T_LP || type == T_LP4 ? 0 : type == T_HP || type == T_HP4 ? 1 : type == T_BP ? 2 : 3;
        base = dj * 255 / 127;
    }
    int32_t four = type == T_DJ4 || type == T_LP4 || type == T_HP4;

    /* the loudness, quick up and slow down, for the envelope follower */
    int32_t peak = 0;
    for (int i = 0; i < 64; i += 8) {
        int32_t a = pair[i] >> 7;
        if (a < 0) a = -a;
        if (a > peak) peak = a;
    }
    env += peak > env ? (peak - env) >> 2 : (peak - env) >> 6;
    shown_env = env;

    /* the cutoff: the knob, and the LFO or the envelope (+-16384), then smoothed */
    uint32_t before = lfo_ph;
    blk++;
    static int32_t last_dv = -1;
    if (sync) {                                     /* SYNC: a new RATE takes effect at once */
        int32_t dv = div_of(kv[K_RATE]);
        if (dv != last_dv || !sync_inc) { sync_inc = inc_for(dv); last_dv = dv; }
    }
    lfo_ph += sync ? sync_inc : df_rate[kv[K_RATE]];
    int32_t ph = (int32_t)(lfo_ph >> 16), m;
    static int32_t held = 0;
    static uint32_t seed = 0x9e3779b9u;
    if (lfo_ph < before) {                          /* a new cycle: RND's next level */
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        held = (int32_t)(seed >> 17) - 16384;
    }
    switch (wave) {
    case W_TRI: m = (ph < 32768 ? ph : 65535 - ph) - 16384; break;
    case W_SAW: m = (ph >> 1) - 16384; break;
    case W_SQR: m = ph < 32768 ? 16384 : -16384; break;
    case W_RND: m = held; break;
    default:    m = clampi(env >> 9, 0, 32767) - 16384; break;   /* ENV: quiet at the bottom */
    }
    if (wave == W_ENV) m += 16384;                  /* the envelope only opens (or closes) from the knob */
    int32_t target = base * 16 + ((m >> 4) * depth >> 6) * 2;      /* up to about 5 octaves */
    target = clampi(target, 0, 255 * 16);
    if (cur_idx < 0) cur_idx = target;
    cur_idx += (target - cur_idx) >> 2;
    shown_idx = cur_idx;

    int32_t kd = 32768 - kv[K_RES] * 252;                            /* 1.0 .. ~0.02 */
    int32_t a1, a2, a3, b1, b2, b3;
    coefs(cur_idx >> 4, kd, &a1, &a2, &a3);
    if (four) coefs(cur_idx >> 4, 23170, &b1, &b2, &b3);           /* the first stage: no peak (0.707) */

    for (int i = 0; i < 64; i++) {
        int c = i & 1;
        int32_t dry = pair[i] >> 7, x = buzz(dry, drive), lo, bp, hp, y;
        if (mode == 4) { y = x; goto out; }
        if (four) {
            svf_tick(&f[1][c], x, b1, b2, b3, 23170, &lo, &bp, &hp);
            x = mode == 0 ? lo : mode == 1 ? hp : x;
        }
        svf_tick(&f[0][c], x, a1, a2, a3, kd, &lo, &bp, &hp);
        y = mode == 0 ? lo : mode == 1 ? hp : mode == 2 ? bp : lo + hp;
    out:
        if (mix < 127)                              /* the effect mix: dry to filtered */
            y = dry + (((y - dry) >> 7) * mix);
        pair[i] = clampi(y, -(1 << 24), (1 << 24) - 1) << 7;
    }
}

/* ---- the page ------------------------------------------------------------------------------------------ */
static int32_t turned[KNOBS];
static int32_t hl_k = -1, hl_ticks;                 /* the knob just turned: its cell lit for a moment */

/* 0.6: everything from here on is the screen and the knobs: built small. The sound above is unchanged. */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns", "no-optimize-strlen")
static void put_num(char *b, int32_t v)
{
    char d[6]; int n = 0;
    if (v < 0) { *b++ = '-'; v = -v; }
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *b++ = d[--n];
    *b = 0;
}

static void val_text(char *b, int32_t k)
{
    int32_t v = kv[k];
    switch (k) {
    case K_DJ:
        if (is_dj(kv[K_TYPE])) {
            if (v >= 62 && v <= 66) { b[0] = 'O'; b[1] = 'F'; b[2] = 'F'; b[3] = 0; }
            else { b[0] = v < 62 ? 'L' : 'H'; b[1] = 'P'; b[2] = 0; }
        } else {
            int32_t hz = df_hz[v * 255 / 127];
            if (hz >= 1000) { put_num(b, hz / 1000); while (*b) b++; *b++ = 'k'; *b = 0; }
            else put_num(b, hz);
        }
        return;
    case K_TYPE: { const char *s = tname[v]; while ((*b++ = *s++)) ; return; }
    case K_WAVE: { const char *s = wname[v]; while ((*b++ = *s++)) ; return; }
    case K_RATE: {
        if (sync) { const char *t = div_name[div_of(v)]; while ((*b++ = *t++)) ; return; }
        int32_t c = df_rate_chz[v];
        if (c < 100) { b[0] = '.'; put_num(b + 1, c); }
        else put_num(b, c / 100);
        return;
    }
    case K_DPTH: put_num(b, v - 64); return;
    default: put_num(b, v); return;
    }
}

static int32_t tlen(const char *s) { int32_t n = 0; while (s[n]) n++; return n; }
static void txt(void *bmp, int32_t x, int32_t y, const char *s) { fw_textf(bmp, fw_font5, x, y, -1, "%s", s); }
static void centred(void *bmp, int32_t x0, int32_t w, int32_t y, const char *s)
{
    int32_t x = x0 + (w - (4 * tlen(s) - 1)) / 2;
    txt(bmp, x < x0 ? x0 : x, y, s);
}
static char *cat(char *b, const char *s) { while (*s) *b++ = *s++; *b = 0; return b; }
static void dot(void *bmp, int32_t x, int32_t y) { fw_fillrect(bmp, x, y, x, y, 1); }
static void line(void *bmp, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        dot(bmp, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ---- the layout (y 0 the bottom row; the title bar is 53-63, the core's) ----
 *   46-50  the readout: what it does, in words; the LFO on the right
 *   28-44  the curve, shaded below, dotted lines at 100 Hz, 1 kHz, 10 kHz, the cutoff now as a line
 *   21-25  the knobs' names;  7-19 their dials;  0-5 their values */
#define GY0 28
#define GH  16

/* the response, roughly: its height (0 .. GH) at column at (0-255 across 20 Hz - 20 kHz) */
static int32_t resp(int32_t mode, int32_t at, int32_t idx, int32_t res)
{
    int32_t d = at - idx, ad = d < 0 ? -d : d, top = GH - 4, h;
    int32_t bump = ad < 14 ? (res / 20) * (14 - ad) / 14 : 0;
    switch (mode) {
    case 0: h = d <= 0 ? top : top - d / 4; break;                    /* low pass */
    case 1: h = d >= 0 ? top : top + d / 4; break;                    /* high pass */
    case 2: h = top - ad / 4; break;                                  /* band pass */
    case 3: h = ad < 7 ? 1 : top; break;                              /* notch */
    default: return top;
    }
    return clampi(h + (mode != 3 ? bump : 0), 0, GH);
}

static void draw_curve(void *bmp, int32_t mode, int32_t idx, int32_t res)
{
    int32_t prev = -1;
    for (int32_t g = 0; g < 3; g++) {                               /* the frequency lines, dotted */
        int32_t x = 4 + df_grid[g] * 120 / 256;
        for (int32_t y = GY0; y <= GY0 + GH; y += 3) dot(bmp, x, y);
    }
    for (int32_t x = 0; x < 120; x++) {
        int32_t h = resp(mode, x * 256 / 120, idx, res);
        for (int32_t y = 0; y < h - 1; y++)                         /* shaded below: every other dot */
            if (((x + y) & 1) == 0) dot(bmp, 4 + x, GY0 + y);
        int32_t lo = prev < 0 ? h : prev < h ? prev : h, hi = prev < 0 ? h : prev > h ? prev : h;
        fw_fillrect(bmp, 4 + x, GY0 + lo, 4 + x, GY0 + hi, 1);       /* the line itself, solid */
        prev = h;
    }
    if (mode != 4) {                                                /* where it cuts right now */
        int32_t x = 4 + clampi(idx, 0, 255) * 120 / 256;
        fw_fillrect(bmp, x, GY0, x, GY0 + GH, 1);
    }
}

/* a dial: a ring of dots, the part up to the value solid (from the top for the two-way knobs), a
 * pointer from the middle; the switches show a notch at each setting */
static void dial(void *bmp, int32_t cx, int32_t cy, int32_t v, int32_t max, int32_t kind)
{
    int32_t at = v * (DIAL_N - 1) / (max ? max : 1), mid = (DIAL_N - 1) / 2;
    for (int32_t i = 0; i < DIAL_N; i++) {
        int32_t on;
        if (kind == 1) on = (i >= mid && i <= at) || (i <= mid && i >= at);   /* two-way: from the top */
        else if (kind == 2) on = i == at;
        else on = i <= at;
        if (on) {
            dot(bmp, cx + dial6[i][0], cy + dial6[i][1]);
            dot(bmp, cx + dial5[i][0], cy + dial5[i][1]);
        } else if (kind == 2 ? (i * max) % (DIAL_N - 1) < max : (i & 1) == 0)
            dot(bmp, cx + dial6[i][0], cy + dial6[i][1]);           /* the track (the switch's notches) */
    }
    if (kind == 1) dot(bmp, cx, cy + 7);                            /* the two-way knobs' middle mark */
    line(bmp, cx, cy, cx + dial3[at][0], cy + dial3[at][1]);
    fw_fillrect(bmp, cx - 1, cy - 1, cx + 1, cy + 1, 1);            /* the hub */
}

static void df_draw(void *view, void *bmp)
{
    char s[24], *p;
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    int32_t type = kv[K_TYPE], dj = kv[K_DJ], idx = shown_idx < 0 ? -1 : shown_idx >> 4, mode;
    if (is_dj(type)) mode = dj < 62 ? 0 : dj > 66 ? 1 : 4;
    else mode = type == T_LP || type == T_LP4 ? 0 : type == T_HP || type == T_HP4 ? 1 : type == T_BP ? 2 : 3;
    if (idx < 0) mode = 4;

    /* the readout: "LP4 1.3KHZ", the LFO on the right */
    p = s;
    if (mode == 4) p = cat(p, is_dj(type) ? "DJ FILTER OFF" : "OFF");
    else {
        static const char *const mn[4] = { "LP", "HP", "BP", "NOTCH" };
        int32_t hz = df_hz[clampi(idx, 0, 255)];
        p = cat(p, mn[mode]);
        if (type == T_DJ4 || type == T_LP4 || type == T_HP4) p = cat(p, "4");
        *p++ = ' ';
        if (hz >= 1000) { put_num(p, hz / 1000); while (*p) p++; *p++ = '.'; put_num(p, hz / 100 % 10); while (*p) p++; *p++ = 'K'; *p = 0; }
        else { put_num(p, hz); while (*p) p++; }
        p = cat(p, "HZ");
    }
    txt(bmp, 2, 46, s);
    if (kv[K_DPTH] != 64) {
        char r[12];
        p = cat(s, wname[kv[K_WAVE]]);
        if (kv[K_WAVE] != W_ENV) { *p++ = ' '; val_text(r, K_RATE); p = cat(p, r); if (!sync) p = cat(p, "HZ"); }
        txt(bmp, 126 - 4 * tlen(s), 46, s);
    }

    draw_curve(bmp, mode, idx < 0 ? 128 : idx, kv[K_RES]);
    int32_t e = clampi(shown_env >> 19, 0, GH);                      /* the envelope follower's level */
    if (kv[K_WAVE] == W_ENV && e) fw_fillrect(bmp, 126, GY0, 127, GY0 + e, 1);
    for (int32_t x = 0; x < 128; x += 2) dot(bmp, x, 26);             /* a dotted line above the knobs */

    /* the knobs: name, dial, value; the one just turned lit */
    for (int32_t k = 0; k < KNOBS; k++) {
        int32_t x0 = k * 16, kind = k == K_DJ ? (is_dj(type) ? 1 : 0) : k == K_DPTH ? 1 : k == K_TYPE || k == K_WAVE ? 2 : 0;
        centred(bmp, x0, 15, 20, k == K_DJ && !is_dj(type) ? "FRQ" : kname[k]);
        dial(bmp, x0 + 7, 12, kv[k], kmax[k], kind);
        val_text(s, k);
        centred(bmp, x0, 15, 0, s);
        if (k == hl_k && hl_ticks > 0)
            fw_fillrect(bmp, x0, 0, x0 + 14, 25, -1);
    }
}

static int32_t df_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > KNOBS)
        return 0;
    hl_k = knob - 1;
    hl_ticks = 40;
    int32_t k = knob - 1, per = k == K_TYPE || k == K_WAVE ? 16 : 4;   /* switches: a notch; sweeps: 4 counts */
    if ((turned[k] > 0) != (turn > 0))
        turned[k] = 0;
    turned[k] += turn;
    while (turned[k] >= per || turned[k] <= -per) {
        int32_t dir = turned[k] > 0 ? 1 : -1;
        turned[k] -= dir * per;
        kv[k] = (int8_t)clampi(kv[k] + dir, 0, kmax[k]);
        if (k == K_TYPE && is_dj(kv[k]) != is_dj(kv[k] - dir))
            kv[K_DJ] = is_dj(kv[k]) ? 64 : kv[k] == T_HP || kv[k] == T_HP4 ? 0 : 127;  /* off / open */
    }
    fw_invalidate(view);
    return 1;
}

static void df_clear(void *view)
{
    for (int32_t k = 0; k < KNOBS; k++) { kv[k] = kdef[k]; turned[k] = 0; }
    fw_invalidate(view);
}

/* ---- LEFT / RIGHT step through the master pages (0.5) -------------------------------------------------
 * The master view keeps its pages' kinds in a list (+124 its start, +128 its end) and the place of the one it
 * shows at +144 (core's master.s); a step writes that place and has the view drawn again. On a mod's page
 * the page's key handler does it; the stock pages (COMP, the mixers) give mods no keys, so an ev_key
 * handler does it there, while the master view is on the screen (drawn in the last ticks). */
#define K_LEFT  16
#define K_RIGHT 17
static uint32_t ticks, drawn_at;
static void mp_step(void *view, int32_t dir)
{
    uint8_t *v = (uint8_t *)view;
    int32_t n = (int32_t)((*(uint32_t *)(v + 128) - *(uint32_t *)(v + 124)) >> 2);
    int32_t cur = *(int32_t *)(v + 144);
    if (n < 2 || cur < 0) return;
    *(int32_t *)(v + 144) = (cur + dir + n) % n;
    fw_invalidate(view);
}
static int32_t df_key(void *view, void *event)
{
    int32_t key = *(int32_t *)((char *)event + 12), fl = *(int32_t *)((char *)event + 16);
    static int32_t yes_tap;
    if (key == 12) {                                /* YES tapped on its own: FREE / SYNC */
        if ((fl & 1) && !(fl & 0x18)) yes_tap = 1;
        else if ((fl & 0x10) && yes_tap) {
            yes_tap = 0;
            sync = !sync;
            sync_inc = 0;
            hl_k = K_RATE; hl_ticks = 60;
            fw_invalidate(view);
        }
        return 0;                                   /* the others (FILL, MUTESNAP) still see YES */
    }
    if (fl & 1) yes_tap = 0;                        /* another key with YES: not a tap */
    if (key != K_LEFT && key != K_RIGHT) return 0;
    if ((fl & 1) && !(fl & 0x12)) mp_step(view, key == K_RIGHT ? 1 : -1);
    return 1;
}
int32_t df_any_key(void *brain, void *ev)
{
    int32_t key = *(int32_t *)((uint8_t *)ev + 12), fl = *(int32_t *)((uint8_t *)ev + 16);
    static int32_t trk_down;
    (void)brain;
    if (key == 2) trk_down = (fl & 1) && !(fl & 0x10);       /* TRK: TRK + LEFT / RIGHT is LED colours' */
    if ((key != K_LEFT && key != K_RIGHT) || trk_down || !core_mview || ticks - drawn_at > 25)
        return 0;
    if (core_mpage_shown(core_mview))
        return 0;                                   /* a mod's page: its own key handler steps */
    if ((fl & 1) && !(fl & 0x12)) mp_step(core_mview, key == K_RIGHT ? 1 : -1);
    return 1;
}

extern struct core_mpage df_page;

void df_tick(void *ctrl)
{
    static uint32_t draws;
    (void)ctrl;
    ticks++;
    if (hl_ticks > 0 && --hl_ticks == 0 && core_mview)
        fw_invalidate(core_mview);                          /* the lit knob goes dark again */
    if (core_mview && core_mview_draws != draws) {          /* the master view is on the screen */
        draws = core_mview_draws;
        drawn_at = ticks;
        if (core_mpage_shown(core_mview) == &df_page && shown_idx >= 0)
            fw_invalidate(core_mview);                      /* keep the curve moving */
    }
}

/* 0.5: after every stock page (99), first of the mod pages; LEFT / RIGHT step the pages */
struct core_mpage df_page = { 99, "DJF", "DJ filter", df_draw, df_key, df_enc, 0, 0, df_clear };
