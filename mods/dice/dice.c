/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DICE + AUTOPILOT for the Digitakt mk1 (core 4.4 + seq-events, OS 1.54): your pattern, changed live while it
 * plays and never written over. One master page, DCE (FUNC + LFO, then LEFT / RIGHT):
 *
 *   A TRK   the track (1-8 audio, 9-16 MIDI); the grid shows its eight
 *   B MODE  OFF, DICE (a random rhythm, rolled with YES) or AUTO (the autopilot: every few bars it flips a step
 *           or two, so the pattern slowly evolves)
 *   C HITS  how many hits a roll makes (DICE), or how busy the autopilot lets it get (AUTO)
 *   D EVRY  AUTO: a change every 1, 2, 4 or 8 loops of the track
 *   E MOVE  AUTO: steps changed each time, 1-4
 *   F HOME  AUTO: how strongly it drifts back toward what you programmed, 0-100%
 *   G ADD   DICE: REPL (only the dice plays) or ADD (the dice on top of your pattern)
 *   YES     roll the dice for the track (DICE), or start the autopilot afresh
 *   NO      the track back to exactly what you programmed (the mode stays)
 *
 * It sees the stored trigs as each step comes up (ev_gen_trig, after other generators) and sets or clears the
 * step's note trig: the pattern in memory is untouched. Rolls and settings are saved. */
#include <stdint.h>
#include "../common/heapbuf.h"
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"
#include "../common/mpui.h"

#define K_YES 12
#define K_NO 13
#define F_DOWN 0x01
#define F_REPEAT 0x08
#define F_RELEASE 0x10
#define NT 16
enum { M_OFF, M_DICE, M_AUTO };

static uint32_t rnd_s = 0x5eed1234u;
static uint32_t rnd(uint32_t n) { rnd_s ^= rnd_s << 13; rnd_s ^= rnd_s >> 17; rnd_s ^= rnd_s << 5; return n ? (rnd_s >> 9) % n : 0; }
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }

/* ---- per track ------------------------------------------------------------------------------------------- */
static int32_t mode[NT], hits[NT], every[NT], move[NT], home[NT], add[NT];
static uint32_t dicem[NT][2];                    /* the rolled rhythm */
static volatile uint32_t orig[NT][2];            /* what you programmed, as last seen */
static uint32_t delta[NT][2];                    /* the autopilot's changes to it */
static volatile int32_t cur[NT] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static volatile uint32_t loops[NT], len_seen[NT];
static uint32_t loops_done[NT];
static int32_t bit(const volatile uint32_t *m, int32_t s) { return (m[s >> 5] >> (s & 31)) & 1; }
static int32_t bitc(const uint32_t *m, int32_t s) { return (m[s >> 5] >> (s & 31)) & 1; }
static void setb(uint32_t *m, int32_t s, int32_t on) { if (on) m[s >> 5] |= 1u << (s & 31); else m[s >> 5] &= ~(1u << (s & 31)); }

static int32_t plays(int32_t t, int32_t s, int32_t stored)
{
    if (mode[t] == M_DICE) return bitc(dicem[t], s) || (add[t] && stored);
    if (mode[t] == M_AUTO) return stored ^ bitc(delta[t], s);
    return stored;
}

/* ---- the step (interrupt level: a few stores) -------------------------------------------------------------- */
void dc_gen_trig(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    (void)first;
    if (track < 0 || track >= NT || step < 0 || step > 63 || !trig) return;
    int32_t stored = (*trig & SQ_TRIG) != 0;
    uint32_t b = 1u << (step & 31);
    if (stored) orig[track][step >> 5] |= b; else orig[track][step >> 5] &= ~b;
    if (step < cur[track]) loops[track]++;       /* it went round */
    if (step + 1 > (int32_t)len_seen[track]) len_seen[track] = (uint32_t)(step + 1);
    cur[track] = step;
    if (mode[track] == M_OFF) return;
    if (plays(track, step, stored)) *trig |= SQ_TRIG; else *trig &= ~SQ_TRIG;
}

