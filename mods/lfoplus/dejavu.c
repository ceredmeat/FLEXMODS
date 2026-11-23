/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Deja Vu, part of LFO+ since 0.6, for the Digitakt mk1 (OS 1.54), after the Octatrack mod: the LFO's RND wave plays a loop
 * of random values instead of new ones forever.
 *
 * SETTINGS gets two rows:
 *   RND LOOP  OFF, 2, 3, 4, 5, 6, 7, 8, 12, 16, 32: how many random values each LFO's loop holds.
 *             RND picks 16 values per LFO cycle, so 16 repeats once per cycle.
 *   RND LOCK  0-100%: how often a value in the loop is kept when its turn comes again. 100% repeats
 *             the loop exactly; lower lets it drift, one value at a time; 0% is as random as stock.
 *   RND TRIG  FREE or RESTART: with RESTART, a trig starts each LFO's loop again from its first
 *             value (in the LFO trig modes TRG, HLD, ONE, HLF), so the loop lines up with your notes.
 * Every LFO set to RND uses them (both LFOs of the audio tracks, and the MIDI tracks' LFO).
 * The settings are saved, two seconds after the last change, in one 512-byte eMMC block (SAVE_BLOCK,
 * in the gap ledcolors uses, a different block), and read back at power-on.
 *
 * How it works (OS 1.54, found by reading its code):
 *   - the LFO engine (0x400ed766 for the audio tracks, 0x400ed438 for the MIDI tracks) runs in the
 *     sound render. With WAVE = RND it asks one tiny routine, 0x400ed3fc, for a new random value
 *     each time the LFO moves on by 1/16 of its cycle (and on a trig in the trig modes). The routine
 *     is a two-word Fibonacci generator, state at 0x4020df1c and 0x4020df20.
 *   - its first instruction now jumps to dv_rand (dejavu.s), which hands us the LFO's own state
 *     pointer (a2 in both engines: one per LFO) so each LFO keeps its own loop, and whether this
 *     call comes from a trig (the audio engine keeps that at 52(sp), the MIDI engine in d5).
 *   - with RND LOOP off we do exactly what the stock routine does. */
#include <stdint.h>
#include "digitakt-mk1/core3.h"

typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);   /* 0 on success */
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
volatile int32_t dv_save_wait;          /* ticks until the pending save; 0 = nothing pending (ui.c sets it too) */

#define SEED_A   ((volatile uint32_t *)SEED_A_AT)
#define SEED_B   ((volatile uint32_t *)SEED_B_AT)

#define SLOTS    40                     /* LFOs we keep loops for: 16 audio + 8 MIDI, and room */
#define MAXLEN   32

static const uint8_t loop_lens[] = {0, 2, 3, 4, 5, 6, 7, 8, 12, 16, 32};
#define LENS     ((int32_t)sizeof loop_lens)

static volatile int32_t len_i;          /* index into loop_lens: 0 is OFF */
static volatile int32_t lock_pct = 100;
static volatile int32_t restart;        /* RND TRIG: 0 FREE, 1 RESTART */

struct loop {
    uint32_t key;                       /* the LFO's state pointer (a2); 0 free */
    uint8_t pos, filled;                /* the step we're on; how many values hold a value yet */
    int32_t value[MAXLEN];
};
static struct loop loops[SLOTS];
static uint32_t chance = 0x9e3779b9u;  /* our own dice, so the stock generator runs as before */

static uint32_t stock_rand(void)        /* exactly what 0x400ed3fc does */
{
    uint32_t b = *SEED_B;
    uint32_t a = b + *SEED_A;
    *SEED_A = b;
    *SEED_B = a;
    return a;
}

static uint32_t dice(void)              /* xorshift32 */
{
    uint32_t x = chance;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    chance = x;
    return x;
}

