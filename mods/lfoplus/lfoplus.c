/* SPDX-License-Identifier: GPL-2.0-or-later */
/* LFO+ for the Digitakt mk1 (OS 1.54): six new LFO waves and four slower speeds.
 *
 * WAVE gets six more shapes after RND (audio and MIDI tracks):
 *   PLS  pulse: high for the first quarter of the cycle, low for the rest (a rhythmic gate)
 *   STP  staircase: four steps up, then back down to the start
 *   TRP  trapezoid: rise, hold high, fall, hold low
 *   BMP  double bump: two smooth humps per cycle
 *   FIN  shark fin: fast curved rise, then a curved fall
 *   BNC  bouncing ball: four bounces, each half as high and shorter than the one before
 * MULT (audio tracks) gets four slower values after the stock 24: /2 /4 /8 /16 in time with the
 * tempo, then /2 /4 /8 /16 free-running, so an LFO can take 2, 4, 8 or 16 times longer than at 1.
 *
 * How it works (OS 1.54, found by reading its code):
 *   - the LFO engines (0x400ed766 audio, 0x400ed438 MIDI) clamp WAVE to 0..6, then call the wave's
 *     routine from a table of six at 0x401bce18 with the phase (32 bits, one cycle); RND (6) is
 *     handled inline. The clamps now allow 0..12 and the engines read our table of 13, whose first
 *     six entries are the stock routines. Three small tables of hold values for the ONE and HLF trig
 *     modes, indexed by wave, are extended the same way (new waves hold at 0, as most stock waves do).
 *   - WAVE's and MULT's parameter records hold their largest value (6 and 23, in 8.8): raised to 12
 *     and 31, so the knob reaches the new values. Their on-screen text comes from two formatters
 *     the firmware registers at start-up: the registration now points at ours (lfoplus.s), which
 *     hand the stock values to the stock formatters.
 *   - the audio engine's speed: rate = speed x tempo >> (11 - n), n the MULT step 0..11 (12..23 the
 *     same steps free-running at a fixed 14400). Our step (lfoplus.s) maps 24..27 and 28..31 to
 *     n = -1..-4, a shift of 12..15: half, a quarter, an eighth, a sixteenth of MULT 1.
 *
 *   - the LFO page draws WAVE as a scrolling graph (0x4009700a): see the end of this file.
 *
 * The wave routines run in the sound render, at interrupt level: plain arithmetic, nothing else. */
#include <stdint.h>
#include "digitakt-mk1/core3.h"

#define HI  0x7fffffff
#define LO  (-0x7fffffff - 1)

/* 0..65535 (bottom to top) as the engine's full range */
static int32_t level(uint32_t u)
{
    if (u > 65535) u = 65535;
    return (int32_t)((u << 16) ^ 0x80000000u);
}

/* a hump over t 0..65535: 0 at both ends, 65535 in the middle */
static uint32_t hump(uint32_t t)
{
    uint32_t s = (t * (65536 - t)) >> 14;
    return s > 65535 ? 65535 : s;
}

int32_t lfo_pls(uint32_t phase)
{
    return phase < 0x40000000u ? HI : LO;
}

int32_t lfo_stp(uint32_t phase)
{
    return level((phase >> 30) * 21845u);
}

int32_t lfo_trp(uint32_t phase)
{
    uint32_t q = phase >> 16;                   /* 0..65535 */
    if (q < 8192) return level(q * 8);          /* rise: an eighth */
    if (q < 32768) return HI;                   /* high: three eighths */
    if (q < 40960) return level((40960 - q) * 8);   /* fall: an eighth */
    return LO;                                  /* low: three eighths */
}

int32_t lfo_bmp(uint32_t phase)
{
    return level(hump((phase << 1) >> 16));
}

int32_t lfo_fin(uint32_t phase)
{
    uint32_t q = phase >> 16, x, r;
    if (q < 32768) {                            /* rise: fast, then easing into the top */
        x = 65535 - q * 2;
        r = 65535 - ((x * x) >> 16);
    } else {                                    /* fall: fast, then easing into the bottom */
        x = 65535 - (q - 32768) * 2;
        r = (x * x) >> 16;
    }
    return level(r);
}

int32_t lfo_bnc(uint32_t phase)
{
    uint32_t q = phase >> 16;                   /* four bounces: 1/2, 1/4, 1/8, 1/8 of the cycle */
    if (q < 32768) return level(hump(q << 1));              /* full height */
    if (q < 49152) return level(hump((q - 32768) << 2) >> 1);   /* half */
    if (q < 57344) return level(hump((q - 49152) << 3) >> 2);   /* a quarter */
    return level(hump((q - 57344) << 3) >> 3);              /* an eighth */
}

/* ---- the Alphabase shapes (0.3) ----------------------------------------------------------------- *
 * Sixteen of the 64 LFO shapes of the Jomox Alphabase, from its wave file (256 points a cycle), chosen
 * to leave out the ones the Digitakt or LFO+ already has and most of its 30 pulse widths. jomox.h is
 * made from the file by gen_jomox.py. Each point is read in turn as the phase goes round, and the
 * output slides in a straight line to the next point, so a smooth shape stays smooth at slow speeds. */