/* ---- rolling and drifting (the UI task) ------------------------------------------------------------------------ */
static int32_t tlen(int32_t t) { struct sq_track i; return sq_track_info(t, &i, sizeof i) == 0 && i.len > 0 ? (i.len > 64 ? 64 : i.len) : 16; }
static int32_t weight(int32_t s) { return (s & 3) == 0 ? 8 : (s & 1) == 0 ? 4 : 2; }   /* downbeats first */
static void roll(int32_t t)
{
    int32_t n = tlen(t), want = hits[t] * n / 16;
    if (want < 1) want = 1;
    dicem[t][0] = dicem[t][1] = 0;
    for (int32_t got = 0, tries = 0; got < want && tries < 400; tries++) {
        int32_t s = (int32_t)rnd((uint32_t)n);
        if (bitc(dicem[t], s) || rnd(8) >= (uint32_t)weight(s)) continue;
        setb(dicem[t], s, 1); got++;
    }
}
static int32_t count_playing(int32_t t, int32_t n)
{
    int32_t c = 0;
    for (int32_t s = 0; s < n; s++) c += plays(t, s, bit(orig[t], s));
    return c;
}
static void drift(int32_t t)
{
    int32_t n = tlen(t), busy = hits[t] * n / 16;
    for (int32_t k = 0; k < move[t]; k++) {
        int32_t changed = 0;
        for (int32_t s = 0; s < n; s++) changed += bitc(delta[t], s);
        if (changed && (int32_t)rnd(100) < home[t]) {          /* back toward what you programmed */
            int32_t pick = (int32_t)rnd((uint32_t)changed);
            for (int32_t s = 0; s < n; s++) if (bitc(delta[t], s) && pick-- == 0) { setb(delta[t], s, 0); break; }
            continue;
        }
        int32_t c = count_playing(t, n), s = (int32_t)rnd((uint32_t)n);
        int32_t on = plays(t, s, bit(orig[t], s));
        if (!on && c >= busy) {                   /* busy enough: take one away instead */
            for (int32_t tries = 0; tries < 32 && !on; tries++) { s = (int32_t)rnd((uint32_t)n); on = plays(t, s, bit(orig[t], s)); }
        }
        if (on && s == 0 && rnd(4)) continue;     /* keep the one, mostly */
        setb(delta[t], s, !bitc(delta[t], s));
    }
}

/* ---- saving --------------------------------------------------------------------------------------------- */
typedef int32_t (*emmc_fn)(int32_t block, int32_t bytes, void *buf);
#define EMMC_READ  ((emmc_fn)EMMC_READ_AT)
#define EMMC_WRITE ((emmc_fn)EMMC_WRITE_AT)
HEAP_BUF(block, 512)
#define MAGIC 0x44494345u                         /* "DICE" */
static int32_t loaded, save_wait;
static uint32_t sum_of(const uint8_t *b) { uint32_t s = 7; for (int32_t i = 8; i < 8 + NT * 14; i++) s = s * 31u + b[i]; return s; }
/* the settings go with the project (common/projsave.h, through PROJECT LINK): each slot's copy at
 * PJ_BASE + slot x PJ_NB */
#define PJ_BASE 0x62380
#define PJ_NB 1
#include "../common/projsave.h"
static void load(void)
{
    if (!block_ready() || EMMC_READ(SAVE_BLOCK, 512, block) != 0) return;
    uint32_t *w = (uint32_t *)block;
    if (w[0] != MAGIC || w[1] != sum_of(block)) return;
    for (int32_t t = 0; t < NT; t++) {
        uint8_t *p = block + 8 + t * 14;
        mode[t] = p[0] % 3; hits[t] = ui_clamp(p[1], 1, 16); every[t] = p[2] & 3; move[t] = ui_clamp(p[3], 1, 4);
        home[t] = ui_clamp(p[4], 0, 100); add[t] = p[5] & 1;
        dicem[t][0] = (uint32_t)p[6] | (uint32_t)p[7] << 8 | (uint32_t)p[8] << 16 | (uint32_t)p[9] << 24;
        dicem[t][1] = (uint32_t)p[10] | (uint32_t)p[11] << 8 | (uint32_t)p[12] << 16 | (uint32_t)p[13] << 24;
    }
}
static void save(void)
{
    if (!block_ready()) return;
    for (int32_t i = 0; i < 512; i++) block[i] = 0;
    uint32_t *w = (uint32_t *)block;
    for (int32_t t = 0; t < NT; t++) {
        uint8_t *p = block + 8 + t * 14;
        p[0] = (uint8_t)mode[t]; p[1] = (uint8_t)hits[t]; p[2] = (uint8_t)every[t]; p[3] = (uint8_t)move[t]; p[4] = (uint8_t)home[t]; p[5] = (uint8_t)add[t];
        for (int32_t k = 0; k < 4; k++) { p[6 + k] = (uint8_t)(dicem[t][0] >> (k * 8)); p[10 + k] = (uint8_t)(dicem[t][1] >> (k * 8)); }
    }
    w[0] = MAGIC; w[1] = sum_of(block);
    EMMC_WRITE(SAVE_BLOCK, 512, block);
}
static void pj_defaults(void) { for (int32_t t = 0; t < NT; t++) { mode[t] = M_OFF; delta[t][0] = delta[t][1] = 0; } }

