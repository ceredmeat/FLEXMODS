/* SPDX-License-Identifier: GPL-2.0-or-later */
/* POLYMETER for the Digitakt mk1 (core 4.1 + seq-events, OS 1.54): a master page (FUNC + LFO, "PLY") with
 * every track's length and speed at once, the pattern's polymeter on one screen.
 *
 *   The lanes: tracks 1-8, one line each as long as the track (a pixel a step, a gap every 4), with its
 *   playhead moving; the active track's lane is marked. On the right, the active track's numbers.
 *   Knobs: A LEN  the active track's length (1-64)
 *          B SPD  its speed: 2x, 3/2x, 1x, 3/4x, 1/2x, 1/4x, 1/8x
 *          C MOD  the pattern's scale mode: PAT (one length for all) or TRK (a length and speed a track)
 *          D CHG  per-track mode's master length (when the pattern changes): INF, 2-64
 *          E SET  a polymeter in one turn, for tracks 1-8: 16 (all 16), DWN (16 down to 9), PRM (primes
 *                 against 16), 3V4 (12 against 16). It sets TRK mode too.
 *   CLEAR: tracks 1-8 back to 16 steps at 1x.
 *
 * Where it is (found with the probe page): the pattern's edit buffer, the one the stock menus write: track
 * t's data from TRACK_PAT's object (911 bytes a track), its length an s16 at +904 (0x388), its speed a byte
 * at +907 (0x38b), 0 2x ... 6 1/8x; the pattern's settings 0x1ec40 on from track 1's, the master length an
 * s16 at +0x14 (per-track mode: 1 is INF), the mode at +0x19 (1: per track). */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "digitakt-mk1/seq.h"
#include "../common/mpui.h"

typedef uint32_t (*fn0)(void);
typedef uint32_t (*fn1)(uint32_t);
typedef uint32_t (*fn2)(uint32_t, uint32_t);
#ifndef APP
#define APP()          (((fn0)0x40138882)())
#define PATTERNS(a)    (((fn1)0x40015786)(a))
#define TRACK_PAT(s,t) (((fn2)0x40012a02)((s), (t)))
#endif

#define STRIDE    911
#define T_LEN     0x388
#define T_SPD     0x38b
#define SETTINGS  0x1ec40
#define S_LEN     0x14
#define S_MODE    0x19

enum { K_LEN, K_SPD, K_MOD, K_CHG, K_SET, KNOBS };
static const char *const kname[KNOBS] = { "LEN", "SPD", "MOD", "CHG", "SET" };
static const char *const sname[7] = { "2X", "3/2", "1X", "3/4", "1/2", "1/4", "1/8" };
#define PRESETS 5
static const char *const pname[PRESETS] = { "---", "16", "DWN", "PRM", "3V4" };
static const uint8_t preset[PRESETS][8] = {
    { 0 },
    { 16, 16, 16, 16, 16, 16, 16, 16 },
    { 16, 15, 14, 13, 12, 11, 10, 9 },
    { 16, 13, 11, 7, 5, 17, 19, 23 },
    { 16, 12, 16, 12, 16, 12, 16, 12 },
};

static int32_t active(void) { int32_t t = (int32_t)fw_active_track; return t < 0 ? 0 : t > 15 ? 15 : t; }

/* track t's data in the edit buffer, or 0 (UI task only: it calls the firmware) */
static uint8_t *track_data(int32_t t)
{
#ifdef PREVIEW
    return PREVIEW + STRIDE * t;
#endif
    uint32_t tp = TRACK_PAT(PATTERNS(APP()), (uint32_t)t);
    if (!tp) return 0;
    uint32_t vt = *(const uint32_t *)tp;
    if (!vt) return 0;
    uint32_t d = (*(fn1 *)(vt + 40))(tp);
    return d >= 0x40000000u && d < 0x48000000u ? (uint8_t *)d : 0;
}
static uint8_t *settings(void)
{
    uint8_t *t0 = track_data(0);
    return t0 ? t0 + SETTINGS : 0;
}
static int32_t get16(const uint8_t *p) { return (int16_t)((p[0] << 8) | p[1]); }
static void put16(uint8_t *p, int32_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static int32_t get_len(int32_t t) { uint8_t *d = track_data(t); return d ? get16(d + T_LEN) : 16; }
static int32_t get_spd(int32_t t) { uint8_t *d = track_data(t); return d && d[T_SPD] <= 6 ? d[T_SPD] : 2; }
static void set_len(int32_t t, int32_t v) { uint8_t *d = track_data(t); if (d) put16(d + T_LEN, ui_clamp(v, 1, 64)); }
static void set_spd(int32_t t, int32_t v) { uint8_t *d = track_data(t); if (d) d[T_SPD] = (uint8_t)ui_clamp(v, 0, 6); }

static int32_t turned[KNOBS], hl_k = -1, hl_ticks, set_k;
static uint32_t ticks;

static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }

static void pl_draw(void *view, void *bmp)
{
    char s[16], *p;
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    int32_t at = active();
    uint8_t *ps = settings();
    int32_t per = ps && ps[S_MODE] == 1, chg = ps ? get16(ps + S_LEN) : 16;

    /* the lanes: tracks 1-8, top to bottom, 3 pixels apart; a dash a step, scaled so the longest fills 80 */
    int32_t lens[8], most = 1;
    for (int32_t t = 0; t < 8; t++) {
        lens[t] = per ? ui_clamp(get_len(t), 1, 64) : ui_clamp(chg, 1, 64);
        if (lens[t] > most) most = lens[t];
    }
    int32_t w = 80 / most;
    if (w < 1) w = 1;
    if (w > 5) w = 5;
    for (int32_t t = 0; t < 8; t++) {
        int32_t y = 49 - t * 3, len = lens[t];
        for (int32_t i = 0; i < len; i++) {
            int32_t x = 8 + i * w;
            if (w >= 2) fw_fillrect(bmp, x, y, x + w - 2, y, 1);
            else if ((i & 3) != 3) fw_fillrect(bmp, x, y, x, y, 1);
        }
        uint32_t st = sq_clock[t].last;
        if (st != SQ_CLOCK_NONE && (int32_t)st < len) {               /* the playhead: its step, thick */
            int32_t x = 8 + (int32_t)st * w;
            fw_fillrect(bmp, x, y, x + (w >= 2 ? w - 2 : 0), y + 1, 1);
        }
        if (t == at) { fw_fillrect(bmp, 2, y - 1, 4, y + 1, 1); fw_fillrect(bmp, 5, y, 5, y, 1); }  /* marked */
    }
    /* the active track's numbers, on the right */
    p = put(s, at < 8 ? "T" : "M"); s[1] = (char)('1' + (at & 7)); s[2] = 0;
    ui_text(bmp, 92, 46, s);
    p = put(s, "L "); ui_num(p, per ? get_len(at) : chg);
    ui_text(bmp, 92, 38, s);
    p = put(s, "S "); put(p, sname[per ? get_spd(at) : 2]);
    ui_text(bmp, 92, 30, s);

    /* the knobs */
    for (int32_t k = 0; k < KNOBS; k++) {
        int32_t v = 0, max = 1;
        if (k == K_LEN) { v = per ? get_len(at) : chg; max = 64; ui_num(s, v); }
        else if (k == K_SPD) { v = per ? get_spd(at) : 2; max = 6; put(s, sname[v]); }
        else if (k == K_MOD) { v = per; max = 1; put(s, per ? "TRK" : "PAT"); }
        else if (k == K_CHG) { v = chg; max = 64; if (per && chg == 1) put(s, "INF"); else ui_num(s, chg); }
        else { v = set_k; max = PRESETS - 1; put(s, pname[set_k]); }
        ui_knob(bmp, k, kname[k], s, v, max, k == K_MOD || k == K_SET || k == K_SPD ? 2 : 0, k == hl_k && hl_ticks > 0);
    }
}

static int32_t pl_enc(void *view, void *event)
{
    int32_t knob = *(int32_t *)((char *)event + 12), turn = *(int32_t *)((char *)event + 16);
    if (knob < 1 || knob > KNOBS)
        return 0;
    int32_t k = knob - 1, at = active(), stepsz = k == K_LEN || k == K_CHG ? 4 : 16;
    uint8_t *ps = settings();
    hl_k = k; hl_ticks = 40;
    if ((turned[k] > 0) != (turn > 0)) turned[k] = 0;
    turned[k] += turn;
    while (turned[k] >= stepsz || turned[k] <= -stepsz) {
        int32_t dir = turned[k] > 0 ? 1 : -1;
        turned[k] -= dir * stepsz;
        if (!ps) break;
        int32_t per = ps[S_MODE] == 1;
        if (k == K_LEN) {
            if (per) set_len(at, get_len(at) + dir);
            else put16(ps + S_LEN, ui_clamp(get16(ps + S_LEN) + dir, 1, 64));
        } else if (k == K_SPD) {
            if (per) set_spd(at, get_spd(at) + dir);           /* speeds a track has in TRK mode only */
        } else if (k == K_MOD)
            ps[S_MODE] = dir > 0 ? 1 : 0;
        else if (k == K_CHG)
            put16(ps + S_LEN, ui_clamp(get16(ps + S_LEN) + dir, 1, 64));
        else {
            set_k = ui_clamp(set_k + dir, 0, PRESETS - 1);
            if (set_k > 0) {
                ps[S_MODE] = 1;
                for (int32_t t = 0; t < 8; t++) set_len(t, preset[set_k][t]);
            }
        }
    }
    fw_invalidate(view);
    return 1;
}

static void pl_clear(void *view)
{
    for (int32_t t = 0; t < 8; t++) { set_len(t, 16); set_spd(t, 2); }
    set_k = 0;
    fw_invalidate(view);
}

struct core_mpage pl_page = { 99, "PLY", "Polymeter", pl_draw, mp_key, pl_enc, 0, 0, pl_clear };

void pl_tick(void *ctrl)
{
    static uint32_t draws, drawn_at;
    (void)ctrl;
    ticks++;
    if (hl_ticks > 0 && --hl_ticks == 0 && core_mview)
        fw_invalidate(core_mview);
    if (core_mview && core_mview_draws != draws) {            /* the master view is on the screen */
        draws = core_mview_draws;
        drawn_at = ticks;
    }
    if (core_mview && ticks - drawn_at < 10 && (ticks & 3) == 0 && core_mpage_shown(core_mview) == &pl_page)
        fw_invalidate(core_mview);                            /* the playheads move */
}