static struct loop *loop_for(uint32_t key)
{
    int32_t i, free_i = -1;
    for (i = 0; i < SLOTS; i++) {
        if (loops[i].key == key)
            return &loops[i];
        if (!loops[i].key && free_i < 0)
            free_i = i;
    }
    if (free_i < 0)
        return 0;
    loops[free_i].key = key;
    loops[free_i].pos = 0;
    loops[free_i].filled = 0;
    return &loops[free_i];
}

/* ---- 0.5: RND LOCK and RND LOOP as LFO destinations (audio tracks) -------------------------------- *
 * The DEST list is made by walking the sound's 53 slots: a slot whose parameter id has the right flags
 * in its descriptor (0x401aa09c + 52 id, +36) is a destination. Slots 46 and 47 have no parameter in
 * the stock firmware, and ids 11 and 12 are unused descriptors ("Error"). mod.json turns those two
 * into "RND:Lock" on slot 46 and "RND:Loop" on slot 47; the firmware's own start-up then lists them.
 * The LFO engine keeps, per LFO, the slot it is pointed at (state - 12) and what it adds to it this
 * moment (state - 8, 8.8: about -128.0 to +128.0). We read those from the track's two LFOs: nothing
 * else of slots 46 and 47 is used. Audio LFO states: 0x421f4e34 + 40 n, n = 2 x track + (0 LFO1, 1 LFO2). */
#define LFO_STATE0  0x421f4e34u
#define SLOT_LOCK   46
#define SLOT_LOOP   47
const char dv_n_lock[] = "RND Lock", dv_n_loop[] = "RND Loop", dv_n_grp[] = "RND",
           dv_s_lock[] = "LOCK", dv_s_loop[] = "LOOP";

static int32_t mod_for(uint32_t lfo, int32_t slot)
{
    uint32_t n = (lfo - LFO_STATE0) / 40u, first;
    int32_t sum = 0, k;
    if (lfo < LFO_STATE0 || n >= 16 || (lfo - LFO_STATE0) % 40u)
        return 0;                       /* not an audio track's LFO */
    first = LFO_STATE0 + (n & ~1u) * 40u;
    for (k = 0; k < 2; k++) {
        const volatile int32_t *st = (const volatile int32_t *)(first + 40u * (uint32_t)k);
        if (st[-3] == slot)
            sum += st[-2];
    }
    return sum;
}

/* the sound render, at interrupt level: called by dv_rand in place of 0x400ed3fc. Keep it short. */
int32_t dv_pick(uint32_t lfo, int32_t trig, int32_t phase)
{
    int32_t pct = lock_pct;
    int32_t li = len_i + mod_for(lfo, SLOT_LOOP) * (LENS - 1) / 0x7f00;
    int32_t len = loop_lens[li < 0 ? 0 : li >= LENS ? LENS - 1 : li];
    struct loop *l;
    uint32_t pos;
    if (!len || !(l = loop_for(lfo)))
        return (int32_t)stock_rand();
    if (trig && restart)
        pos = 0;                        /* a trig: back to the loop's first value */
    else
        pos = (uint32_t)(l->pos + 1) % (uint32_t)len;
    l->pos = (uint8_t)pos;
    if (pos >= l->filled) {             /* a step the loop hasn't reached yet: a fresh value */
        l->value[pos] = (int32_t)stock_rand();
        l->filled = (uint8_t)(pos + 1);
    } else {
        /* 0.4, RND LOCK = PHAS: the LFO's own PHAS knob is its lock, 0 = 0 % to 127 = 100 %. The engine
         * ignores the start phase of RND (it restarts RND at 0), so the knob is free for this, and being
         * a sound parameter it is per track and per LFO, can be parameter-locked, and can be the other
         * LFO's destination. */
        if (pct > 100) {
            if (phase < 0) phase = 0;
            pct = ((phase >> 8) * 100 + 63) / 127;
        }
        pct += mod_for(lfo, SLOT_LOCK) * 100 / 0x7f00;
        if ((int32_t)(dice() % 100u) < pct)
            return l->value[pos];
        l->value[pos] = (int32_t)stock_rand();   /* not kept this time: the loop drifts */
    }
    return l->value[pos];
}