/* ---- the page ---------------------------------------------------------------------------------------------- */
static int32_t sel, turned[7], hl = -1, hl_ticks, roll_anim, roll_face;
static void die(void *bmp, int32_t x, int32_t y, int32_t face)    /* a die, 21 x 21 */
{
    static const uint16_t pips[6] = { 0x010, 0x101, 0x111, 0x145, 0x155, 0x16d };   /* a 3 x 3 grid of spots */
    fw_fillrect(bmp, x, y, x + 20, y + 20, 0); fw_framerect(bmp, x, y, x + 20, y + 20, 1);
    fw_fillrect(bmp, x, y, x, y, 0); fw_fillrect(bmp, x + 20, y, x + 20, y, 0); fw_fillrect(bmp, x, y + 20, x, y + 20, 0); fw_fillrect(bmp, x + 20, y + 20, x + 20, y + 20, 0);
    for (int32_t i = 0; i < 9; i++) if ((pips[face % 6] >> i) & 1) { int32_t px = x + 4 + (i % 3) * 5, py = y + 14 - (i / 3) * 5; fw_fillrect(bmp, px, py, px + 2, py + 2, 1); }
}
static void dc_draw(void *view, void *bmp)
{
    char s[24], *p;
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    int32_t bank = sel / 8 * 8;
    for (int32_t r = 0; r < 8; r++) {             /* the eight tracks of the bank: what plays now */
        int32_t t = bank + r, n = tlen(t), y = 49 - r * 3, shown = n < 16 ? n : 16, page = cur[t] >= 0 ? (cur[t] % n) / 16 : 0;
        if (t == sel) fw_fillrect(bmp, 0, y, 2, y + 1, 1);
        if (mode[t] != M_OFF) fw_fillrect(bmp, 4, y, 4, y + 1, 1);
        for (int32_t s = 0; s < shown; s++) {     /* the 16 steps of the page that's playing */
            int32_t st = page * 16 + s, x = 8 + s * 5;
            if (st >= n) break;
            if (plays(t, st, bit(orig[t], st))) fw_fillrect(bmp, x, y, x + 3, y + 1, 1);
            else if ((s & 3) == 0) fw_fillrect(bmp, x + 1, y, x + 1, y, 1);
            if (st == cur[t] % n) fw_fillrect(bmp, x, y, x + 3, y + 1, -1), fw_framerect(bmp, x - 1, y - 1, x + 4, y + 2, 1);   /* the playhead */
        }
    }
    static const char *const mn[3] = { "OFF", "DCE", "AUT" }, *const ev[4] = { "1", "2", "4", "8" };
    char tn[4], hn[4], mv[2] = { (char)('0' + move[sel]), 0 }, hm[5];
    p = put(tn, sel < 8 ? "T" : "M"); ui_num(p, (sel & 7) + 1);
    ui_num(hn, hits[sel]); p = ui_num(hm, home[sel]); put(p, "%");
    if (roll_anim > 0) die(bmp, 98, 26, roll_face);
    p = put(s, ""); (void)p;
    ui_knob(bmp, 0, "TRK", tn, sel, NT - 1, 2, hl == 0 && hl_ticks > 0);
    ui_knob(bmp, 1, "MOD", mn[mode[sel]], mode[sel], 2, 2, hl == 1 && hl_ticks > 0);
    ui_knob(bmp, 2, "HIT", hn, hits[sel], 16, 0, hl == 2 && hl_ticks > 0);
    ui_knob(bmp, 3, "EVR", ev[every[sel]], every[sel], 3, 2, hl == 3 && hl_ticks > 0);
    ui_knob(bmp, 4, "MOV", mv, move[sel], 4, 0, hl == 4 && hl_ticks > 0);
    ui_knob(bmp, 5, "HOM", hm, home[sel], 100, 0, hl == 5 && hl_ticks > 0);
    ui_knob(bmp, 6, "ADD", add[sel] ? "ADD" : "RPL", add[sel], 1, 2, hl == 6 && hl_ticks > 0);
}
static int32_t dc_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > 7) return 0;
    int32_t k = knob - 1;
    hl = k; hl_ticks = 60;
    if ((turned[k] > 0) != (turn > 0)) turned[k] = 0;
    turned[k] += turn;
    while (turned[k] >= 16 || turned[k] <= -16) {
        int32_t d = turned[k] > 0 ? 1 : -1;
        turned[k] -= d * 16;
        switch (k) {
        case 0: sel = ui_clamp(sel + d, 0, NT - 1); break;
        case 1: mode[sel] = ui_clamp(mode[sel] + d, 0, 2); if (mode[sel] == M_DICE && !dicem[sel][0] && !dicem[sel][1]) roll(sel); break;
        case 2: hits[sel] = ui_clamp(hits[sel] + d, 1, 16); break;
        case 3: every[sel] = ui_clamp(every[sel] + d, 0, 3); break;
        case 4: move[sel] = ui_clamp(move[sel] + d, 1, 4); break;
        case 5: home[sel] = ui_clamp(home[sel] + d * 5, 0, 100); break;
        case 6: add[sel] = ui_clamp(add[sel] + d, 0, 1); break;
        }
    }
    save_wait = 60;
    fw_invalidate(view);
    return 1;
}
static int32_t dc_key(void *view, void *event)
{
    int32_t key = *(int32_t *)((char *)event + 12), fl = *(int32_t *)((char *)event + 16);
    int32_t press = (fl & F_DOWN) && !(fl & (F_RELEASE | F_REPEAT));
    if (key == K_YES) {
        if (press) {
            if (mode[sel] == M_OFF) mode[sel] = M_DICE;
            if (mode[sel] == M_DICE) { roll(sel); roll_anim = 24; } else { delta[sel][0] = delta[sel][1] = 0; drift(sel); }
            save_wait = 60; fw_invalidate(view);
        }
        return 1;
    }
    if (key == K_NO) {
        if (press) { delta[sel][0] = delta[sel][1] = 0; if (mode[sel] == M_DICE) mode[sel] = M_OFF; save_wait = 60; fw_invalidate(view); }
        return 1;
    }
    return mp_key(view, event);
}
static void dc_clear(void *view)
{
    for (int32_t t = 0; t < NT; t++) { mode[t] = M_OFF; delta[t][0] = delta[t][1] = 0; }
    save_wait = 60; fw_invalidate(view);
}
struct core_mpage dc_page = { 99, "DCE", "Dice + autopilot", dc_draw, dc_key, dc_enc, 0, 0, dc_clear };

void dc_tick(void *ctrl)
{
    static uint32_t now, draws, drawn_at;
    (void)ctrl;
    now++;
    if (!loaded && now > 42) { loaded = 1; load(); }
    if (loaded) pj_tick();                           /* a project loaded or saved */
    if (save_wait > 0 && --save_wait == 0) save();
    if (hl_ticks > 0) hl_ticks--;
    if (roll_anim > 0) { roll_anim--; if (roll_anim > 6 && (now & 1)) roll_face = (int32_t)rnd(6); }
    for (int32_t t = 0; t < NT; t++) {            /* the autopilot: a change every few loops */
        uint32_t l = loops[t];
        if (l == loops_done[t]) continue;
        loops_done[t] = l;
        if (mode[t] == M_AUTO && (l % (1u << every[t])) == 0) drift(t);
    }
    if (core_mview && core_mview_draws != draws) { draws = core_mview_draws; drawn_at = now; }
    if (core_mview && now - drawn_at < 10 && core_mpage_shown(core_mview) == &dc_page && (now & 1) == 0) fw_invalidate(core_mview);
}
