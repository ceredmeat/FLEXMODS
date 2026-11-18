/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DENSITY for the Digitakt mk1 (core 4.1 + seq-events, OS 1.54): one knob for how busy the whole pattern
 * is, on a master page (FUNC + LFO). The middle plays it as it is; left, trigs drop out, more and more;
 * right, ghost notes fill the empty steps. The pattern itself never changes.
 *
 *   A  DNS   the density: left thinner, right fuller
 *   B  DWN   keep the downbeats (every 4th step) whatever DNS says
 *   C  GHV   the ghost notes' velocity, against the track's own
 *   D  NEW   a new choice each loop (ON), or the same steps every loop (OFF)
 *   E  TRK   which tracks: ALL, AUD (audio), MID (MIDI), or NO1 (all but track 1, the kick)
 *
 * The page shows the eight audio tracks' first 16 steps: a dot each stored trig, a box each one that
 * plays now, a small box each ghost note. CLEAR puts DNS back in the middle. */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"
#include "../common/mpui.h"

enum { K_DNS, K_DWN, K_GHV, K_NEW, K_TRK, KNOBS };
static const char *const kname[KNOBS] = { "DNS", "DWN", "GHV", "NEW", "TRK" };
static const int8_t kdef[KNOBS] = { 64, 1, 50, 1, 0 };
static const int8_t kmax[KNOBS] = { 127, 1, 127, 1, 3 };
static volatile int8_t kv[KNOBS] = { 64, 1, 50, 1, 0 };
static const char *const trk_n[4] = { "ALL", "AUD", "MID", "NO1" };

static uint32_t hash(int32_t track, int32_t step)
{
    uint32_t h = (uint32_t)track * 0x85ebca6bu ^ (uint32_t)step * 0xc2b2ae35u ^
                 (kv[K_NEW] ? sq_clock[track & 15].parity * 0x27d4eb2fu : 0x1234567u);
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    return h;
}
static int included(int32_t t)
{
    switch (kv[K_TRK]) {
    case 1: return t < SQ_AUDIO;
    case 2: return t >= SQ_AUDIO;
    case 3: return t != 0;
    default: return 1;
    }
}
/* what density does to a step: 0 nothing, 1 drops it, 2 adds a ghost */
static int verdict(int32_t t, int32_t step, int stored)
{
    int32_t d = kv[K_DNS] - 64;
    if (d == 0 || !included(t) || step < 0 || step >= SQ_STEPS)
        return 0;
    if (kv[K_DWN] && (step & 3) == 0 && stored)
        return 0;
    int32_t r = (int32_t)(hash(t, step) % 1000u);
    if (stored && d < 0 && r < -d * 1000 / 64)
        return 1;
    if (!stored && d > 0 && r < d * 600 / 63)                /* full right: 60 % of the empty steps */
        return 2;
    return 0;
}

static volatile uint32_t ghost[SQ_TRACKS][2];             /* bit s: step s is a ghost this loop */

void dn_gen_trig(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    if (track < 0 || track >= SQ_TRACKS || step < 0 || step >= SQ_STEPS)
        return;
    int v = verdict(track, step, (*trig & SQ_TRIG) != 0);
    if (v == 1) *trig &= ~SQ_TRIG;
    if (v == 2) *trig |= SQ_TRIG;
    if (first) {
        volatile uint32_t *g = &ghost[track][step >> 5];
        uint32_t bit = 1u << (step & 31);
        *g = v == 2 ? *g | bit : *g & ~bit;
    }
}

void dn_fire_vel(int32_t track, int32_t step, int32_t *vel)
{
    if (track < 0 || track >= SQ_TRACKS || step < 0 || step >= SQ_STEPS)
        return;
    if ((ghost[track][step >> 5] >> (step & 31)) & 1u)
        *vel = *vel * kv[K_GHV] / 127;
}

/* ---- the page ------------------------------------------------------------------------------------------ */
static int32_t turned[KNOBS], hl_k = -1, hl_ticks;

static void val(char *b, int32_t k)
{
    int32_t v = kv[k];
    if (k == K_DNS) { if (v == 64) { b[0] = 'O'; b[1] = 'F'; b[2] = 'F'; b[3] = 0; } else { if (v > 64) *b++ = '+'; ui_num(b, (v - 64) * 100 / 64); } }
    else if (k == K_DWN || k == K_NEW) { const char *s = v ? "ON" : "OFF"; while ((*b++ = *s++)) ; }
    else if (k == K_TRK) { const char *s = trk_n[v]; while ((*b++ = *s++)) ; }
    else ui_num(b, v);
}

static void dn_draw(void *view, void *bmp)
{
    char s[24];
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    /* the eight audio tracks' first 16 steps, a row each: a bar each trig that plays, a dot each one
     * dropped, a short bar each ghost */
    for (int32_t t = 0; t < 8; t++) {
        uint32_t lo = (uint32_t)sq_stored(t);
        int32_t y = 50 - t * 3;
        for (int32_t k = 0; k < 16; k++) {
            int32_t x = 1 + k * 8, stored = (lo >> k) & 1u, v = verdict(t, k, stored);
            if (stored && v != 1) fw_fillrect(bmp, x, y - 1, x + 5, y, 1);
            else if (stored) ui_dot(bmp, x + 2, y);
            else if (v == 2) fw_fillrect(bmp, x + 2, y - 1, x + 3, y, 1);
        }
    }
    for (int32_t k = 0; k < KNOBS; k++) {
        val(s, k);
        ui_knob(bmp, k, kname[k], s, kv[k], kmax[k], k == K_DNS ? 1 : (k == K_GHV ? 0 : 2), k == hl_k && hl_ticks > 0);
    }
}

static int32_t dn_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > KNOBS)
        return 0;
    int32_t k = knob - 1, per = k == K_DNS || k == K_GHV ? 4 : 16;
    hl_k = k; hl_ticks = 40;
    if ((turned[k] > 0) != (turn > 0)) turned[k] = 0;
    turned[k] += turn;
    while (turned[k] >= per || turned[k] <= -per) {
        int32_t dir = turned[k] > 0 ? 1 : -1;
        turned[k] -= dir * per;
        kv[k] = (int8_t)ui_clamp(kv[k] + dir, 0, kmax[k]);
    }
    sq_rearm(0xffffu);
    fw_invalidate(view);
    return 1;
}

static void dn_clear(void *view)
{
    for (int32_t k = 0; k < KNOBS; k++) { kv[k] = kdef[k]; turned[k] = 0; }
    sq_rearm(0xffffu);
    fw_invalidate(view);
}

void dn_tick(void *ctrl)
{
    static uint32_t seen;
    (void)ctrl;
    if (hl_ticks > 0 && --hl_ticks == 0 && core_mview) fw_invalidate(core_mview);
    if (core_mview && core_mview_draws != seen) {
        seen = core_mview_draws;
        if (core_mpage_shown(core_mview) && kv[K_DNS] != 64 && kv[K_NEW])
            fw_invalidate(core_mview);                  /* new choices each loop: keep it current */
    }
}

struct core_mpage dn_page = { 99, "DNS", "Density", dn_draw, mp_key, dn_enc, 0, 0, dn_clear };
