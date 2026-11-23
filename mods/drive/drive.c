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
 *   F  LPG   0.5: a low pass gate after the drive, after VILNIS's: OFF, LP (it darkens as it dies), VCA (it
 *            fades), BTH (both: the Buchla bongo). Every hit on the track strikes it (sequenced or played,
 *            the firmware's voice start), harder hits open it further
 *   G  DCY   the gate's decay, short plucks to long rings
 *   H  RES   the gate's resonance
 *
 * The page draws the curve: what comes out for what goes in. The eight tracks' settings are saved. It
 * works on each track's audio in ev_render_voices (the sample playback's block, before the filter). */
#include <stdint.h>
#include "../common/heapbuf.h"
#include "digitakt-mk1/core3.h"
#include "../common/mpui.h"

enum { K_TYP, K_DRV, K_TON, K_MIX, K_LVL, K_GAT, K_DCY, K_RES, KNOBS };
#define OLD_KNOBS 5                                  /* 0.4's: its saves keep these five a track */
enum { T_OFF, T_TAPE, T_TUBE, T_FUZZ, T_FOLD, TYPES };
static const char *const kname[KNOBS] = { "TYP", "DRV", "TON", "MIX", "LVL", "LPG", "DCY", "RES" };
static const char *const gname[4] = { "OFF", "LP", "VCA", "BTH" };
static const char *const tname[TYPES] = { "OFF", "TAP", "TUB", "FUZ", "FLD" };
static const int8_t kdef[KNOBS] = { T_OFF, 40, 127, 127, 100, 0, 50, 20 };
static const int8_t kmax[KNOBS] = { TYPES - 1, 127, 127, 127, 127, 3, 127, 127 };
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

/* the gate's decay speed per DCY (VILNIS's table, tau 15 ms .. 4 s, as a curve: x 0.9576 a step), worked
 * out in the UI task when DCY turns, so it costs no table */
static volatile int32_t lpg_dk[8] = { 0 };
static int32_t dk_for(int32_t v) { int32_t x = 1455 * 16; while (v-- > 0) x = (x * 62754) >> 16; x = (x + 8) >> 4; return x < 1 ? 1 : x; }

static struct {
    int32_t lp;                     /* the drive's tone filter */
    int32_t lpg, peak, rising;      /* 0.5: the gate's level (Q30), where a strike rises to, rising or not */
    int32_t low, band;              /* the gate's filter */
} st[8];

/* 0.5: the low pass gate, VILNIS's: struck by every voice start, it moves once a block (a ramp across the
 * block); its filter is a state-variable low pass that opens with it. Built small: it is a simple loop. */
static void __attribute__((noinline, optimize("Os", "no-tree-loop-distribute-patterns"))) lpg(int t, int32_t *b, int32_t gate, int32_t start)
{
    int32_t L = st[t].lpg, L0 = L;
    if (start) {
        int32_t vel = ui_clamp(fw_voice_vel[t] >> 8, 0, 127);
        st[t].peak = (1 << 29) + vel * (1 << 22);                   /* 0.5 .. 1.0 by velocity */
        st[t].rising = 1;
    }
    if (st[t].rising) {
        L += (st[t].peak - L) - ((st[t].peak - L) >> 2);            /* a strike: 3/4 of the way a block */
        if (L >= st[t].peak - (st[t].peak >> 6))
            st[t].rising = 0;
    } else {
        int32_t dk = lpg_dk[t] ? lpg_dk[t] : 1455;
        L -= (L >> 20) * (dk + ((dk * (L >> 15)) >> 13)) * 32;      /* faster while it is high */
        if (L < 0)
            L = 0;
    }
    int32_t l15 = L0 >> 15, l15_end = L >> 15, l15_step = (l15_end - l15) / 32;
    int32_t lp = gate == 1 || gate == 3, vca = gate >= 2;
    int32_t f = 330 + ((((l15_end * l15_end) >> 15) * 29000) >> 15);
    int32_t open = l15_end > 24576 ? (l15_end - 24576) * 4 : 0;     /* wide open: let the top through */
    int32_t q = 32768 - kv[t][K_RES] * 242;                         /* damping 2.0 .. 0.12, Q14 */
    int32_t low = st[t].low, band = st[t].band;
    for (int i = 0; i < 32; i++) {
        int32_t x = b[i] >> 16;
        if (lp) {
            low = ui_clamp(low + ((f * band) >> 15), -65535, 65535);
            int32_t high = ui_clamp(x - low - ((q * band) >> 14), -65535, 65535);
            band = ui_clamp(band + ((f * high) >> 15), -65535, 65535);
            x = open ? low + ((((x - low) >> 1) * open) >> 14) : low;
        }
        if (vca) {
            x = (x * l15) >> 15;
            l15 += l15_step;
        }
        b[i] = ui_clamp(x, -32767, 32767) << 16;
    }
    st[t].lpg = L; st[t].low = low; st[t].band = band;
}

