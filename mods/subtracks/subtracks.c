/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SUBTRACKS 0.1 (test build) for the Digitakt mk1 (OS 1.54): four lanes on each audio track, after the
 * Tonverk's Subtracks machine, sharing the track's one voice.
 *
 *   lane 1      the track itself: its own trigs, its own sound, as stock
 *   lanes 2-4   16-step lanes the mod keeps, each with its own sample and settings, played as
 *               parameter locks on the track's voice (seq-events' ev_fire_locks)
 *
 * One voice: a hit cuts the one before (a choke group). Two lanes on one step: the lowest lane plays.
 * A lane setting left at "--" follows the track's sound.
 *
 * TRK twice: the SUB page for the active track. UP / DOWN picks the lane, trig keys 1-16 turn the
 * lane's steps on and off, knobs A-G set its SAMP, TUNE, STRT, LEV, FREQ, RESO and PAN, FUNC + trig
 * 1-4 mutes and unmutes a lane, NO (or TRK twice) closes.
 *
 * 0.1 is a test of the playback: the lanes are kept for all patterns at once and are not saved yet
 * (they are gone at power off). */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"

#define LANES   4                       /* lane 0 is the track; 1-3 the mod's */
#define VALS    7
static const uint8_t val_slot[VALS] = { 20, 17, 21, 24, 26, 27, 44 };  /* SAMP TUNE STRT LEV FREQ RESO PAN */
static const char *const val_name[VALS] = { "SMP", "TUN", "STR", "LEV", "FRQ", "RES", "PAN" };

static volatile uint16_t steps[SQ_AUDIO][LANES];        /* bit s: the lane plays step s (of 16) */
static volatile uint8_t val[SQ_AUDIO][LANES][VALS];     /* 0: follow the track; else the value + 1 */
static volatile uint8_t muted[SQ_AUDIO];                /* bit l: lane l muted */
static volatile int8_t win_lane[SQ_AUDIO];              /* the lane the last trig test chose, -1 none */
static volatile int8_t win_step[SQ_AUDIO];

/* a lock of exactly 0 reads as no lock on the mk1, so 0 goes out as 0x0001 (FREQSEQ 0.5) */
static int16_t nz(int32_t v) { return (int16_t)(v ? v : 1); }

/* ---- the sequencer (interrupt level) --------------------------------------------------------------- */
void st_gen_trig(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    (void)first;
    if (track < 0 || track >= SQ_AUDIO || step < 0 || step >= SQ_STEPS)
        return;
    int32_t s = step & 15, lane = -1;
    uint32_t m = muted[track];
    if (*trig & SQ_TRIG) {
        if (m & 1u)
            *trig &= ~SQ_TRIG;                  /* lane 1 muted: the track's own trig is left out */
        else
            lane = 0;                           /* the lowest lane wins: the track's own trig */
    }
    for (int32_t l = 1; lane < 0 && l < LANES; l++)
        if (!(m & (1u << l)) && ((steps[track][l] >> s) & 1u)) {
            *trig |= SQ_TRIG;
            lane = l;
        }
    win_lane[track] = (int8_t)lane;
    win_step[track] = (int8_t)step;
}

int32_t st_fire_locks(int32_t track, int32_t step, struct sq_lock *out, int32_t room)
{
    if (track < 0 || track >= SQ_AUDIO || win_step[track] != step)
        return 0;
    int32_t l = win_lane[track], n = 0;
    if (l < 1)
        return 0;                               /* the track's own trig: its own sound */
    for (int32_t k = 0; k < VALS && n < room; k++) {
        int32_t v = val[track][l][k];
        if (!v)
            continue;
        out[n].slot = val_slot[k];
        out[n].value = nz((v - 1) << 8);
        n++;
    }
    return n;
}

/* ---- the SUB page (UI task) -------------------------------------------------------------------------- */
#define K_FUNC  1
#define K_TRK   2
#define K_NO    13
#define K_UP    14
#define K_DOWN  15
#define K_TRIG1 24
#define F_DOWN    0x01
#define F_DOUBLE  0x04
#define F_REPEAT  0x08
#define F_RELEASE 0x10
#define KNOB_CLICK 2
#define DOUBLE_TICKS 10                         /* a third of a second at 30 ticks */

static int32_t ui_open, sel, func_down, trk_down, swallow_trk, swallow_no;
static int32_t ticks, trk_up_at = -100, other_key;

static int32_t active(void) { int32_t t = (int32_t)fw_active_track; return t < 0 ? 0 : t > 7 ? 7 : t; }