#include "jomox.h"

static int32_t table_wave(const int16_t *t, uint32_t phase)
{
    uint32_t i = phase >> 24, f = (phase >> 9) & 0x7fff;    /* the point, and how far to the next */
    int32_t a = t[i], b = t[(i + 1) & 255];
    /* wraps in unsigned arithmetic on the way; the sum, between the two points, does not */
    return (int32_t)(((uint32_t)a << 16) + ((uint32_t)((b - a) * (int32_t)f) << 1));
}

#define JX(k) int32_t lfo_jx##k(uint32_t phase) { return table_wave(jx_table[k], phase); }
JX(0) JX(1) JX(2) JX(3) JX(4) JX(5) JX(6) JX(7) JX(8) JX(9) JX(10) JX(11) JX(12) JX(13) JX(14) JX(15)

#define WAVES 29                                /* 0..6 stock, 7..12 LFO+'s, 13..28 the Alphabase's */

/* the wave table the engines read: the six stock routines, a stand-in for RND (never called: the
 * engines handle 6 themselves), then ours */
typedef int32_t (*wave_fn)(uint32_t phase);
const wave_fn lfo_waves[WAVES] = {
    (wave_fn)0x400ed292, (wave_fn)0x400ed2a8, (wave_fn)0x400ed274, (wave_fn)0x400ed286,
    (wave_fn)0x400ed302, (wave_fn)0x400ed2f2, (wave_fn)0x400ed292,
    lfo_pls, lfo_stp, lfo_trp, lfo_bmp, lfo_fin, lfo_bnc,
    lfo_jx0, lfo_jx1, lfo_jx2, lfo_jx3, lfo_jx4, lfo_jx5, lfo_jx6, lfo_jx7,
    lfo_jx8, lfo_jx9, lfo_jx10, lfo_jx11, lfo_jx12, lfo_jx13, lfo_jx14, lfo_jx15,
};

/* the ONE / HLF hold values by wave: the stock seven, then 0 */
const int32_t lfo_hold_half[WAVES] = {0, 0, 0, 0, 0, HI, 0};       /* stock 0x401bcdc4 */
const int32_t lfo_hold_up[WAVES]   = {0, 0, 0, 0, 0, 0, 0};        /* stock 0x401bcde0 */
const int32_t lfo_hold_down[WAVES] = {0, 0, 0, 0, HI, 0, 0};       /* stock 0x401bcdfc */

/* WAVE's text for the new values */
const char *const lfo_wave_names[WAVES - 7] = {"PLS", "STP", "TRP", "BMP", "FIN", "BNC", JX_NAMES};

/* 0.2 and 0.3 drew 19 x 17 pictures through the knob-picture code (0x4006047a, 0x4000f2bc); the LFO
 * page never draws WAVE that way, so 0.5 drops them. The page's graph is below. */
struct icon { const void *vt; int32_t width, height, one; const uint32_t *px, *mask; int32_t zero; };

/* ---- the LFO page's wave graph (0.4) --------------------------------------------------------------- *
 * The big bracketed graph under WAVE/PHAS is not a knob picture: 0x4009700a draws it. It holds WAVE to
 * 0..6 (so every new wave showed RND), then tiles one cycle of the wave, scrolled by PHAS, from a
 * "sheet" of four 28 x 15 frames per wave (as is, upside down, back to front, both: the page picks one
 * by the depth's sign and the mode). The sheets of the stock seven sit in RAM at 0x4208d6b4, 12 bytes
 * each (first, end, capacity of the frame list). lfoplus.s lets the hold reach 28 and, for 7..28,
 * points the routine at the sheets below instead. The frames are made by gen_jomox.py, which draws the
 * stock TRI frame exactly the same way as a check. */
#define GRAPH_MASK ((const uint32_t *)0x4022e724)    /* the stock graphs' mask: every row */
static const uint32_t graph_px[WAVES - 7][4][28] = { GRAPH_FRAMES };
#define GF(k, f) {fw_bitmap_vt, 28, 15, 1, graph_px[k][f], GRAPH_MASK, 0}
#define GW(k) GF(k, 0), GF(k, 1), GF(k, 2), GF(k, 3)
static const struct icon graph_frames[WAVES - 7][4] = {
    {GW(0)}, {GW(1)}, {GW(2)}, {GW(3)}, {GW(4)}, {GW(5)}, {GW(6)}, {GW(7)}, {GW(8)}, {GW(9)}, {GW(10)},
    {GW(11)}, {GW(12)}, {GW(13)}, {GW(14)}, {GW(15)}, {GW(16)}, {GW(17)}, {GW(18)}, {GW(19)}, {GW(20)},
    {GW(21)},
};
struct sheet { const struct icon *first, *end, *cap; };
#define SH(k) {graph_frames[k], graph_frames[k] + 4, graph_frames[k] + 4}
const struct sheet lfo_graph_sheets[WAVES - 7] = {
    SH(0), SH(1), SH(2), SH(3), SH(4), SH(5), SH(6), SH(7), SH(8), SH(9), SH(10),
    SH(11), SH(12), SH(13), SH(14), SH(15), SH(16), SH(17), SH(18), SH(19), SH(20), SH(21),
};