void dr_render(int32_t *blocks)
{
    uint32_t starts = fw_voice_start;
    for (int t = 0; t < 8; t++) {
        int32_t type = kv[t][K_TYP], gate = kv[t][K_GAT];
        if (type == T_OFF && gate == 0)
            continue;
        int32_t *b = &blocks[32 * t];
        if (type != T_OFF) {
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
        if (gate == 0)
            continue;
        lpg(t, b, gate, (starts >> t) & 1);
    }
}

/* ---- saving ------------------------------------------------------------------------------------------------ */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
#define MAGIC 0x44525645u                            /* "DRVE" */
HEAP_BUF(block, 512)          /* borrowed from the heap while loading or saving */
static int32_t save_wait, loaded, ticks;

/* 0.4: everything from here on is the saving, the screen and the knobs: built small. The sound above is unchanged. */
#pragma GCC optimize ("Os", "no-tree-loop-distribute-patterns", "no-optimize-strlen")
#define MAGIC2 0x44525632u                           /* "DRV2": 0.5's, with the gate */
#define NEW_AT (8 + 8 * OLD_KNOBS)                   /* the gate's three a track, after 0.4's five */
static uint32_t sum_of(int32_t n) { uint32_t sum = 0; for (int i = 8; i < 8 + n; i++) sum = sum * 31u + block[i]; return sum; }
/* the settings go with the project (common/projsave.h, through PROJECT LINK): each slot's copy at
 * PJ_BASE + slot x PJ_NB */
#define PJ_BASE 0x62280
#define PJ_NB 1
#include "../common/projsave.h"
static void load(void)
{
    if (!block_ready()) return;
    uint32_t *w = (uint32_t *)block;
    for (int t = 0; t < 8; t++) for (int k = 0; k < KNOBS; k++) kv[t][k] = kdef[k];
    for (int i = 0; i < 512; i++) block[i] = 0;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0)
        return;
    int32_t v2 = w[0] == MAGIC2 && w[1] == sum_of(8 * KNOBS);
    if (!v2 && !(w[0] == MAGIC && w[1] == sum_of(8 * OLD_KNOBS)))
        return;
    for (int t = 0; t < 8; t++) for (int k = 0; k < KNOBS; k++) {
        int32_t at = k < OLD_KNOBS ? 8 + t * OLD_KNOBS + k : NEW_AT + t * (KNOBS - OLD_KNOBS) + (k - OLD_KNOBS);
        if (k < OLD_KNOBS || v2)
            kv[t][k] = (int8_t)ui_clamp((int8_t)block[at], 0, kmax[k]);
    }
}
static void save(void)
{
    if (!block_ready()) return;
    uint32_t *w = (uint32_t *)block;
    for (int i = 0; i < 512; i++) block[i] = 0;
    for (int t = 0; t < 8; t++) for (int k = 0; k < KNOBS; k++) {
        int32_t at = k < OLD_KNOBS ? 8 + t * OLD_KNOBS + k : NEW_AT + t * (KNOBS - OLD_KNOBS) + (k - OLD_KNOBS);
        block[at] = (uint8_t)kv[t][k];
    }
    w[0] = MAGIC2; w[1] = sum_of(8 * KNOBS);
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}
static void pj_defaults(void) { for (int t = 0; t < 8; t++) for (int k = 0; k < KNOBS; k++) kv[t][k] = kdef[k]; }

/* ---- the page ------------------------------------------------------------------------------------------ */
static int32_t turned[KNOBS], hl_k = -1, hl_ticks;

static void dr_draw(void *view, void *bmp)
{
    char s[24];
    (void)view;
    int32_t t = active(), type = kv[t][K_TYP];
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
        if (k == K_TYP || k == K_GAT) { a = k == K_TYP ? tname[v] : gname[v]; p = s; while ((*p++ = *a++)) ; }
        else ui_num(s, v);
        ui_knob(bmp, k, kname[k], s, v, kmax[k], k == K_TYP || k == K_GAT ? 2 : 0, k == hl_k && hl_ticks > 0);
    }
}

static int32_t dr_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > KNOBS)
        return 0;
    int32_t t = active(), k = knob - 1, per = k == K_TYP || k == K_GAT ? 16 : 4;
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
    if (loaded) pj_tick();                           /* a project loaded or saved */
    if (save_wait > 0 && --save_wait == 0) save();
    if (hl_ticks > 0 && --hl_ticks == 0 && core_mview) fw_invalidate(core_mview);
    static int8_t dk_v[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    for (int t = 0; t < 8; t++)                     /* 0.5: the gate's decay, when DCY changed */
        if (dk_v[t] != kv[t][K_DCY]) { dk_v[t] = kv[t][K_DCY]; lpg_dk[t] = dk_for(kv[t][K_DCY]); }
}

struct core_mpage dr_page = { 99, "DRV", "Drive", dr_draw, mp_key, dr_enc, 0, 0, dr_clear };