/* ---- SETTINGS rows (the same shape as ledcolors' rows) ------------------------------------------- */
#define XOFF      -4
#define YOFF      0
#define TEXT_ADV  4
#define BOX_DOWN  1
#define BOX_UP    6
#define TOP_LIMIT 52

enum { ROW_LOOP, ROW_LOCK, ROW_TRIG };

static void redraw(void **payload)
{
    fw_invalidate((char *)*payload + 0x38);
}

static void step_row(int32_t row, int32_t delta)
{
    int32_t d;
    if (!delta)
        return;
    d = delta > 0 ? 1 : -1;
    if (row == ROW_LOOP) {
        int32_t i = len_i + d;
        if (i < 0) i = 0;
        if (i >= LENS) i = LENS - 1;
        len_i = i;
    } else if (row == ROW_LOCK) {
        int32_t v = lock_pct + 10 * d;
        if (v < 0) v = 0;
        if (v > 110) v = 110;                   /* 110: PHAS, the step after 100 % */
        lock_pct = v;
    } else {
        restart = !restart;
    }
    dv_save_wait = 60;                     /* saved two seconds after the last change */
}

static void put_num(char *p, int32_t v, int32_t pct)
{
    if (v >= 100) *p++ = (char)('0' + v / 100);
    if (v >= 10) *p++ = (char)('0' + (v / 10) % 10);
    *p++ = (char)('0' + v % 10);
    if (pct) *p++ = '%';
    *p = 0;
}

static void draw_row(int32_t row, void *bmp, int32_t x, int32_t y)
{
    char num[6];
    const char *text = num;
    int32_t n = 0, tx;
    if (row == ROW_LOOP) {
        if (len_i == 0)
            text = "OFF";
        else
            put_num(num, loop_lens[len_i], 0);
    } else if (row == ROW_LOCK) {
        if (lock_pct > 100)
            text = "PHAS";
        else
            put_num(num, lock_pct, 1);
    } else {
        text = restart ? "RESTART" : "FREE";
    }
    while (text[n]) n++;
    if (y + YOFF + BOX_UP > TOP_LIMIT)  /* the top row: the left side is the menu's title there */
        return;
    tx = x + XOFF - TEXT_ADV * n;
    fw_fillrect(bmp, tx - 2, y + YOFF - BOX_DOWN, x + XOFF, y + YOFF + BOX_UP, 0);
    fw_textf(bmp, fw_font5, tx, y + YOFF, 16, "%s", text);
}

#define ROW_FNS(name, row)                                                                        \
    void dv_##name##_select(void **payload, void *item) { (void)item; step_row(row, 1); redraw(payload); } \
    void dv_##name##_change(void **payload, void *item, int32_t delta) { (void)item; step_row(row, delta); redraw(payload); } \
    void dv_##name##_draw(void *payload, void *item, void *bmp, int32_t x, int32_t y) { (void)payload; (void)item; draw_row(row, bmp, x, y); }

ROW_FNS(loop, ROW_LOOP)
ROW_FNS(lock, ROW_LOCK)
ROW_FNS(trig, ROW_TRIG)

extern const uint32_t dv_row_loop[], dv_row_lock[], dv_row_trig[];
extern void core_additem(void *menu, const void *row);

extern volatile int32_t modset_closed;   /* MOD SETTINGS: weak, 0 without that mod */

void dv_settings(void *menu)
{
    if (modset_closed)                  /* inside a closed MOD SETTINGS section */
        return;
    core_additem(menu, dv_row_loop);
    core_additem(menu, dv_row_lock);
    core_additem(menu, dv_row_trig);
}

/* ---- saving (the same way ledcolors saves) --------------------------------------------------------- *
 * Version 1 (0.2-0.5): the three SETTINGS rows. Version 2 (0.6): the same, then the LFO (3/3) page's
 * settings for the eight tracks (ui.c, follow.c), so they come back at power-on too. A version 1 block
 * still loads: the page then starts at its defaults. */
