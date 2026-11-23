/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DASH for the Digitakt mk1 (core 4.4, OS 1.54): several pages of the active track's sound on one screen, a
 * master page (FUNC + LFO, then LEFT / RIGHT). Made to be seen whole, on the unit or in a screen dump.
 *
 *   SRC   the machine's eight knobs (slots 17-24: TUNE PLAY BR SAMP STRT LEN LOOP LEV on the sample machine)
 *   LFO   its eight (slots 1-8: SPD MULT FADE DEST WAVE SPH MODE DEP)
 *   F+A   the filter and amp slots (25-45), as bars, until their order is confirmed on a unit
 *   the bottom line: the last value that moved, by slot, and its value (the slot finder)
 *
 * It only reads the kit's copy of the sound (track t's slot k at fw_kit + 0x20 + 0xa2 t + 0x14 + 2 k, 8.8). */
#include <stdint.h>
#include "digitakt-mk1/core3.h"
#include "../common/mpui.h"

static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static int32_t word(int32_t t, int32_t k)
{
    uint8_t *kit = fw_kit;
    if (!kit || t < 0 || t > 7 || k < 0 || k > 52) return 0;
    return *(volatile int16_t *)(kit + 0x20 + 0xa2 * t + 0x14 + 2 * k);
}
static int32_t val(int32_t t, int32_t k) { int32_t v = word(t, k) >> 8; return v < 0 ? 0 : v > 127 ? 127 : v; }

static int16_t last[53];
static int32_t last_track = -1, moved = -1, moved_ticks;
static int8_t hist[8];                           /* the slots moved, oldest first (the slot finder) */
static int32_t nhist;
static const char *const src_lab[8] = { "TUNE", "PLAY", "BR", "SAMP", "STRT", "LEN", "LOOP", "LEV" };
static const char *const lfo_lab[8] = { "SPD", "MULT", "FADE", "DEST", "WAVE", "SPH", "MODE", "DEP" };

/* a panel: a title and n bars side by side, the one that last moved filled solid */
static void panel(void *bmp, int32_t x0, int32_t y0, int32_t w, const char *title, int32_t t, int32_t first, int32_t n)
{
    ui_text(bmp, x0, y0 + 16, title);
    int32_t bw = w / n;
    for (int32_t i = 0; i < n; i++) {
        int32_t k = first + i, v = val(t, k), x = x0 + i * bw, h = v * 13 / 127;
        fw_framerect(bmp, x, y0, x + bw - 2, y0 + 14, 1);
        if (h) fw_fillrect(bmp, x + 1, y0 + 1, x + bw - 3, y0 + h, 1);
        if (k == moved) fw_fillrect(bmp, x, y0 - 2, x + bw - 2, y0 - 2, 1);    /* the one that moved */
    }
}
static void dash_draw(void *view, void *bmp)
{
    char s[40], *p;
    (void)view;
    fw_fillrect(bmp, 0, 0, 127, 52, 0);
    int32_t t = (int32_t)fw_active_track;
    if (t > 7) { ui_centred(bmp, 0, 128, 26, "A MIDI TRACK: NO SOUND TO SHOW"); return; }
    p = put(s, "T"); *p++ = (char)('1' + t); *p = 0;
    ui_text(bmp, 112, 46, s);
    panel(bmp, 0, 31, 62, "SRC", t, 17, 8);
    panel(bmp, 66, 31, 46, "LFO", t, 1, 8);
    panel(bmp, 0, 9, 126, "FLTR + AMP  25-45", t, 25, 21);
    fw_fillrect(bmp, 0, 0, 127, 5, 0);
    if (nhist) {                                  /* the slot finder: every slot moved, in order */
        p = put(s, "MOVED:");
        for (int32_t i = 0; i < nhist; i++) { *p++ = ' '; p = ui_num(p, hist[i]); }
        ui_text(bmp, 0, 0, s);
    } else ui_text(bmp, 0, 0, "TURN KNOBS ON ANY PAGE  NO: CLEAR");
}
static int32_t dash_key(void *view, void *event)
{
    int32_t key = *(int32_t *)((char *)event + 12), fl = *(int32_t *)((char *)event + 16);
    if (key == 13) { if ((fl & 1) && !(fl & 0x18)) { nhist = 0; moved = -1; fw_invalidate(view); } return 1; }   /* NO: a fresh list */
    return mp_key(view, event);
}
struct core_mpage dash_page = { 99, "DSH", "Dashboard", dash_draw, dash_key, 0, 0, 0, 0 };

void dash_tick(void *ctrl)
{
    static uint32_t now, draws, drawn_at;
    (void)ctrl;
    now++;
    int32_t t = (int32_t)fw_active_track, changed = -1;
    if (t >= 0 && t < 8) {
        for (int32_t k = 1; k < 53; k++) {
            int16_t w = (int16_t)word(t, k);
            if (t == last_track && w != last[k]) changed = k;
            last[k] = w;
        }
        if (t != last_track) { last_track = t; moved = -1; nhist = 0; }
        if (changed >= 0) {
            moved = changed; moved_ticks = 0;
            if (!nhist || hist[nhist - 1] != changed) {   /* a new slot: onto the list (the oldest drops off) */
                int32_t seen = 0;
                for (int32_t i = 0; i < nhist; i++) if (hist[i] == changed) seen = 1;
                if (!seen) { if (nhist == 8) { for (int32_t i = 0; i < 7; i++) hist[i] = hist[i + 1]; nhist = 7; } hist[nhist++] = (int8_t)changed; }
            }
        }
    }
    if (core_mview && core_mview_draws != draws) { draws = core_mview_draws; drawn_at = now; }
    if (core_mview && now - drawn_at < 10 && core_mpage_shown(core_mview) == &dash_page && (now & 3) == 0) fw_invalidate(core_mview);
}
