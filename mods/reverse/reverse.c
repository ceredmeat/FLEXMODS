/* SPDX-License-Identifier: GPL-2.0-or-later */
/* REVERSE for the Digitakt mk1 (core 4.1 + seq-events, OS 1.54): a chance, per track, that a trig plays its
 * sample backwards, on a master page (FUNC + LFO): knobs A-H are tracks 1-8, 0-100 %.
 *
 * A trig that comes up reversed gets a lock on PLAY (the SRC page's play mode, slot 18) flipped from the
 * sound's own: forward to reverse, forward loop to reverse loop, and the other way (seq-events,
 * ev_fire_locks). The pattern itself never changes. Saved. The page flashes a track's arrow each time it
 * plays one backwards. */
#include <stdint.h>
#include "../common/heapbuf.h"
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"
#include "../common/mpui.h"

#define SLOT_PLAY 18
static volatile int8_t chance[8];
static volatile uint8_t flash[8];
static uint32_t dice = 0x9e3779b9u;

#define KIT_WORD(t, k) (*(volatile int16_t *)(fw_kit + 0x20 + 0xa2 * (t) + 20 + 2 * (k)))

int32_t rv_fire_locks(int32_t track, int32_t step, struct sq_lock *out, int32_t room)
{
    (void)step;
    if (track < 0 || track >= 8 || room < 1 || !chance[track] || !fw_kit)
        return 0;
    uint32_t x = dice;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    dice = x;
    if ((int32_t)(x % 100u) >= chance[track])
        return 0;
    int32_t play = (KIT_WORD(track, SLOT_PLAY) >> 8) & 3;
    out[0].slot = SLOT_PLAY;
    /* 0.3: the play modes are 0 REV, 1 REV LOOP, 2 FWD LOOP, 3 FWD (measured on a unit), so the mirror of
     * each is 3 - it: FWD <-> REV, FWD LOOP <-> REV LOOP. (0.2 flipped the low bit, which made FWD into
     * FWD LOOP: a one-shot repeating, never reversed.) */
    /* 0.4: never exactly 0: REV (0) as 0x0001, the mode in the high byte and a fine part of 1, in case a lock of
     * 0 reads as no lock (0.3 sent 0x0000 for REV and nothing changed; 0.2's FWD LOOP, 0x0200, did play) */
    out[0].value = (int16_t)(((3 - play) << 8) | 1);
    flash[track] = 12;
    return 1;
}

/* ---- saving ------------------------------------------------------------------------------------------------ */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
#define MAGIC 0x52455653u                            /* "REVS" */
HEAP_BUF(block, 512)          /* borrowed from the heap while loading or saving */
static int32_t save_wait, loaded, ticks;

/* the settings go with the project (common/projsave.h, through PROJECT LINK): each slot's copy at
 * PJ_BASE + slot x PJ_NB */
#define PJ_BASE 0x62300
#define PJ_NB 1
#include "../common/projsave.h"
static void load(void)
{
    if (!block_ready()) return;
    uint32_t *w = (uint32_t *)block;
    for (int i = 0; i < 512; i++) block[i] = 0;
    if (EMMC_READ(SAVE_BLOCK, 512, block) != 0 || w[0] != MAGIC || w[1] != (w[2] ^ 0x5a5a5a5au))
        return;
    for (int t = 0; t < 8; t++) chance[t] = (int8_t)ui_clamp((int8_t)block[12 + t], 0, 100);
}
static void save(void)
{
    if (!block_ready()) return;
    uint32_t *w = (uint32_t *)block, sum = 0;
    for (int i = 0; i < 512; i++) block[i] = 0;
    for (int t = 0; t < 8; t++) { block[12 + t] = (uint8_t)chance[t]; sum = sum * 31u + (uint8_t)chance[t]; }
    w[0] = MAGIC; w[2] = sum; w[1] = sum ^ 0x5a5a5a5au;
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}
static void pj_defaults(void) { for (int32_t t = 0; t < 8; t++) chance[t] = 0; }

/* ---- the page ------------------------------------------------------------------------------------------ */
static int32_t turned[8], hl_k = -1, hl_ticks;

/* a little arrow pointing left (reversed), solid while it flashes */
static void arrow(void *bmp, int32_t x, int32_t y, int32_t solid)
{
    for (int32_t i = 0; i < 4; i++)
        fw_fillrect(bmp, x + i, y - i, x + i, y + i, 1);
    if (solid) fw_fillrect(bmp, x + 4, y - 1, x + 10, y + 1, 1);
    else for (int32_t c = x + 5; c <= x + 10; c += 2) ui_dot(bmp, c, y);
}

static void rv_draw(void *view, void *bmp)
{
    char s[8];
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    ui_text(bmp, 2, 46, "CHANCE A TRIG PLAYS BACKWARDS");
    for (int32_t t = 0; t < 8; t++) {                     /* each track: an arrow, lit as it reverses */
        int32_t x = t * 16 + 2;
        if (chance[t]) arrow(bmp, x, 35, flash[t] > 0);
        else ui_dot(bmp, x + 6, 35);
    }
    for (int32_t x = 0; x < 128; x += 2) ui_dot(bmp, x, 26);
    for (int32_t t = 0; t < 8; t++) {
        char nm[3] = { 'T', (char)('1' + t), 0 };
        if (chance[t]) ui_num(s, chance[t]); else { s[0] = 'O'; s[1] = 'F'; s[2] = 'F'; s[3] = 0; }
        ui_knob(bmp, t, nm, s, chance[t], 100, 0, t == hl_k && hl_ticks > 0);
    }
}

static int32_t rv_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > 8)
        return 0;
    int32_t k = knob - 1;
    hl_k = k; hl_ticks = 40;
    if ((turned[k] > 0) != (turn > 0)) turned[k] = 0;
    turned[k] += turn;
    while (turned[k] >= 4 || turned[k] <= -4) {            /* 5 % a click */
        int32_t dir = turned[k] > 0 ? 1 : -1;
        turned[k] -= dir * 4;
        chance[k] = (int8_t)ui_clamp(chance[k] + dir * 5, 0, 100);
    }
    save_wait = 60;
    fw_invalidate(view);
    return 1;
}

static void rv_clear(void *view)
{
    for (int32_t t = 0; t < 8; t++) { chance[t] = 0; turned[t] = 0; }
    save_wait = 60;
    fw_invalidate(view);
}

void rv_tick(void *ctrl)
{
    int any = 0;
    (void)ctrl;
    if (!loaded && ++ticks > 30) { loaded = 1; load(); }
    if (loaded) pj_tick();                           /* a project loaded or saved */
    if (save_wait > 0 && --save_wait == 0) save();
    for (int t = 0; t < 8; t++) if (flash[t]) { flash[t]--; any = 1; if (!flash[t]) any = 2; }
    if (hl_ticks > 0 && --hl_ticks == 0) any = 2;
    if (any && core_mview && core_mpage_shown(core_mview)) fw_invalidate(core_mview);
}

struct core_mpage rv_page = { 99, "REV", "Reverse", rv_draw, mp_key, rv_enc, 0, 0, rv_clear };
