/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Velocity Humanise for the Digitakt mk1 (OS 1.54), after the Analog Rytm mod of the same name:
 * every note trig plays a little louder or softer, at random, by up to an amount you set per track.
 *
 * SETTINGS rows HUMAN T1 .. HUMAN T8 (the audio tracks) and HUMAN MIDI (all MIDI tracks):
 * OFF, 2, 4, 6, 8, 12, 16, 24, 32: how far the velocity may move up or down (of 0-127).
 * Saved two seconds after the last change in one eMMC block (SAVE_BLOCK), read back at power-on.
 *
 * How it works: seq-events' ev_fire_vel, the velocity a note trig is about to play with. Nothing in
 * the pattern changes: take the mod out and every trig plays as you programmed it. */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"

typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);   /* 0 on success */
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)

#define ROWS 9                                  /* T1-T8, then MIDI */
static const uint8_t amounts[] = {0, 2, 4, 6, 8, 12, 16, 24, 32};
#define AMOUNTS ((int32_t)sizeof amounts)

static volatile uint8_t amt_i[ROWS];            /* index into amounts, per row */
static uint32_t dice = 0x2545f491u;
static int32_t save_wait, loaded, ticks;

/* ---- the sequencer's fire, at interrupt level ------------------------------------------------------ */
void hum_vel(int32_t track, int32_t step, int32_t *vel)
{
    int32_t row = track < SQ_AUDIO ? track : ROWS - 1;
    int32_t a = amounts[amt_i[row]];
    uint32_t x;
    (void)step;
    if (!a)
        return;
    x = dice;                                   /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    dice = x;
    *vel += (int32_t)(x % (uint32_t)(2 * a + 1)) - a;   /* -a .. +a; seq-events keeps it in 0-127 */
}

/* ---- saving ---------------------------------------------------------------------------------------- */
#define SAVE_MAGIC 0x48554d4eu                  /* "HUMN" */
struct save { uint32_t magic; uint8_t version, amt[ROWS]; uint8_t pad[2]; uint32_t sum; };
static uint8_t block[512] __attribute__((aligned(16)));

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
    struct save *s = (struct save *)block;
    int32_t i;
    for (i = 0; i < 512; i++) block[i] = 0;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0)
        return;
    if (s->magic != SAVE_MAGIC || s->version != 1
        || s->sum != checksum(block, (int32_t)((uint8_t *)&s->sum - block)))
        return;
    for (i = 0; i < ROWS; i++)
        amt_i[i] = s->amt[i] < AMOUNTS ? s->amt[i] : 0;
}

static void save(void)
{
    struct save *s = (struct save *)block;
    int32_t i;
    for (i = 0; i < 512; i++) block[i] = 0;
    s->magic = SAVE_MAGIC;
    s->version = 1;
    for (i = 0; i < ROWS; i++)
        s->amt[i] = amt_i[i];
    s->sum = checksum(block, (int32_t)((uint8_t *)&s->sum - block));
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}

void hum_tick(void *ctrl)
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
    i = amt_i[row] + (delta > 0 ? 1 : -1);
    if (i < 0) i = 0;
    if (i >= AMOUNTS) i = AMOUNTS - 1;
    amt_i[row] = (uint8_t)i;
    save_wait = 60;
}

static void draw_row(int32_t row, void *bmp, int32_t x, int32_t y)
{
    char num[4];
    const char *text = num;
    int32_t v = amounts[amt_i[row]], n = 0, tx;
    if (!v) {
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
    void hum_##name##_select(void **payload, void *item) { (void)item; step_row(row, 1); redraw(payload); } \
    void hum_##name##_change(void **payload, void *item, int32_t delta) { (void)item; step_row(row, delta); redraw(payload); } \
    void hum_##name##_draw(void *payload, void *item, void *bmp, int32_t x, int32_t y) { (void)payload; (void)item; draw_row(row, bmp, x, y); }

ROW_FNS(t1, 0) ROW_FNS(t2, 1) ROW_FNS(t3, 2) ROW_FNS(t4, 3)
ROW_FNS(t5, 4) ROW_FNS(t6, 5) ROW_FNS(t7, 6) ROW_FNS(t8, 7)
ROW_FNS(midi, 8)

extern const uint32_t hum_row_t1[], hum_row_t2[], hum_row_t3[], hum_row_t4[], hum_row_t5[],
    hum_row_t6[], hum_row_t7[], hum_row_t8[], hum_row_midi[];
extern void core_additem(void *menu, const void *row);

extern volatile int32_t modset_closed;   /* MOD SETTINGS: weak, 0 without that mod */

void hum_settings(void *menu)
{
    if (modset_closed)                  /* inside a closed MOD SETTINGS section */
        return;
    static const uint32_t *const rows[ROWS] = {hum_row_t1, hum_row_t2, hum_row_t3, hum_row_t4,
        hum_row_t5, hum_row_t6, hum_row_t7, hum_row_t8, hum_row_midi};
    int32_t i;
    for (i = 0; i < ROWS; i++)
        core_additem(menu, rows[i]);
}
