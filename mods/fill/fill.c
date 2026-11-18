/* SPDX-License-Identifier: GPL-2.0-or-later */
/* FILL for the Digitakt mk1 (OS 1.54): hold YES, then hold NO, and the pattern plays a random variation
 * of itself for as long as NO is held: a new one each loop. Let go and it plays as it is: the pattern
 * itself never changes. While you hold it, any knob sets how wild it is (AMOUNT, 0-100 %, saved).
 *
 * What the variation does, on the audio tracks (seq-events, at interrupt level):
 *   - shuffle: a step plays a neighbour's trig instead (one or two steps either side, or a beat back)
 *   - ghost notes: an empty step plays, softly
 *   - drops: a trig is left out (never on the first step of each half bar)
 *   - accents: the velocities move up and down
 * Each choice comes from a hash of the step, the track, the loop (seq-events' parity) and a seed drawn
 * each time FILL starts, so both reads of a step agree and every loop is different. */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"

static volatile int32_t on, amount = 50;
static volatile uint32_t seed = 0x1234567u;
static volatile uint32_t ghost[SQ_AUDIO][2];            /* bit s: step s is a ghost note this loop */

static uint32_t hash(int32_t track, int32_t step, uint32_t salt)
{
    uint32_t h = seed ^ salt ^ (uint32_t)track * 0x85ebca6bu ^ (uint32_t)step * 0xc2b2ae35u ^
                 sq_clock[track].parity * 0x27d4eb2fu;
    h ^= h >> 16; h *= 0x7feb352du;
    h ^= h >> 15; h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

/* ---- the variation (interrupt level) ---------------------------------------------------------------- */
int32_t fl_fire_step(int32_t track, int32_t step, int32_t len)
{
    static const int8_t off[8] = { -1, 1, -2, 2, -4, -1, 1, -4 };
    if (!on || track >= SQ_AUDIO || len < 4 || (step & 7) == 0)
        return step;
    uint32_t h = hash(track, step, 0x51ed270bu);
    if ((int32_t)(h % 100u) >= amount * 30 / 100)
        return step;
    return (step + off[(h >> 8) & 7] + len * 4) % len;      /* a neighbour's trig */
}

void fl_gen_trig(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    if (!on || track >= SQ_AUDIO || step < 0 || step >= SQ_STEPS)
        return;
    uint32_t h = hash(track, step, 0x2545f491u);
    uint32_t bit = 1u << (step & 31);
    volatile uint32_t *g = &ghost[track][step >> 5];
    int32_t is_ghost = 0;
    if (!(*trig & SQ_TRIG)) {
        if ((int32_t)(h % 100u) < amount * 35 / 100) {
            *trig |= SQ_TRIG;                               /* a ghost note */
            is_ghost = 1;
        }
    } else if ((step & 7) && (int32_t)((h >> 8) % 100u) < amount * 15 / 100)
        *trig &= ~SQ_TRIG;                                  /* dropped */
    if (first)
        *g = is_ghost ? *g | bit : *g & ~bit;
}

void fl_fire_vel(int32_t track, int32_t step, int32_t *vel)
{
    if (!on || track >= SQ_AUDIO || step < 0 || step >= SQ_STEPS)
        return;
    if ((ghost[track][step >> 5] >> (step & 31)) & 1u) {
        *vel = *vel * 40 / 100;                             /* ghosts are soft */
        return;
    }
    int32_t v = amount * 30 / 100;
    if (v)
        *vel += (int32_t)(hash(track, step, 0x9e3779b9u) % (uint32_t)(2 * v + 1)) - v;
}

/* ---- saving the amount -------------------------------------------------------------------------------- */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
#define SAVE_MAGIC 0x46494c4cu                          /* "FILL" */
static uint8_t block[512] __attribute__((aligned(16)));
static int32_t save_wait, loaded, ticks;

static void load(void)
{
    uint32_t *w = (uint32_t *)block;
    for (int32_t i = 0; i < 512; i++) block[i] = 0;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0)
        return;
    if (w[0] == SAVE_MAGIC && w[1] <= 100 && w[2] == (w[1] ^ 0xa5a5a5a5u))
        amount = (int32_t)w[1];
}

static void save(void)
{
    uint32_t *w = (uint32_t *)block;
    for (int32_t i = 0; i < 512; i++) block[i] = 0;
    w[0] = SAVE_MAGIC;
    w[1] = (uint32_t)amount;
    w[2] = w[1] ^ 0xa5a5a5a5u;
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}

/* ---- keys, knobs, the box at the top (UI task) ------------------------------------------------------- */
#define K_YES 12
#define K_NO  13
#define F_DOWN    0x01
#define F_RELEASE 0x10
#define F_REPEAT  0x08

static int32_t yes_down, no_ours, shown_ticks;
static uint32_t entropy = 0x6d2b79f5u;

int32_t fl_key(void *brain, void *ev)
{
    int32_t key = *(int32_t *)((uint8_t *)ev + 12), fl = *(int32_t *)((uint8_t *)ev + 16);
    int32_t down = (fl & F_DOWN) && !(fl & F_RELEASE), press = down && !(fl & F_REPEAT);
    (void)brain;
    entropy = entropy * 1664525u + 1013904223u + (uint32_t)key;
    if (key == K_YES)
        yes_down = down;
    if (key != K_NO)
        return 0;
    if (no_ours) {                                          /* the NO that started FILL */
        if (!down) {
            no_ours = 0;
            on = 0;
        }
        return 1;
    }
    if (press && yes_down) {
        seed = entropy ^ (uint32_t)ticks * 2654435761u;     /* a new variation each time */
        on = 1;
        no_ours = 1;
        return 1;
    }
    return 0;
}

int32_t fl_enc(void *brain, void *ev)
{
    static int32_t acc;
    int32_t delta = *(int32_t *)((uint8_t *)ev + 16), v;
    (void)brain;
    if (!on)
        return 0;
    if ((acc > 0) != (delta > 0))
        acc = 0;
    acc += delta;
    while (acc >= 4 || acc <= -4) {                         /* one 5 % step a click */
        int32_t dir = acc > 0 ? 1 : -1;
        acc -= dir * 4;
        v = amount + dir * 5;
        amount = v < 0 ? 0 : v > 100 ? 100 : v;
        save_wait = 60;
    }
    return 1;                                               /* the knobs are the amount while held */
}

void fl_draw(void *bmp, void *ctrl)
{
    (void)ctrl;
    if (!on)
        return;
    int32_t a = amount;
    char t[12] = "FILL ";
    char *p = t + 5;
    if (a >= 100) *p++ = '1';
    if (a >= 10) *p++ = (char)('0' + a / 10 % 10);
    *p++ = (char)('0' + a % 10);
    *p++ = '%';
    *p = 0;
    fw_fillrect(bmp, 38, 54, 88, 63, 0);
    fw_framerect(bmp, 38, 54, 88, 63, 1);
    fw_textf(bmp, fw_font5, 44, 56, 12, "%s", t);
}

void fl_tick(void *ctrl)
{
    if (!loaded && ++ticks > 30) {
        loaded = 1;
        load();
    }
    ticks++;
    if (save_wait > 0 && --save_wait == 0)
        save();
    if (on || shown_ticks) {
        *((uint8_t *)ctrl + 0x20) = 1;                      /* redraw: the box comes and goes */
        shown_ticks = on ? 2 : shown_ticks - 1;
    }
}