#define SAVE_MAGIC 0x444a5655u          /* "DJVU" */
struct dv3_cfg { uint8_t trg[2], fol[2], atk, rel, vel, gain; };
extern volatile struct dv3_cfg dv3_cfg[8];
extern volatile uint8_t dv3_any_trg;
volatile int32_t dv3_cfg_loaded;        /* the page's settings came from the save: ui.c keeps them */
struct save1 { uint32_t magic; uint8_t version, len_i, lock_pct, restart; uint32_t sum; };
struct save2 { uint32_t magic; uint8_t version, len_i, lock_pct, restart; struct dv3_cfg cfg[8]; uint32_t sum; };
static uint8_t block[512] __attribute__((aligned(16)));
static int32_t loaded, ticks;

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
    struct save1 *s1 = (struct save1 *)block;
    struct save2 *s2 = (struct save2 *)block;
    int32_t i, t, any = 0;
    for (i = 0; i < 512; i++) block[i] = 0;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0 || s1->magic != SAVE_MAGIC)
        return;
    if (s1->version == 1) {
        if (s1->sum != checksum(block, (int32_t)((uint8_t *)&s1->sum - block)))
            return;
    } else if (s1->version == 2) {
        if (s2->sum != checksum(block, (int32_t)((uint8_t *)&s2->sum - block)))
            return;
        for (t = 0; t < 8; t++) {       /* each value checked, as the page's knobs allow */
            const struct dv3_cfg *c = &s2->cfg[t];
            dv3_cfg[t].trg[0] = c->trg[0] <= 8 ? c->trg[0] : 0;
            dv3_cfg[t].trg[1] = c->trg[1] <= 8 ? c->trg[1] : 0;
            dv3_cfg[t].fol[0] = c->fol[0] & 7;
            dv3_cfg[t].fol[1] = c->fol[1] & 7;
            dv3_cfg[t].atk = c->atk <= 127 ? c->atk : 8;
            dv3_cfg[t].rel = c->rel <= 127 ? c->rel : 60;
            dv3_cfg[t].vel = c->vel ? 1 : 0;
            dv3_cfg[t].gain = c->gain <= 127 ? c->gain : 32;
            any |= dv3_cfg[t].trg[0] | dv3_cfg[t].trg[1];
        }
        dv3_any_trg = (uint8_t)(any != 0);
        dv3_cfg_loaded = 1;
    } else {
        return;                         /* nothing saved yet: the defaults stay */
    }
    if (s1->len_i < LENS) len_i = s1->len_i;
    if (s1->lock_pct <= 110) lock_pct = s1->lock_pct;
    restart = s1->restart ? 1 : 0;
}

static void save(void)
{
    struct save2 *s = (struct save2 *)block;
    int32_t i, t;
    for (i = 0; i < 512; i++) block[i] = 0;
    s->magic = SAVE_MAGIC;
    s->version = 2;
    s->len_i = (uint8_t)len_i;
    s->lock_pct = (uint8_t)lock_pct;
    s->restart = (uint8_t)restart;
    for (t = 0; t < 8; t++) {
        s->cfg[t].trg[0] = dv3_cfg[t].trg[0];
        s->cfg[t].trg[1] = dv3_cfg[t].trg[1];
        s->cfg[t].fol[0] = dv3_cfg[t].fol[0];
        s->cfg[t].fol[1] = dv3_cfg[t].fol[1];
        s->cfg[t].atk = dv3_cfg[t].atk;
        s->cfg[t].rel = dv3_cfg[t].rel;
        s->cfg[t].vel = dv3_cfg[t].vel;
        s->cfg[t].gain = dv3_cfg[t].gain;
    }
    s->sum = checksum(block, (int32_t)((uint8_t *)&s->sum - block));
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}

void dv_tick(void *ctrl)
{
    (void)ctrl;
    if (!loaded && ++ticks > 30) {      /* a second after start-up, as ledcolors waits */
        loaded = 1;
        load();
    }
    if (dv_save_wait > 0 && --dv_save_wait == 0)
        save();
}
