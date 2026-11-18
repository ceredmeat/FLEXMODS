/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DRIVE for the Digitakt mk1 (core 4.1, OS 1.54): a saturation of its own on each audio track, before the
 * track's filter, on a master page (FUNC + LFO) that edits the active track's.
 *
 *   A  TYP   OFF, TAPE (soft and warm), TUBE (lopsided, even harmonics), FUZZ (hard, square-ish),
 *            FOLD (a wavefolder: bright, metallic)
 *   B  DRV   how hard
 *   C  TON   its tone: a low pass after it, dark to open
 *   D  MIX   dry to driven
 *   E  LVL   its output level (the louder types are kept about even already)
 *
 * The page draws the curve: what comes out for what goes in. The eight tracks' settings are saved. It
 * works on each track's audio in ev_render_voices (the sample playback's block, before the filter). */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "../common/mpui.h"

enum { K_TYP, K_DRV, K_TON, K_MIX, K_LVL, KNOBS };
enum { T_OFF, T_TAPE, T_TUBE, T_FUZZ, T_FOLD, TYPES };
static const char *const kname[KNOBS] = { "TYP", "DRV", "TON", "MIX", "LVL" };
static const char *const tname[TYPES] = { "OFF", "TAP", "TUB", "FUZ", "FLD" };
static const char *const tlong[TYPES] = { "NO DRIVE", "TAPE: SOFT, WARM", "TUBE: LOPSIDED", "FUZZ: HARD CLIP", "FOLD: METALLIC" };
static const int8_t kdef[KNOBS] = { T_OFF, 40, 127, 127, 100 };
static const int8_t kmax[KNOBS] = { TYPES - 1, 127, 127, 127, 127 };
static volatile int8_t kv[8][KNOBS];

static int32_t active(void) { int32_t t = (int32_t)fw_active_track; return t < 0 ? 0 : t > 7 ? 7 : t; }

/* the curve: x in Q15 (already gained), out Q15 */
static inline int32_t shape(int32_t type, int32_t x)
{
    switch (type) {
    case T_TAPE: {                                  /* x - x^3 / 3, clipped at +-1 then */
        x = ui_clamp(x, -32767, 32767);
        return (x - ((((x * x) >> 15) * x) >> 15) / 3) * 3 / 2;     /* to about full level */
    }
    case T_TUBE: {                                  /* lopsided: the top softer than the bottom */
        x = ui_clamp(x, -32767, 32767);
        int32_t sq = (x * x) >> 15;
        return (x > 0 ? x - sq / 2 : x + sq / 4) * 4 / 3;
    }
    case T_FUZZ: {                                  /* hard, with the corners a little rounded */
        int32_t lim = 16384;
        if (x > lim) x = lim + ((x - lim) >> 4);
        if (x < -lim) x = -lim + ((x + lim) >> 4);
        return ui_clamp(x, -20480, 20480) * 3 / 2;
    }
    case T_FOLD: {                                  /* fold back at +-1: a triangle wave of the input */
        int32_t p = x & 0x1ffff;                    /* period 4.0 (in Q15: 131072) */
        if (x < 0) p = (x % 131072 + 131072) % 131072;
        return p < 32768 ? p : p < 98304 ? 65536 - p : p - 131072;
    }
    default: return x;
    }
}
static int32_t gain_q8(int32_t type, int32_t drv)  /* how much in: x1 .. x20, fuzz up to x40 */
{
    return 256 + drv * (type == T_FUZZ ? 80 : type == T_FOLD ? 30 : 38);
}

static struct { int32_t lp; } st[8];

void dr_render(int32_t *blocks)
{
    for (int t = 0; t < 8; t++) {
        int32_t type = kv[t][K_TYP];
        if (type == T_OFF)
            continue;
        int32_t *b = &blocks[32 * t];
        int32_t g = gain_q8(type, kv[t][K_DRV]), mix = kv[t][K_MIX], lvl = kv[t][K_LVL];
        int32_t lpk = 2000 + kv[t][K_TON] * 241, lp = st[t].lp;      /* Q15: dark .. open */
        for (int i = 0; i < 32; i++) {
            int32_t dry = b[i] >> 16, x = (dry * g) >> 8, y = shape(type, x);
            lp += ((y - lp) * (lpk >> 1)) >> 14;
            y = lp;
            y = dry + (((y - dry) * mix) >> 7);
            b[i] = ui_clamp((y * lvl) / 100, -32767, 32767) << 16;
        }
        st[t].lp = lp;
    }
}

/* ---- saving ------------------------------------------------------------------------------------------------ */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
#define MAGIC 0x44525645u                            /* "DRVE" */
static uint8_t block[512] __attribute__((aligned(16)));
static int32_t save_wait, loaded, ticks;

