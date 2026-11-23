/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Euclid Accents for the Digitakt mk1 (OS 1.54), after the Analog Rytm mod of the same name: a
 * Euclidean rhythm accents the trigs you already have, instead of adding new ones.
 *
 * SETTINGS rows:
 *   ACC DEPTH      0-64: how much louder accented trigs play, and how much softer the others (half
 *                  each way: DEPTH 24 means +12 on an accent, -12 off it)
 *   ACC T1 .. T8   OFF, 1-16: the accents on that audio track, spread as evenly as they go over the
 *                  track's length (5 on a 16-step track: steps 1, 5, 8, 11, 14). The first step is
 *                  always an accent.
 * A trig on an accent step plays louder, one off it softer; empty steps stay empty.
 * Saved two seconds after the last change in one eMMC block (SAVE_BLOCK), read back at power-on.
 *
 * How it works: seq-events' ev_fire_vel, the velocity a note trig is about to play with, and
 * sq_track_info for the track's length. Nothing in the pattern changes. */
#include <stdint.h>
#include "../common/heapbuf.h"
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"

typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);   /* 0 on success */
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)

#define ROWS 9                                  /* DEPTH, then T1-T8 */
#define ROW_DEPTH 0
static volatile uint8_t val[ROWS] = {24};       /* DEPTH 0-64; hits 0 (OFF)-16 per track */
static int32_t save_wait, loaded, ticks;

/* ---- the sequencer's fire, at interrupt level ------------------------------------------------------ */
void acc_vel(int32_t track, int32_t step, int32_t *vel)
{
    struct sq_track info;
    int32_t k, len, s, half = val[ROW_DEPTH] / 2;
    if (track >= SQ_AUDIO || !half || !(k = val[1 + track]))
        return;
    if (sq_track_info(track, &info, sizeof info) != 0 || info.len < 1)
        return;
    len = info.len;
    if (k >= len)                               /* every step an accent: nothing to bring out */
        return;
    s = step % len;
    if ((s * k) % len < k)                      /* Bresenham's spread: the Euclidean rhythm E(k, len) */
        *vel += half;
    else
        *vel -= half;                           /* seq-events keeps it in 0-127 */
}

/* ---- saving ---------------------------------------------------------------------------------------- */
#define SAVE_MAGIC 0x45414343u                  /* "EACC" */
struct save { uint32_t magic; uint8_t version, val[ROWS]; uint8_t pad[2]; uint32_t sum; };
HEAP_BUF(block, 512)          /* borrowed from the heap while loading or saving */

static uint32_t checksum(const uint8_t *p, int32_t n)
{
    uint32_t s = 0x5eed1ed5u;
    int32_t i;
    for (i = 0; i < n; i++)
        s = (s << 5 | s >> 27) ^ p[i];
    return s;
}

static void load(void)
{
    if (!block_ready()) return;
    struct save *s = (struct save *)block;
    int32_t i;
    for (i = 0; i < 512; i++) block[i] = 0;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0)
        return;
    if (s->magic != SAVE_MAGIC || s->version != 1
        || s->sum != checksum(block, (int32_t)((uint8_t *)&s->sum - block)))
        return;
    val[ROW_DEPTH] = s->val[ROW_DEPTH] <= 64 ? s->val[ROW_DEPTH] : 24;
    for (i = 1; i < ROWS; i++)
        val[i] = s->val[i] <= 16 ? s->val[i] : 0;
}

static void save(void)
{
    if (!block_ready()) return;
    struct save *s = (struct save *)block;
    int32_t i;
    for (i = 0; i < 512; i++) block[i] = 0;
    s->magic = SAVE_MAGIC;
    s->version = 1;
    for (i = 0; i < ROWS; i++)
        s->val[i] = val[i];
    s->sum = checksum(block, (int32_t)((uint8_t *)&s->sum - block));
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}

void acc_tick(void *ctrl)
{
    (void)ctrl;
    if (!loaded && ++ticks > 30) {
        loaded = 1;
        load();
    }
    if (save_wait > 0 && --save_wait == 0)
        save();
}

/* ---- SETTINGS rows (the same shape as ledcolors' rows) --------------------------------------------- */
#define XOFF      -4
#define YOFF      0
#define TEXT_ADV  4
#define BOX_DOWN  1
#define BOX_UP    6
#define TOP_LIMIT 52

static void redraw(void **payload)
{
    fw_invalidate((char *)*payload + 0x38);
}

static void step_row(int32_t row, int32_t delta)
{
    int32_t i;
    if (!delta)
        return;
    if (row == ROW_DEPTH) {
        i = val[row] + (delta > 0 ? 4 : -4);
        if (i < 0) i = 0;
        if (i > 64) i = 64;
    } else {
        i = val[row] + (delta > 0 ? 1 : -1);
        if (i < 0) i = 0;
        if (i > 16) i = 16;
    }
    val[row] = (uint8_t)i;
    save_wait = 60;
}

static void draw_row(int32_t row, void *bmp, int32_t x, int32_t y)
{
    char num[4];
    const char *text = num;
    int32_t v = val[row], n = 0, tx;
    if (!v && row != ROW_DEPTH) {
        text = "OFF";
    } else {
        char *p = num;
        if (v >= 10) *p++ = (char)('0' + v / 10);
        *p++ = (char)('0' + v % 10);
        *p = 0;
    }
    while (text[n]) n++;
    if (y + YOFF + BOX_UP > TOP_LIMIT)
        return;
    tx = x + XOFF - TEXT_ADV * n;
    fw_fillrect(bmp, tx - 2, y + YOFF - BOX_DOWN, x + XOFF, y + YOFF + BOX_UP, 0);
    fw_textf(bmp, fw_font5, tx, y + YOFF, 16, "%s", text);
}

#define ROW_FNS(name, row)                                                                        \
    void acc_##name##_select(void **payload, void *item) { (void)item; step_row(row, 1); redraw(payload); } \
    void acc_##name##_change(void **payload, void *item, int32_t delta) { (void)item; step_row(row, delta); redraw(payload); } \
    void acc_##name##_draw(void *payload, void *item, void *bmp, int32_t x, int32_t y) { (void)payload; (void)item; draw_row(row, bmp, x, y); }

ROW_FNS(depth, 0)
ROW_FNS(t1, 1) ROW_FNS(t2, 2) ROW_FNS(t3, 3) ROW_FNS(t4, 4)
ROW_FNS(t5, 5) ROW_FNS(t6, 6) ROW_FNS(t7, 7) ROW_FNS(t8, 8)

extern const uint32_t acc_row_depth[], acc_row_t1[], acc_row_t2[], acc_row_t3[], acc_row_t4[],
    acc_row_t5[], acc_row_t6[], acc_row_t7[], acc_row_t8[];
extern void core_additem(void *menu, const void *row);

extern volatile int32_t modset_closed;   /* MOD SETTINGS: weak, 0 without that mod */

void acc_settings(void *menu)
{
    if (modset_closed)                  /* inside a closed MOD SETTINGS section */
        return;
    static const uint32_t *const rows[ROWS] = {acc_row_depth, acc_row_t1, acc_row_t2, acc_row_t3,
        acc_row_t4, acc_row_t5, acc_row_t6, acc_row_t7, acc_row_t8};
    int32_t i;
    for (i = 0; i < ROWS; i++)
        core_additem(menu, rows[i]);
}