int32_t st_key(void *brain, void *ev)
{
    int32_t key = *(int32_t *)((uint8_t *)ev + 12), fl = *(int32_t *)((uint8_t *)ev + 16);
    int32_t down = (fl & F_DOWN) && !(fl & F_RELEASE), press = down && !(fl & F_REPEAT);
    (void)brain;
    if (key == K_FUNC)
        func_down = down;
    if (key == K_TRK) {
        trk_down = down;
        if (swallow_trk) {                      /* the release of the TRK that opened or closed us */
            if (!down) { swallow_trk = 0; trk_up_at = -100; }
            return 1;
        }
        if (press && ((fl & F_DOUBLE) || (!other_key && ticks - trk_up_at <= DOUBLE_TICKS))) {
            ui_open = !ui_open;
            swallow_trk = 1;
            return 1;
        }
        if (press)
            other_key = 0;
        if (!down)
            trk_up_at = other_key ? -100 : ticks;
        return 0;                               /* a single TRK stays stock's */
    }
    if (down)
        other_key = 1;                          /* TRK + something is not a double press */
    if (key == K_NO && swallow_no) {
        if (!down) swallow_no = 0;
        return 1;
    }
    if (!ui_open)
        return 0;
    int32_t t = active();
    if (key == K_NO) {
        if (press) { ui_open = 0; swallow_no = 1; }
        return 1;
    }
    if ((key == K_UP || key == K_DOWN) && !trk_down) {   /* TRK + UP / DOWN stays TRACK SOLO's */
        if (press) sel = (sel + (key == K_DOWN ? 1 : LANES - 1)) % LANES;
        return 1;
    }
    if (key >= K_TRIG1 && key < K_TRIG1 + 16 && !trk_down) {
        int32_t k = key - K_TRIG1;
        if (press) {
            if (func_down) {
                if (k < LANES) muted[t] ^= (uint8_t)(1u << k);
            } else if (sel > 0)
                steps[t][sel] ^= (uint16_t)(1u << k);
            sq_rearm(1u << t);                  /* a stopped track prepares its first step again */
        }
        return 1;
    }
    return 0;
}

int32_t st_enc(void *brain, void *ev)
{
    int32_t enc = *(int32_t *)((uint8_t *)ev + 12), delta = *(int32_t *)((uint8_t *)ev + 16);
    static int32_t acc[9];
    (void)brain;
    if (!ui_open)
        return 0;
    if (enc >= 1 && enc <= VALS && delta && sel > 0) {
        if ((acc[enc] > 0) != (delta > 0))
            acc[enc] = 0;
        acc[enc] += delta;
        while (acc[enc] >= KNOB_CLICK || acc[enc] <= -KNOB_CLICK) {
            int32_t dir = acc[enc] > 0 ? 1 : -1;
            acc[enc] -= dir * KNOB_CLICK;
            int32_t v = (int32_t)val[active()][sel][enc - 1] + dir;
            val[active()][sel][enc - 1] = (uint8_t)(v < 0 ? 0 : v > 128 ? 128 : v);
        }
    }
    return 1;                                   /* every knob is ours while the page shows */
}

/* ---- drawing (y = 0 is the bottom row; 128 x 64) ---------------------------------------------------- */
static void text(void *bmp, int32_t x, int32_t y, const char *s) { fw_textf(bmp, fw_font5, x, y, 24, "%s", s); }

static void num(char *b, int32_t v)
{
    char d[6]; int32_t n = 0;
    if (v < 0) { *b++ = '-'; v = -v; }
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *b++ = d[--n];
    *b = 0;
}

void st_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    if (!ui_open)
        return;
    int32_t t = active();
    char b[24];
    fw_fillrect(bmp, 0, 0, 127, 63, 0);

    /* the top: the track, then the four lanes: the selected one framed, a muted one hollow */
    b[0] = 'S'; b[1] = 'U'; b[2] = 'B'; b[3] = ' '; b[4] = 'T'; num(b + 5, t + 1);
    text(bmp, 1, 56, b);
    for (int32_t l = 0; l < LANES; l++) {
        int32_t x = 52 + 19 * l;
        char c[2] = { (char)('1' + l), 0 };
        if (l == sel) fw_framerect(bmp, x - 2, 54, x + 14, 63, 1);
        text(bmp, x, 56, c);
        if (muted[t] & (1u << l)) fw_framerect(bmp, x + 6, 57, x + 11, 61, 1);
        else fw_fillrect(bmp, x + 6, 57, x + 11, 61, 1);
    }

    /* the steps: the selected lane's 16, the playing one underlined */
    uint32_t bits = sel ? steps[t][sel] : 0;
    if (!sel) {
        uint64_t st = sq_stored(t);
        uint32_t lo = (uint32_t)st, hi = (uint32_t)(st >> 32);   /* a fixed shift: no libgcc call */
        bits = (lo | lo >> 16 | hi | hi >> 16) & 0xffffu;          /* any of the 4 pages, folded onto 16 */
    }
    uint32_t last = sq_clock[t].last;
    for (int32_t s = 0; s < 16; s++) {
        int32_t x = 1 + 8 * s;
        if ((bits >> s) & 1u) fw_fillrect(bmp, x, 38, x + 5, 47, 1);
        else fw_framerect(bmp, x, 38, x + 5, 47, 1);
        if (last != SQ_CLOCK_NONE && (int32_t)(last & 15) == s) fw_fillrect(bmp, x, 35, x + 5, 36, 1);
    }

    /* the bottom: the lane's settings (lane 1 is the track's own sound) */
    if (!sel) {
        text(bmp, 1, 18, "LANE 1 IS THE TRACK ITSELF:");
        text(bmp, 1, 8, "ITS OWN TRIGS AND SOUND");
        return;
    }
    for (int32_t k = 0; k < VALS; k++) {
        int32_t x = 1 + 18 * k, v = val[t][sel][k];
        text(bmp, x, 14, val_name[k]);
        if (!v) { b[0] = '-'; b[1] = '-'; b[2] = 0; }
        else num(b, v - 1);
        text(bmp, x, 4, b);
    }
}

void st_tick(void *ctrl)
{
    ticks++;
    if (ui_open)
        *((uint8_t *)ctrl + 0x20) = 1;          /* redraw: the playing step moves */
}