/* 0.4: everything from here on is the saving, the screen and the knobs: built small. The sound above is unchanged. */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns", "no-optimize-strlen")
static void load(void)
{
    uint32_t *w = (uint32_t *)block;
    for (int t = 0; t < 8; t++) for (int k = 0; k < KNOBS; k++) kv[t][k] = kdef[k];
    for (int i = 0; i < 512; i++) block[i] = 0;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0 || w[0] != MAGIC)
        return;
    uint32_t sum = 0;
    for (int i = 8; i < 8 + 8 * KNOBS; i++) sum = sum * 31u + block[i];
    if (w[1] != sum) return;
    for (int t = 0; t < 8; t++) for (int k = 0; k < KNOBS; k++)
        kv[t][k] = (int8_t)ui_clamp((int8_t)block[8 + t * KNOBS + k], 0, kmax[k]);
}
static void save(void)
{
    uint32_t *w = (uint32_t *)block, sum = 0;
    for (int i = 0; i < 512; i++) block[i] = 0;
    for (int t = 0; t < 8; t++) for (int k = 0; k < KNOBS; k++) block[8 + t * KNOBS + k] = (uint8_t)kv[t][k];
    for (int i = 8; i < 8 + 8 * KNOBS; i++) sum = sum * 31u + block[i];
    w[0] = MAGIC; w[1] = sum;
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}

/* ---- the page ------------------------------------------------------------------------------------------ */
static int32_t turned[KNOBS], hl_k = -1, hl_ticks;

static void dr_draw(void *view, void *bmp)
{
    char s[24];
    (void)view;
    int32_t t = active(), type = kv[t][K_TYP];
    (void)tlong;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    char *p;
    const char *a;
    /* the curve, in the middle: in across, out up; the straight line (no drive) dotted */
    int32_t x0 = 44, y0 = 28, w = 40, h = 22, prev = -1;
    fw_framerect(bmp, x0 - 1, y0 - 1, x0 + w, y0 + h, 1);
    for (int32_t c = 0; c < w; c += 2) ui_dot(bmp, x0 + c, y0 + c * h / w);
    for (int32_t c = 0; c < w; c++) {
        int32_t in = -32767 + c * 65534 / (w - 1);
        int32_t y = type == T_OFF ? in : shape(type, (in * gain_q8(type, kv[t][K_DRV])) >> 8);
        int32_t r = ui_clamp((y + 32768) * (h - 1) / 65535, 0, h - 1);
        int32_t lo = prev < 0 ? r : prev < r ? prev : r, hi = prev < 0 ? r : prev > r ? prev : r;
        fw_fillrect(bmp, x0 + c, y0 + lo, x0 + c, y0 + hi, 1);
        prev = r;
    }
    /* the eight tracks' drives, four each side, the active one lit */
    for (int32_t k = 0; k < 8; k++) {
        int32_t x = k < 4 ? 2 : 90, y = 45 - (k & 3) * 6;   /* 0.3: a pixel between rows, the lit box clear of its neighbours */
        char lb[8] = { 'T', (char)('1' + k), ' ', 0 };
        a = tname[kv[k][K_TYP]]; p = lb + 3; while ((*p++ = *a++)) ;
        ui_text(bmp, x, y, lb);
        if (k == t) fw_fillrect(bmp, x - 1, y - 1, x + 28, y + 5, -1);
    }
    for (int32_t k = 0; k < KNOBS; k++) {
        int32_t v = kv[t][k];
        if (k == K_TYP) { a = tname[v]; p = s; while ((*p++ = *a++)) ; }
        else ui_num(s, k == K_LVL ? v : v);
        ui_knob(bmp, k, kname[k], s, v, kmax[k], k == K_TYP ? 2 : 0, k == hl_k && hl_ticks > 0);
    }
}

static int32_t dr_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > KNOBS)
        return 0;
    int32_t t = active(), k = knob - 1, per = k == K_TYP ? 16 : 4;
    hl_k = k; hl_ticks = 40;
    if ((turned[k] > 0) != (turn > 0)) turned[k] = 0;
    turned[k] += turn;
    while (turned[k] >= per || turned[k] <= -per) {
        int32_t dir = turned[k] > 0 ? 1 : -1;
        turned[k] -= dir * per;
        kv[t][k] = (int8_t)ui_clamp(kv[t][k] + dir, 0, kmax[k]);
    }
    save_wait = 60;
    fw_invalidate(view);
    return 1;
}

static void dr_clear(void *view)
{
    int32_t t = active();
    for (int32_t k = 0; k < KNOBS; k++) { kv[t][k] = kdef[k]; turned[k] = 0; }
    save_wait = 60;
    fw_invalidate(view);
}

void dr_tick(void *ctrl)
{
    (void)ctrl;
    if (!loaded && ++ticks > 30) { loaded = 1; load(); }
    if (save_wait > 0 && --save_wait == 0) save();
    if (hl_ticks > 0 && --hl_ticks == 0 && core_mview) fw_invalidate(core_mview);
}

struct core_mpage dr_page = { 99, "DRV", "Drive", dr_draw, mp_key, dr_enc, 0, 0, dr_clear };
